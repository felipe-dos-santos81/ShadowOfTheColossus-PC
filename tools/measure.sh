#!/usr/bin/env bash
# Ad-hoc measurement harness (macOS): enters the level with the pad file and reports the port's
# own FPS/cmd counters for a fixed window. tools/restart.sh cannot do this here: it needs setsid,
# which macOS does not have, and it waits on "Frame stats" lines this tree no longer prints.
#
#   measure.sh <label> [VAR=value ...]
set -uo pipefail
here=$(cd -- "$(dirname -- "$(realpath -- "$0")")" && pwd)
cd -- "$here/.."   # the port's root: the pad, the toggles and the logs are all out/*
label=$1; shift
log=out/session-$label.log
pkill -x bb-probe 2>/dev/null; sleep 1
: > out/pad; echo 0 > out/toggles
env BB_PAD_FILE=$PWD/out/pad BB_TOGGLE_FILE=$PWD/out/toggles BB_FPS_LIMIT=0 BB_GPU_LOG=warning \
    SOTC_FPS=1 SOTC_CMD_STATS=1 "$@" bash run.sh > "$log" 2>&1 &
pid=$!
for i in $(seq 1 60); do sleep 1; grep -q "presenter ready" "$log" && break; done
sleep 8; bash tools/press.sh cross; sleep 6; bash tools/press.sh cross
sleep 50
kill $pid 2>/dev/null; pkill -x bb-probe 2>/dev/null; sleep 1
echo "--- $label: ${*:-defaults} ---"
grep "SOTC FPS" "$log" | tail -3
grep -c "waited " "$log" | sed 's/^/stall lines: /'
grep "SOTC cmds/frame" "$log" | tail -1
