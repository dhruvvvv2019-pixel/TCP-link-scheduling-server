#!/bin/sh
set -eu
POLICY="$1"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
Q="${2:-8192}"
mkdir -p "$ROOT/results"
if [ "$POLICY" = rr ] || [ "$POLICY" = drr ]; then
  "$ROOT/server" --sched "$POLICY" --quantum "$Q" --file "$ROOT/server_files" --config "$ROOT/config.json" --metrics-out "$ROOT/results/${POLICY}.csv" &
else
  "$ROOT/server" --sched "$POLICY" --file "$ROOT/server_files" --config "$ROOT/config.json" --metrics-out "$ROOT/results/${POLICY}.csv" &
fi
PID=$!
trap 'kill -INT "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true' INT TERM EXIT
sleep 1
"$ROOT/client" load "$ROOT/workload" --requests 1000 --config "$ROOT/config.json"
kill -INT "$PID"
wait "$PID"
trap - INT TERM EXIT
