#!/usr/bin/env bash
set -euo pipefail
[[ $# -eq 4 ]] || { echo "Usage: $0 POTIOND ASSETS SIMULATOR_ASSETS STATE_DIR" >&2; exit 2; }
DAEMON=$1; ASSETS=$2; SIMULATOR=$3; STATE=$4; LOG="$STATE/potiond.log"; URL=http://127.0.0.1:8766/simulator/
mkdir -p "$STATE"; chmod 700 "$STATE"
curl --silent --fail http://127.0.0.1:8766/health >/dev/null 2>&1 && { echo "Port 8766 is already occupied." >&2; exit 1; }
"$DAEMON" --simulator --assets "$ASSETS" --simulator-assets "$SIMULATOR" --data-dir "$STATE" --port 8766 >"$LOG" 2>&1 &
PID=$!; cleanup(){ kill "$PID" >/dev/null 2>&1 || true; wait "$PID" >/dev/null 2>&1 || true; }; trap cleanup EXIT; trap 'exit 0' INT TERM HUP
for _ in {1..80}; do curl --silent --fail http://127.0.0.1:8766/health >/dev/null 2>&1 && break; kill -0 "$PID" >/dev/null 2>&1 || { tail -n 40 "$LOG" >&2; exit 1; }; sleep .1; done
curl --silent --fail http://127.0.0.1:8766/health >/dev/null
case "$(uname -s)" in Darwin) open "$URL";; Linux) command -v xdg-open >/dev/null && xdg-open "$URL" >/dev/null 2>&1 || true;; esac
echo "Potion simulator: $URL"; echo "Persistent private state: $STATE"; wait "$PID"
