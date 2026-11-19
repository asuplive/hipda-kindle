#!/usr/bin/env python3
"""Stage fonts for packaging.

The reader now reads standard .ttf/.otf/.ttc DIRECTLY (via stb_truetype), so
no conversion is performed. This script simply reports which fonts will be
bundled; every *.bin / *.ttf / *.otf / *.ttc in fonts/ is picked up by the
packager.
"""
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, 'fonts')


def main():
    bins = sorted(glob.glob(os.path.join(FONTS, '*.bin')))
    ttfs = sorted(glob.glob(os.path.join(FONTS, '*.ttf')) +
                  glob.glob(os.path.join(FONTS, '*.otf')) +
                  glob.glob(os.path.join(FONTS, '*.ttc')))
    print('prepare_fonts: standard fonts are used directly (no conversion)')
    print('  pre-rendered HUF1: %d .bin' % len(bins))
    print('  raw TTF/OTF/TTC  : %d files' % len(ttfs))
    for p in ttfs:
        print('    -', os.path.basename(p))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
