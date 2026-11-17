#!/bin/sh
# Write a full diagnostic report to /mnt/us/extensions/hipda/check.txt.
# Copy this ONE file off over USB — it aggregates every other log.

ROOT=/mnt/us/extensions/hipda
OUT=$ROOT/check.txt

{
  echo "hipda-kindle environment check v2"
  echo "date: $(date)"
  echo "kernel: $(uname -a)"
  echo "fw: $(cat /etc/version 2>/dev/null || cat /etc/prettyversion 2>/dev/null)"
  echo "arch: $(uname -m)"
  echo "uid: $(id 2>&1)"
  echo
  echo "-- binary --"
  ls -l "$ROOT/bin/hipda" 2>&1
  echo "-- font --"
  ls -l "$ROOT/fonts/unifont.bin" 2>&1
  echo "-- config --"
  ls -l "$ROOT/config.ini" 2>&1
  echo "username set: $(grep -c '^username=.' "$ROOT/config.ini" 2>/dev/null)"
  echo "-- scripts --"
  ls -l "$ROOT/scripts/" 2>&1
  echo "-- network command --"
  command -v curl 2>&1
  command -v wget 2>&1
  echo "-- framebuffer --"
  ls -l /dev/fb0 2>&1
  echo "-- input devices --"
  ls -l /dev/input/event* 2>&1 | head -20
  echo "-- memory --"
  free 2>&1 | head -4
  echo
  echo "================ ARTIFACTS ================"
  for f in hipda.log diag.log diag.txt; do
    echo
    echo "-- $f --"
    if [ -f "$ROOT/$f" ]; then
      cat "$ROOT/$f" 2>&1
    else
      echo "(NOT FOUND — this action's script likely died before writing it)"
    fi
  done
  echo
  echo "================ END ================"
} > "$OUT" 2>&1

chmod 666 "$OUT" 2>/dev/null
exit 0
