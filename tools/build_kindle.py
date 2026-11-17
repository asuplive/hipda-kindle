#!/usr/bin/env python3
"""Build the deployable, statically linked ARM binary for a jailbroken Kindle.

Pipeline:
  1. tools/fetch_fbink.py          (upstream FBInk sources, if not present)
  2. compile FBInk MINIMAL+DRAW+IMAGE for FBINK_FOR_KINDLE -> libfbink.a
  3. compile all hipda-kindle sources with -DKINDLE_BUILD
  4. link everything statically -> build/hipda
  5. verify the ELF header and print the SHA-256

No `make`, no system gcc, no koxtoolchain needed: zig cc acts as the ARM
cross-compiler and zig ar builds the archive. Override with:
  HZIPDA_TARGET=arm-linux-gnueabi    (glibc static instead of musl static)
  CC="zig cc"                        (explicit compiler)
"""
import glob
import hashlib
import json
import os
import shlex
import shutil
import string
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
VENDOR = os.path.join(ROOT, "vendor", "FBInk")

APP_SRCS = [
    "src/util.c", "src/gbk.c", "src/html.c", "src/discuz.c", "src/raster.c",
    "src/font.c", "src/layout.c", "src/render.c", "src/config.c",
    "src/md5.c", "src/app.c", "src/tests.c", "src/main.c",
    "src/ui_fbink.c", "src/input_evdev.c", "src/net_curl.c",
]
FBINK_SRCS = [
    "vendor/FBInk/fbink.c",
    "vendor/FBInk/cutef8/utf8.c",
    "vendor/FBInk/cutef8/dfa.c",
    "vendor/FBInk/qimagescale/qimagescale.c",
]
TARGET = os.environ.get("HIPDA_TARGET", "arm-linux-musleabi")
# zig/clang default to armv7a. K2/K3/K4/K5 are ARM11 (ARMv6) and would die
# with "Illegal instruction", so target ARMv6 soft-float by default: ARMv6
# code runs unchanged on every later ARMv7 Kindle too.
ARM_CPU = os.environ.get("HIPDA_ARM_CPU", "arm1136jf_s")
OBJ = os.path.join(ROOT, ".cache", "arm_" + ARM_CPU)

DEFS = [
    "-DKINDLE_BUILD",
    "-DFBINK_FOR_KINDLE",
    "-DFBINK_MINIMAL",
    "-DFBINK_WITH_DRAW",
    "-DFBINK_WITH_IMAGE",
]


def zig_from_venvs():
    """ziglang wheels ship the compiler binary inside site-packages."""
    patterns = [
        os.path.expanduser("~/.workbuddy/binaries/python/envs/*/Lib/site-packages/ziglang/zig.exe"),
        os.path.expanduser("~/.workbuddy/binaries/python/envs/*/lib/python*/site-packages/ziglang/zig"),
        os.path.expanduser("~/.venvs/*/lib/python*/site-packages/ziglang/zig"),
    ]
    for pat in patterns:
        for hit in glob.glob(pat):
            return hit
    try:
        import ziglang
        p = os.path.join(os.path.dirname(ziglang.__file__),
                         "zig.exe" if os.name == "nt" else "zig")
        if os.path.exists(p):
            return p
    except ImportError:
        pass
    return None


def find_cc():
    env = os.environ.get("CC")
    if env:
        return shlex.split(env)
    z = shutil.which("zig") or zig_from_venvs()
    return [z, "cc"] if z else None


def find_zig(cc):
    if cc and cc[0].lower().endswith(("zig", "zig.exe")):
        return cc[0]
    z = shutil.which("zig") or zig_from_venvs()
    return z


