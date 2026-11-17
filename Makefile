# hipda-kindle -- build file
#
# One source tree, three build lines:
#   make pc           -> native CLI (fixtures, selftests, previews)
#   make kindle-easy  -> RECOMMENDED: zig cc ARM cross-build + libfbink.a + zip
#   make kindle       -> alternative: your own koxtoolchain + prebuilt libfbink.a
#
# The Kindle build only needs FBInk's minimal feature set (framebuffer +
# raw pixel blit + refresh). No FreeType/HarfBuzz/GTK/Qt is required.
#
CROSS      ?= arm-linux-gnueabi-
CC_PC      ?= cc
FBINK_LIB  ?= vendor/FBInk/Release
OUT_PC     ?= hipda-pc
OUT_KINDLE ?= build/hipda

INC   := -Iinclude -Isrc
CFLAGS_PC := -std=c99 -DPC_BUILD -Wall -O1 -g
CFLAGS_K  := -std=c99 -DKINDLE_BUILD -Wall -Os

COMMON := src/util.c src/gbk.c src/html.c src/discuz.c src/raster.c \
          src/font.c src/layout.c src/render.c src/config.c src/md5.c \
          src/image.c src/app.c src/tests.c src/main.c
PC_SRC     := src/ui_stub.c src/input_stub.c src/net_stub.c
KINDLE_SRC := src/ui_fbink.c src/input_evdev.c src/net_curl.c

.PHONY: all pc test preview fixtures fonts kindle kindle-easy check-arm fbink-help clean

all: pc

pc: $(OUT_PC)

$(OUT_PC): $(COMMON) $(PC_SRC) $(wildcard include/*.h) $(wildcard src/*.h)
	$(CC_PC) $(CFLAGS_PC) $(INC) -o $@ $(COMMON) $(PC_SRC) -lm

test: $(OUT_PC)
	./$(OUT_PC) --selftest

fixtures:
	python3 tools/make_fixtures.py

fonts:
	python3 tools/gen_gbk_table.py
	python3 tools/gen_font.py

preview: $(OUT_PC) fixtures
	HIPDA_PREVIEW_DIR=preview/k3  ./$(OUT_PC) --export --screen 600x800@167
	HIPDA_PREVIEW_DIR=preview/pw3 ./$(OUT_PC) --export --screen 758x1024@212
	HIPDA_PREVIEW_DIR=preview/pw4 ./$(OUT_PC) --export --screen 1072x1448@300
	python3 tools/build_preview.py
	node tools/check_preview.js

kindle-easy:
	python3 tools/fetch_fbink.py
	python3 tools/build_kindle.py
	python3 tools/check_elf.py
	python3 tools/package_kindle.py

check-arm:
	python3 tools/check_elf.py

kindle: $(OUT_KINDLE)

$(OUT_KINDLE): $(COMMON) $(KINDLE_SRC) $(wildcard include/*.h) $(wildcard src/*.h)
	mkdir -p build
	$(CROSS)gcc $(CFLAGS_K) $(INC) -static -o $@ $(COMMON) $(KINDLE_SRC) \
	    -L$(FBINK_LIB) -lfbink -lm -lz

fbink-help:
	@echo "1) git clone https://github.com/NiLuJe/FBInk.git vendor/FBInk"
	@echo "2) cd vendor/FBInk && make kindle MINIMAL=1 DRAW=1 IMAGE=1"
	@echo "   (needs koxtoolchain's arm-linux-gnueabi-* in PATH)"
	@echo "3) make kindle CROSS=arm-linux-gnueabi- FBINK_LIB=vendor/FBInk/Release"

clean:
	rm -f $(OUT_PC) $(OUT_KINDLE)
	rm -f preview/*.pgm preview/*.png preview/pages.json
