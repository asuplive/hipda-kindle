#!/usr/bin/env python3
"""Convert the PGM frames emitted by the C renderer into PNGs, then inject the
real layout data into the interactive HTML preview.

The PNGs are the exact Y8 pixels the Kindle backend sends to FBInk -- this is
not a mock-up drawn separately in HTML.
"""
import base64
import glob
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "preview")

DEVICES = [
    ("k3",  "600x800@167",   'Kindle 3 / 4 · 600×800 · 167ppi · 物理翻页键'),
    ("pw3", "758x1024@212",  'Paperwhite 3 / Voyage · 758×1024 · 212ppi'),
    ("pw4", "1072x1448@300", 'Paperwhite 4 / Oasis · 1072×1448 · 300ppi'),
]


def read_pgm(path):
    with open(path, "rb") as fh:
        data = fh.read()
    tokens, i, pos = [], 0, 0
    while len(tokens) < 4 and pos < len(data):
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] not in (b"\n", b"\r"):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
        pos += 1
    if len(tokens) < 4 or tokens[0] != b"P5":
        raise ValueError("not a P5 PGM: " + path)
    w, h, maxv = int(tokens[1]), int(tokens[2]), int(tokens[3])
    if maxv != 255:
        raise ValueError("only 8-bit PGM supported")
    pixels = data[pos:pos + w * h]
    if len(pixels) != w * h:
        raise ValueError("truncated PGM: " + path)
    return w, h, pixels


def main() -> int:
    try:
        from PIL import Image
    except ImportError:
        print("Pillow is required: pip install Pillow", file=sys.stderr)
        return 1

    all_data = {}
    total_png = 0
    for name, screen, label in DEVICES:
        d = os.path.join(PREVIEW, name)
        jp = os.path.join(d, "pages.json")
        if not os.path.exists(jp):
            print("missing %s -- run tools/build.py first" % jp, file=sys.stderr)
            return 1
        with open(jp, "r", encoding="utf-8") as fh:
            pages = json.load(fh)
        for pg in pages:
            pgm = os.path.join(d, pg["png"].replace(".png", ".pgm"))
            w, h, pixels = read_pgm(pgm)
            img = Image.frombytes("L", (w, h), pixels)
            png_path = os.path.join(d, pg["png"])
            img.save(png_path, optimize=True)
            pg["w"], pg["h"] = w, h
            # Inline the frame so index.html works from file://, a preview
            # iframe, or a sandbox with no access to sibling files.
            with open(png_path, "rb") as fh:
                pg["data"] = "data:image/png;base64," + base64.b64encode(fh.read()).decode("ascii")
            total_png += 1
        # index of the first screen of each view, for "tap a row" navigation
        starts = {}
        for i, pg in enumerate(pages):
            starts.setdefault(pg["view"], i)
        all_data[name] = {
            "label": label, "screen": screen, "dir": name,
            "pages": pages, "viewStarts": starts,
        }
        print("%-4s %3d screens, %s" % (name, len(pages), label))

    payload = json.dumps(all_data, ensure_ascii=False, separators=(",", ":"))
    with open(os.path.join(PREVIEW, "data.js"), "w", encoding="utf-8") as fh:
        fh.write("window.HIPDA_PREVIEW = " + payload + ";\n")

    tpl = os.path.join(PREVIEW, "template.html")
    with open(tpl, "r", encoding="utf-8") as fh:
        html = fh.read()
    if "/*__DATA__*/" not in html:
        print("template.html is missing the /*__DATA__*/ placeholder", file=sys.stderr)
        return 1
    html = html.replace("/*__DATA__*/", "window.HIPDA_PREVIEW = " + payload + ";")
    with open(os.path.join(PREVIEW, "index.html"), "w", encoding="utf-8") as fh:
        fh.write(html)
    print("wrote %d PNGs, preview/data.js and preview/index.html" % total_png)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
