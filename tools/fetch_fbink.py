#!/usr/bin/env python3
"""Fetch the FBInk source files needed for a MINIMAL+DRAW+IMAGE Kindle build.

Why not git clone / tarball: github.com and raw.githubusercontent.com are
blocked or reset on this machine, while cdn.jsdelivr.net serves individual
files quickly and reliably. This script is the reproducible "install the
upstream source" step of tools/build_kindle.py.
"""
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "vendor", "FBInk")
BASE = "https://cdn.jsdelivr.net/gh/NiLuJe/FBInk@master/"

FILES = [
    # core library (fbink.c is the only translation unit; it #includes the
    # device-id / rotation / button / input implementation files at the end)
    "fbink.c", "fbink.h", "fbink_internal.h", "fbink_types.h",
    "fbink_string_utils.h", "fbink_rgb565.h",
    "fbink_device_id.c", "fbink_device_id.h",
    "fbink_rota_quirks.c", "fbink_rota_quirks.h",
    "fbink_input_scan.c", "fbink_input_scan.h",
    "fbink_button_scan.c", "fbink_button_scan.h",
    # vendored UTF-8 helpers (always compiled)
    "cutef8/utf8.c", "cutef8/utf8.h", "cutef8/dfa.c", "cutef8/dfa.h",
    # vendored Qt image scaler (FBINK_WITH_IMAGE)
    "qimagescale/qimagescale.c", "qimagescale/qimagescale_p.h",
    "qimagescale/qdrawhelper_p.h", "qimagescale/qglobal.h",
    "qimagescale/qrgb.h", "qimagescale/qimagescale_sse4.c",
    "qimagescale/qimagescale_neon.c",
    # Kindle e-ink ioctl definitions (FBINK_FOR_KINDLE)
    "eink/einkfb.h", "eink/mxcfb-kindle.h", "eink/mtk-kindle.h",
    "eink/frontlight-kindle.h", "eink/stylus-kindle.h",
    # only needed by other platform targets, but handy for reference
    "eink/mxcfb-kobo.h", "eink/ion-kobo.h", "eink/sunxi-kobo.h",
    "eink/mtk-kobo.h", "eink/mxcfb-cervantes.h", "eink/mxcfb-remarkable.h",
    "eink/mxcfb-pocketbook.h", "eink/mxcfb-pocketbook-compat.h",
]

# stb is a git submodule and therefore absent from the GitHub tarball.
EXTERNAL = {
    "stb/stb_image.h": "https://cdn.jsdelivr.net/gh/nothings/stb@master/stb_image.h",
}


def fetch(url: str, path: str, timeout: int = 90) -> bool:
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return True
    os.makedirs(os.path.dirname(path), exist_ok=True)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            data = r.read()
        if len(data) < 32:
            raise ValueError("suspiciously small response")
        with open(path, "wb") as fh:
            fh.write(data)
        print("  OK   %-42s %8d bytes" % (os.path.relpath(path, DEST).replace("\\", "/"), len(data)))
        return True
    except Exception as exc:
        print("  FAIL %-42s %s" % (os.path.relpath(path, DEST).replace("\\", "/"), exc))
        return False


def main() -> int:
    os.makedirs(DEST, exist_ok=True)
    print("FBInk source ->", DEST)
    ok, bad = 0, []
    for rel in FILES:
        if fetch(BASE + rel, os.path.join(DEST, rel)):
            ok += 1
        else:
            bad.append(rel)
    for rel, url in EXTERNAL.items():
        if fetch(url, os.path.join(DEST, rel)):
            ok += 1
        else:
            bad.append(rel)
    print("fetched %d files, %d failed" % (ok, len(bad)))
    if bad:
        print("missing:", ", ".join(bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    raise SystemExit(main())