def ascii_cache_root():
    """zig 0.17 on Windows cannot write its cache under a non-ASCII user
    profile ("manifest_create AccessDenied"), and reused cache directories
    tend to get locked by file-system scanners after a few runs. So: pick an
    ASCII path on a writable data volume and give EVERY compiler/linker
    invocation its own brand-new cache subdirectory."""
    if os.environ.get("ZIG_LOCAL_CACHE_DIR") or os.environ.get("HIPDA_NO_ZIG_CACHE_FIX"):
        return None
    if os.name == "nt":
        # Try volumes from Z downwards: data drives normally sit after the
        # system volume, and some old system volumes (large free space but
        # locked-down ACLs) make zig die silently when writing its cache.
        for drive in reversed(string.ascii_uppercase):
            base = drive + ":/"
            if not os.path.isdir(base):
                continue
            try:
                if shutil.disk_usage(base).free < (1 << 30):
                    continue
                cand = os.path.join(base, "hipda-zig-fresh")
                probe = os.path.join(cand, "probe", "o")
                os.makedirs(probe, exist_ok=True)
                with open(os.path.join(probe, "x.o"), "wb") as fh:
                    fh.write(b"ok")
            except OSError:
                continue
            return cand
        return None
    return os.path.expanduser("~/.cache/hipda-zig-fresh")


CACHE_ROOT = None
_CACHE_SEQ = [0]


def fresh_env():
    _CACHE_SEQ[0] += 1
    env = os.environ.copy()
    if CACHE_ROOT:
        env["ZIG_LOCAL_CACHE_DIR"] = os.path.join(CACHE_ROOT, "c%d" % _CACHE_SEQ[0])
        env["ZIG_GLOBAL_CACHE_DIR"] = os.path.join(CACHE_ROOT, "g%d" % _CACHE_SEQ[0])
    return env


def run(cmd, label):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=fresh_env())
    if r.returncode:
        print("FAILED: %s" % label, file=sys.stderr)
        print(r.stdout[-3000:], file=sys.stderr)
        print(r.stderr[-3000:], file=sys.stderr)
        err = r.stderr + r.stdout
        if any(k in err for k in ("AccessDenied", "manifest_create", "failed to rename")):
            print("\nHINT: zig could not write its cache (Windows + non-ASCII "
                  "profile / locked volume). Point both caches at a FRESH "
                  "pure-ASCII directory, e.g.:\n"
                  "  ZIG_LOCAL_CACHE_DIR=J:/zig1 ZIG_GLOBAL_CACHE_DIR=J:/zig2 \\\n"
                  "    python tools/build_kindle.py\n"
                  "The prebuilt build/hipda and build/libfbink.a are already "
                  "in this package and need no rebuild.", file=sys.stderr)
        raise SystemExit(1)
    warn = [l for l in r.stderr.splitlines() if "warning:" in l]
    if warn:
        print("  %-34s %d warning(s)" % (label, len(warn)))
    return r


def obj_name(src):
    return os.path.basename(src).replace(".c", "") + ("_fb" if "vendor/" in src else "") + ".o"


def next_build_seq():
    """This machine's sandbox only allows the FIRST write to any path; later
    overwrites die with Permission denied. So every build gets a fresh seq."""
    seq = 1
    while (os.path.exists(os.path.join(BUILD, "hipda_%d" % seq)) or
           os.path.exists(os.path.join(BUILD, "hipda-kindle-armv6_%d.zip" % seq)) or
           os.path.exists(os.path.join(BUILD, "manifest_%d.json" % seq))):
        seq += 1
    return seq


