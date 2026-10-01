#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
mkdir -p "$ROOT/results" "$ROOT/server_files"
run() {
  policy="$1"; threads="$2"; q="$3"; tag="$4"
  sed "s/\"server_threads\": [0-9]*/\"server_threads\": $threads/" "$ROOT/config.json" > "$ROOT/config_run.json"
  rm -f "$ROOT/server_files"/*
  if [ "$policy" = rr ] || [ "$policy" = drr ]; then
    "$ROOT/server" --sched "$policy" --quantum "$q" --file "$ROOT/server_files" --config "$ROOT/config_run.json" --metrics-out "$ROOT/results/$tag.csv" > "$ROOT/results/$tag.server.log" 2>&1 &
  else
    "$ROOT/server" --sched "$policy" --file "$ROOT/server_files" --config "$ROOT/config_run.json" --metrics-out "$ROOT/results/$tag.csv" > "$ROOT/results/$tag.server.log" 2>&1 &
  fi
  pid=$!
  trap 'kill -INT "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' INT TERM EXIT
  sleep 1
  "$ROOT/client" load "$ROOT/workload" --requests 1000 --config "$ROOT/config_run.json"
  kill -INT "$pid"
  wait "$pid"
  trap - INT TERM EXIT
  python3 "$ROOT/scripts/analyze_metrics.py" "$ROOT/results/$tag.csv" > "$ROOT/results/$tag.summary.txt"
}
run fcfs 4 0 fcfs_4t
run sjf 4 0 sjf_4t
run rr 4 8192 rr_4t
run drr 4 8192 drr_4t
run fcfs 1 0 fcfs_1t
run rr 1 8192 rr_1t
