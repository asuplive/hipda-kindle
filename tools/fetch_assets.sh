#!/bin/sh
# Re-fetch the two upstream assets needed to rebuild the bitmap font.
# fonts/unifont.bin is already committed/generated, so this is only needed
# if you want to regenerate it or update the FBInk header.
set -e
cd "$(dirname "$0")/.."
mkdir -p .cache include fonts

echo "fetching fbink.h ..."
curl -sS -L -o .cache/fbink.h \
  "https://cdn.jsdelivr.net/gh/NiLuJe/FBInk@master/fbink.h"
cp .cache/fbink.h include/fbink.h

echo "fetching GNU Unifont 13.0.04 hex data ..."
curl -sS -L -o .cache/unifont.hex.gz \
  "https://cdn.jsdelivr.net/gh/NiLuJe/FBInk@master/fonts/unifont-13.0.04.hex.gz"
python3 -c "import gzip; open('.cache/unifont.hex','wb').write(gzip.decompress(open('.cache/unifont.hex.gz','rb').read()))"

python3 tools/gen_gbk_table.py
python3 tools/gen_font.py
echo "assets ready"
