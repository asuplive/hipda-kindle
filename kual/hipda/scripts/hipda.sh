#!/bin/sh
# KUAL entry point for hipda-kindle.
# Keep everything on /mnt/us: the root filesystem is read-only.
#
# IMPORTANT (FW 5.x + KUAL): do NOT call `framework stop` here.
# A KUAL action runs as a CHILD of lab126_gui; stopping the framework kills
# that whole process tree — including this script — before the reader binary
# ever starts. The symptom is a blank screen with no hipda.log at all.
# FBInk writes the framebuffer directly, so stopping the UI is unnecessary.

ROOT=/mnt/us/extensions/hipda
LOG=$ROOT/hipda.log
MODE="$1"
EXTRA=""

case "$MODE" in
  offline) EXTRA="--offline" ;;
esac

# Log from the very first line so we can always see how far we got.
echo "=== hipda start $(date) mode=[$MODE] shpid=$$ ===" >> "$LOG"
echo "PATH=$PATH" >> "$LOG"

cd "$ROOT" || { echo "FATAL: cd $ROOT failed" >> "$LOG"; exit 1; }
echo "cwd=$(pwd)" >> "$LOG"
echo "binary: $(ls -l ./bin/hipda 2>&1)" >> "$LOG"
echo "font  : $(ls -l ./fonts/unifont.bin 2>&1)" >> "$LOG"

# Keep the screensaver from firing while reading. Non-fatal if lipc is absent.
lipc-set-prop com.lab126.powerd preventScreenSaver 1 >>"$LOG" 2>&1 || \
  echo "note: lipc preventScreenSaver unavailable (non-fatal)" >> "$LOG"

echo "launching: ./bin/hipda $EXTRA" >> "$LOG"
./bin/hipda --config "$ROOT/config.ini" --font "$ROOT/fonts/unifont.bin" $EXTRA >> "$LOG" 2>&1
RC=$?
echo "=== hipda exit $RC  $(date) ===" >> "$LOG"

lipc-set-prop com.lab126.powerd preventScreenSaver 0 >>"$LOG" 2>&1
exit $RC
