#!/usr/bin/env python3
"""One-shot PC build + regression run + multi-device preview export.

Works on Windows/Linux/macOS. Compiler resolution:
  $CC (may contain spaces) -> `zig cc` -> gcc/clang/cc -> python ziglang wheel
"""
import json
import os
import shlex
import shutil
import string
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "hipda-pc.exe" if os.name == "nt" else "hipda-pc")

COMMON = [
    "src/util.c", "src/gbk.c", "src/html.c", "src/discuz.c", "src/raster.c",
    "src/font.c", "src/font_ttf.c", "src/layout.c", "src/render.c", "src/config.c",
    "src/md5.c", "src/image.c", "src/app.c", "src/tests.c", "src/main.c",
]
PC_SRC = ["src/ui_stub.c", "src/input_stub.c", "src/net_stub.c"]

DEVICES = [
    ("k3",  "600x800@167",     "Kindle 3 / 4 (6\", 167ppi, 物理翻页键)"),
    ("pw3", "758x1024@212",    "Paperwhite 3 / Voyage (6\", 212ppi)"),
    ("pw4", "1072x1448@300",   "Paperwhite 4 / Oasis (6-7\", 300ppi)"),
]


def find_cc():
    env = os.environ.get("CC")
    if env:
        return shlex.split(env)
    z = shutil.which("zig")
    if z:
        return [z, "cc"]
    try:
        import ziglang
        p = os.path.join(os.path.dirname(ziglang.__file__),
                         "zig.exe" if os.name == "nt" else "zig")
        if os.path.exists(p):
            return [p, "cc"]
    except ImportError:
        pass
    for name in ("gcc", "clang", "cc"):
        p = shutil.which(name)
        if p:
            return [p]
    return None


ENV = None


def prepare_zig_cache():
    """Windows + non-ASCII home makes zig fail with manifest_create
    AccessDenied. Pick a writable pure-ASCII cache on a data volume."""
    env = os.environ.copy()
    if os.environ.get("HIPDA_NO_ZIG_CACHE_FIX"):
        return env
    if env.get("ZIG_LOCAL_CACHE_DIR") and env.get("ZIG_GLOBAL_CACHE_DIR"):
        return env
    if os.name == "nt":
        for drive in reversed(string.ascii_uppercase):
            base = drive + ":/"
            if not os.path.isdir(base):
                continue
            try:
                if shutil.disk_usage(base).free < (1 << 30):
                    continue
                cand = os.path.join(base, "hipda-pc-zig")
                probe = os.path.join(cand, "local", "o", "probe")
                os.makedirs(probe, exist_ok=True)
                with open(os.path.join(probe, "x.o"), "wb") as fh:
                    fh.write(b"ok")
            except OSError:
                continue
            env.setdefault("ZIG_LOCAL_CACHE_DIR", os.path.join(cand, "local"))
            env.setdefault("ZIG_GLOBAL_CACHE_DIR", os.path.join(cand, "global"))
            print("zig cache:", cand)
            break
    return env


def run(cmd, **kw):
    print("+", " ".join(shlex.quote(x) for x in cmd))
    kw.setdefault("env", ENV)
    return subprocess.call(cmd, cwd=ROOT, **kw)


def main() -> int:
    global ENV
    ENV = prepare_zig_cache()
    cc = find_cc()
    if not cc:
        print("No C compiler found. Set CC=... or install zig / gcc.", file=sys.stderr)
        return 2
    print("compiler:", " ".join(cc))
    # One zig cc invocation per translation unit: parallel multi-file
    # compiles race on the shared cache and die with AccessDenied on this
    # machine (Windows Defender locks freshly renamed .obj files).
    objdir = os.path.join(ROOT, ".cache", "pc_obj")
    os.makedirs(objdir, exist_ok=True)
    objs = []
    failed = False
    for src in COMMON + PC_SRC:
        out = os.path.join(objdir, os.path.basename(src) + ".obj")
        if run(cc + ["-std=c99", "-DPC_BUILD", "-Wall", "-O1", "-g",
                     "-Iinclude", "-Isrc", "-Ivendor", "-c", src, "-o", out]):
            print("compile FAILED:", src, file=sys.stderr)
            failed = True
            break
        objs.append(out)
    if failed:
        return 1
    if run(cc + ["-std=c99", "-DPC_BUILD", "-Wall", "-O1", "-g",
                 "-Iinclude", "-Isrc", "-Ivendor", "-o", EXE] + objs + ["-lm"]):
        print("link FAILED", file=sys.stderr)
        return 1
    print("built:", EXE)

    print("\n=== selftest ===")
    if run([EXE, "--selftest"]):
        print("selftest FAILED", file=sys.stderr)
        return 1

    print("\n=== export previews ===")
    for name, screen, _label in DEVICES:
        env = os.environ.copy()
        env["HIPDA_PREVIEW_DIR"] = "preview/" + name
        d = os.path.join(ROOT, "preview", name)
        os.makedirs(d, exist_ok=True)
        # Frames are overwritten in place; never delete files here (some
        # sandboxes block os.remove, and stale frames are harmless).
        r = run([EXE, "--export", "--screen", screen], env=env)
        if r:
            print("export FAILED for", name, file=sys.stderr)
            return 1
        p = os.path.join(d, "pages.json")
        with open(p, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        print("  %-4s %s -> %d screens" % (name, screen, len(data)))

    print("\n=== build preview site ===")
    if run([sys.executable, os.path.join("tools", "build_preview.py")]):
        return 1
    print("\nDONE -> preview/index.html")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
