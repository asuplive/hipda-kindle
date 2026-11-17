#!/bin/sh
# Screen self-test: draws a high-contrast pattern with NO font / network.
# Do NOT stop the framework — see hipda.sh for why that kills KUAL actions.
# Result is written to diag.txt / diag.log in this folder; copy them off USB.

ROOT=/mnt/us/extensions/hipda
LOG=$ROOT/diag.log

echo "=== diag start $(date) shpid=$$ ===" > "$LOG"
cd "$ROOT" || { echo "FATAL: cd failed" >> "$LOG"; exit 1; }
echo "binary: $(ls -l ./bin/hipda 2>&1)" >> "$LOG"

./bin/hipda --diag >> "$LOG" 2>&1
RC=$?
echo "=== diag exit $RC $(date) ===" >> "$LOG"
exit $RC
