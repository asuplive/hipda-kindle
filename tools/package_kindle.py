#!/usr/bin/env python3
"""Assemble the ready-to-copy KUAL extension zip.

Reads the newest build/manifest_<seq>.json (the freshly linked binary) plus
the static kual/hipda config/menu/scripts, and writes
build/hipda-kindle-armv6_<seq>.zip with executable bits on the binary and
shell scripts. Nothing here overwrites an existing file: this machine's
sandbox only allows the first write to any path.
"""
import hashlib
import json
import os
import re
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = os.path.join(ROOT, "kual", "hipda")
BUILD = os.path.join(ROOT, "build")

EXEC_FILES = {"bin/hipda", "scripts/hipda.sh", "scripts/check.sh", "scripts/diag.sh"}
SKIP_NAMES = {"hipda.log", "check.txt", "cookies.txt", ".DS_Store"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def latest_manifest():
    best, seq = None, 0
    for name in os.listdir(BUILD):
        m = re.match(r"manifest_(\d+)\.json$", name)
        if m and int(m.group(1)) >= seq:
            seq = int(m.group(1))
            best = os.path.join(BUILD, name)
    return seq, best


def main() -> int:
    # Everything is READ here and the zip gets a fresh name: this sandbox
    # only allows the first write to any path, so never touch locked copies.
    seq, mpath = latest_manifest()
    if not mpath:
        print("run tools/build_kindle.py first (no manifest_*.json)", file=__import__("sys").stderr)
        return 2
    manifest = json.load(open(mpath, encoding="utf-8"))
    src = os.path.join(ROOT, manifest["binary"])
    font = os.path.join(ROOT, "fonts", "unifont.bin")
    readme = os.path.join(ROOT, "README.md")
    for p in (src, font, readme):
        if not os.path.exists(p):
            print("missing", p, file=__import__("sys").stderr)
            return 2
    zip_path = os.path.join(BUILD, "hipda-kindle-armv6_%d.zip" % seq)
    while os.path.exists(zip_path):     # never overwrite: sandbox refuses it
        seq += 1
        zip_path = os.path.join(BUILD, "hipda-kindle-armv6_%d.zip" % seq)

    files = []
    cfg_build = os.path.join(BUILD, "config.ini")
    cfg_source = cfg_build if os.path.exists(cfg_build) else os.path.join(PKG, "config.ini")
    for base, _dirs, names in os.walk(PKG):
        for n in sorted(names):
            if n in SKIP_NAMES or n == "README.md" or n == "config.ini" or \
                    n.endswith((".pgm", ".png", ".o")):
                continue
            full = os.path.join(base, n)
            rel = os.path.relpath(full, PKG).replace("\\", "/")
            if rel in ("bin/hipda", "fonts/unifont.bin"):
                continue          # stale copies; the fresh ones are appended below
            files.append((full, rel))
    files.append((cfg_source, "config.ini"))   # newest credentials, if generated
    files.append((src, "bin/hipda"))           # the freshly linked binary
    files.append((font, "fonts/unifont.bin"))
    files.append((readme, "README.md"))
    files.sort(key=lambda x: x[1])

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for full, rel in files:
            zi = zipfile.ZipInfo("extensions/hipda/" + rel)
            zi.external_attr = (0o755 if rel in EXEC_FILES else 0o644) << 16
            zi.create_system = 3  # Unix, so the mode bits survive
            with open(full, "rb") as fh:
                z.writestr(zi, fh.read(), compress_type=zipfile.ZIP_DEFLATED)

    sums = [
        (os.path.basename(zip_path), sha256(zip_path)),
        (manifest["binary"] + " (ARM executable)", sha256(src)),
        (manifest.get("lib", "libfbink") + " (ARM static lib)",
         sha256(os.path.join(ROOT, manifest["lib"])) if
         os.path.exists(os.path.join(ROOT, manifest.get("lib", ""))) else "n/a"),
        ("unifont.bin (font)", sha256(font)),
    ]
    sums = [s for s in sums if s[1] != "n/a"]
    sums_path = os.path.join(BUILD, "SHA256SUMS_%d.txt" % seq)
    with open(sums_path, "w", encoding="utf-8") as fh:
        for name, digest in sums:
            fh.write("%s  %s\n" % (digest, name))

    print("package :", zip_path)
    print("size    : %d bytes, %d files" % (os.path.getsize(zip_path), len(files)))
    for full, rel in files:
        mode = "755" if rel in EXEC_FILES else "644"
        print("  %s  extensions/hipda/%-34s %8d" %
              (mode, rel, os.path.getsize(full)))
    print("\nSHA-256")
    for name, digest in sums:
        print(" ", digest, name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