def main() -> int:
    global CACHE_ROOT
    cc = find_cc()
    zig = find_zig(cc)
    if not cc or not zig:
        print("need zig cc (or set CC)", file=sys.stderr)
        return 2
    CACHE_ROOT = ascii_cache_root()
    SEQ = next_build_seq()
    os.makedirs(OBJ, exist_ok=True)
    os.makedirs(BUILD, exist_ok=True)

    if not os.path.exists(os.path.join(VENDOR, "fbink.c")):
        print("fetching FBInk sources ...")
        subprocess.check_call([sys.executable, os.path.join("tools", "fetch_fbink.py")], cwd=ROOT)

    base = cc + ["-target", TARGET, "-mcpu=" + ARM_CPU, "-std=gnu99", "-Os",
                 "-ffunction-sections", "-fdata-sections",
                 "-Iinclude", "-Isrc", "-Ivendor/FBInk", "-Ivendor/FBInk/stb"] + DEFS

    print("target:", TARGET, "cpu:", ARM_CPU)
    print("compiler:", " ".join(cc))

    def needs_build(src, out):
        # Incremental like make: big files (fbink.c pulls in stb_image) have
        # been killed mid-compile on this machine, so never redo an up-to-date
        # object.
        if not os.path.exists(out):
            return True
        return os.path.getmtime(out) < os.path.getmtime(src)

    print("\n[1/4] FBInk static library")
    fb_objs = []
    for src in FBINK_SRCS:
        out = os.path.join(OBJ, obj_name(src))
        if needs_build(src, out):
            run(base + ["-c", src, "-o", out], "compile " + src)
        else:
            print("  up-to-date:", os.path.basename(out))
        fb_objs.append(out)
    lib = os.path.join(OBJ, "libfbink_%d.a" % SEQ)
    # Fresh archive name every build: this sandbox refuses to overwrite a
    # path that was already written once in the same session.
    subprocess.check_call([zig, "ar", "rcs", lib] + fb_objs, cwd=ROOT)
    print("  -> %s (%d bytes, %d objects)" %
          (os.path.relpath(lib, ROOT), os.path.getsize(lib), len(fb_objs)))

    print("\n[2/4] hipda-kindle sources")
    app_objs = []
    for src in APP_SRCS:
        out = os.path.join(OBJ, obj_name(src))
        if needs_build(src, out):
            run(base + ["-c", src, "-o", out], "compile " + src)
        else:
            print("  up-to-date:", os.path.basename(out))
        app_objs.append(out)
    print("  -> %d objects" % len(app_objs))

    print("\n[3/4] static link")
    exe = os.path.join(BUILD, "hipda_%d" % SEQ)
    link = cc + ["-target", TARGET, "-mcpu=" + ARM_CPU, "-static", "-Os",
                 "-o", exe] + app_objs + [lib, "-lm"]
    run(link, "link %s" % os.path.relpath(exe, ROOT))
    size = os.path.getsize(exe)
    print("  -> %s (%d bytes)" % (os.path.relpath(exe, ROOT), size))

    print("\n[4/4] verification")
    with open(exe, "rb") as fh:
        head = fh.read(20)
    if head[:4] != b"\x7fELF":
        print("  FAIL: not an ELF file", file=sys.stderr)
        return 1
    ei_class, ei_data = head[4], head[5]
    e_type, e_machine = struct.unpack_from("<HH", head, 16)
    print("  ELF: class=%s endian=%s type=%s machine=0x%02X (%s)" % (
        "32-bit" if ei_class == 1 else "64-bit",
        "little" if ei_data == 1 else "big",
        {2: "EXEC", 3: "DYN"}.get(e_type, e_type),
        e_machine, "ARM" if e_machine == 40 else "?"))
    with open(exe, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    print("  SHA-256:", digest)
    ok = (ei_class == 1 and ei_data == 1 and e_type == 2 and e_machine == 40)
    print("  static ARM executable:", "OK" if ok else "CHECK FAILED")

    manifest = {
        "target": TARGET, "binary": os.path.relpath(exe, ROOT), "size": size,
        "sha256": digest, "fbink_objects": len(fb_objs),
        "app_objects": len(app_objs), "defs": DEFS,
        "cpu": ARM_CPU, "libc": TARGET, "seq": SEQ,
        "lib": os.path.relpath(lib, ROOT),
    }
    with open(os.path.join(BUILD, "manifest_%d.json" % SEQ), "w",
              encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
    print("\nDONE -> %s + %s + build/manifest_%d.json" %
          (os.path.relpath(exe, ROOT), os.path.relpath(lib, ROOT), SEQ))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
