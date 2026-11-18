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
echo "font  : $(ls -l ./fonts/reading.bin 2>&1)" >> "$LOG"

# Keep the screensaver from firing while reading. Non-fatal if lipc is absent.
lipc-set-prop com.lab126.powerd preventScreenSaver 1 >>"$LOG" 2>&1 || \
  echo "note: lipc preventScreenSaver unavailable (non-fatal)" >> "$LOG"

# Hide the system status bar (clock / wifi / battery). The real property is
# com.lab126.pillow / disableEnablePillow — NOT winmgr_statusbar_visability
# (that property does not exist, so the old attempt silently did nothing).
# FW >= 5.6.5: hard-disable the pillow (status bar) and SIGSTOP awesome so
# the window manager stops repainting the clock. Both are reversed on exit.
# Never `stop framework` here — KUAL actions are lab126_gui children.
FW=$(grep -oE '5\.[0-9]+' /etc/prettyversion.txt 2>/dev/null | head -1)
echo "firmware: $FW" >> "$LOG"
BAR_HIDDEN=0
if lipc-set-prop com.lab126.pillow disableEnablePillow disable >>"$LOG" 2>&1; then
  BAR_HIDDEN=1
  echo "status bar: pillow disabled" >> "$LOG"
fi
# Stop the window manager's repaint loop (reversible; never killing it).
if killall -stop awesome >>"$LOG" 2>&1; then
  echo "status bar: awesome stopped (SIGSTOP)" >> "$LOG"
fi
# Newer firmware may run statusbar as its own upstart service.
if [ -f /etc/upstart/statusbar.conf ]; then
  stop statusbar >>"$LOG" 2>&1 && echo "status bar: service stopped" >> "$LOG"
fi
if [ "$BAR_HIDDEN" = 0 ]; then
  echo "note: pillow disable failed; status bar may remain visible" >> "$LOG"
fi
# Give the WM a moment to settle before we take over the framebuffer.
usleep 400000 2>/dev/null || sleep 1

echo "launching: ./bin/hipda --config $ROOT/config.ini" >> "$LOG"
./bin/hipda --config "$ROOT/config.ini" $EXTRA >> "$LOG" 2>&1
RC=$?
echo "=== hipda exit $RC  $(date) ===" >> "$LOG"

# Restore everything in reverse order.
if [ -f /etc/upstart/statusbar.conf ]; then
  start statusbar >>"$LOG" 2>&1
fi
killall -cont awesome >>"$LOG" 2>&1
lipc-set-prop com.lab126.pillow disableEnablePillow enable >>"$LOG" 2>&1
lipc-set-prop com.lab126.powerd preventScreenSaver 0 >>"$LOG" 2>&1
exit $RC
