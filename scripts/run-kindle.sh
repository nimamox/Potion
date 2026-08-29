#!/bin/sh
set -eu
POTION_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
POTION_APP_ID=org.potion.app
POTION_PID_FILE=/var/tmp/potiond.pid
POTION_LOG="$POTION_ROOT/potiond.log"
POTION_DATA=/var/local/potion
POTION_CURRENT=/var/local/mesquite/potion-current
POTION_PAGE=${1:-${POTION_PAGE_ID:-}}
if [ -n "$POTION_PAGE" ]; then
  POTION_PAGE=$(printf '%s' "$POTION_PAGE" | tr -d '-')
  case "$POTION_PAGE" in *[!0-9a-fA-F]*|'') echo "Invalid Notion page ID" >&2; exit 2;; esac
  [ "${#POTION_PAGE}" -eq 32 ] || { echo "Invalid Notion page ID" >&2; exit 2; }
fi
if [ "${POTION_LAUNCHER:-0}" != 1 ]; then POTION_LAUNCHER=1 setsid "$0" "$@" </dev/null >/dev/null 2>&1 & exit 0; fi
lipc-set-prop com.lab126.appmgrd stop "app://$POTION_APP_ID" >/dev/null 2>&1 || true
if [ -f "$POTION_PID_FILE" ]; then read -r OLD_PID < "$POTION_PID_FILE" || OLD_PID=; [ -n "$OLD_PID" ] && kill "$OLD_PID" 2>/dev/null || true; fi
rm -f "$POTION_PID_FILE"; sleep 1; mkdir -p "$POTION_DATA"; chmod 700 "$POTION_DATA"
if [ -x "$POTION_ROOT/lib/ld-linux.so.3" ]; then LOADER="$POTION_ROOT/lib/ld-linux.so.3"; else echo "Missing ARM loader" >"$POTION_LOG"; exit 1; fi
: >"$POTION_LOG"
POTION_START_ARGS=
[ -z "$POTION_PAGE" ] || POTION_START_ARGS="--start-page $POTION_PAGE"
# POTION_PAGE is validated above as exactly 32 hexadecimal characters.
# shellcheck disable=SC2086
setsid "$LOADER" --library-path "$POTION_ROOT/lib" "$POTION_ROOT/bin/potiond" --assets "$POTION_ROOT/share/potion" --data-dir "$POTION_DATA" --token-import "$POTION_ROOT/notion-token.txt" --ca-bundle "$POTION_ROOT/etc/ca-certificates.crt" $POTION_START_ARGS --port 8766 >>"$POTION_LOG" 2>&1 </dev/null &
PID=$!; echo "$PID" >"$POTION_PID_FILE"
READY=0; ATTEMPTS=0
if command -v wget >/dev/null 2>&1; then
  while [ "$ATTEMPTS" -lt 50 ]; do
    kill -0 "$PID" 2>/dev/null || break
    if wget -q -O /dev/null http://127.0.0.1:8766/health 2>/dev/null; then READY=1; break; fi
    ATTEMPTS=$((ATTEMPTS + 1)); sleep 1
  done
else
  sleep 1; kill -0 "$PID" 2>/dev/null && READY=1
fi
[ "$READY" = 1 ] || { echo "Potion engine did not become ready" >>"$POTION_LOG"; kill "$PID" 2>/dev/null || true; rm -f "$POTION_PID_FILE"; exit 1; }
ASSET_ID=$(cksum "$POTION_ROOT/share/potion/index.html" "$POTION_ROOT/share/potion/app.css" "$POTION_ROOT/share/potion/app.js" | cksum | awk '{print $1}')
MESQUITE_DIR="/var/local/mesquite/potion-$ASSET_ID"; STAGE="$MESQUITE_DIR.new"; rm -rf "$STAGE"; mkdir -p "$STAGE"; cp -R "$POTION_ROOT/share/potion/." "$STAGE/"; rm -rf "$MESQUITE_DIR"; mv "$STAGE" "$MESQUITE_DIR"
if [ -f "$POTION_CURRENT" ]; then read -r OLD <"$POTION_CURRENT" || OLD=; case "$OLD" in /var/local/mesquite/potion-[0-9]*) [ "$OLD" = "$MESQUITE_DIR" ] || rm -rf "$OLD";; esac; fi
echo "$MESQUITE_DIR" >"$POTION_CURRENT"
POTION_URL="file://$MESQUITE_DIR/"
sqlite3 /var/local/appreg.db <<EOF
BEGIN IMMEDIATE;
INSERT OR IGNORE INTO interfaces(interface) VALUES('application');
INSERT OR IGNORE INTO handlerIds(handlerId) VALUES('$POTION_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','lipcId','$POTION_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','command','/usr/bin/mesquite -l $POTION_APP_ID -c $POTION_URL');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','supportedOrientation','UDLR');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','unloadPolicy','unloadOnPause');
COMMIT;
EOF
lipc-set-prop com.lab126.appmgrd start "app://$POTION_APP_ID" >>"$POTION_LOG" 2>&1 &
