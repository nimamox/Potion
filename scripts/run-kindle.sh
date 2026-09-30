#!/bin/sh
set -eu
POTION_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
POTION_APP_ID=org.potion.app
POTION_PID_FILE=/var/tmp/potiond.pid
POTION_LOG="$POTION_ROOT/potiond.log"
POTION_DATA=/var/local/potion
POTION_CURRENT=/var/local/mesquite/potion-current
POTION_PAGE=${1:-${POTION_PAGE_ID:-}}

# ARMEL and ARMHF devices both report an ARM CPU. Select from the installed
# userspace loader before executing target code. The override is test-only.
if [ "${POTION_LAUNCHER_TEST:-0}" = 1 ]; then
  case "${POTION_LAUNCHER_TEST_ABI:-}" in
    armel|armhf) POTION_ABI=$POTION_LAUNCHER_TEST_ABI ;;
    *) echo "POTION_LAUNCHER_TEST_ABI must be armel or armhf" >&2; exit 2 ;;
  esac
elif [ -e /lib/ld-linux-armhf.so.3 ]; then
  POTION_ABI=armhf
elif [ -e /lib/ld-linux.so.3 ]; then
  POTION_ABI=armel
else
  echo "Cannot determine a compatible Kindle userspace ABI" >> "$POTION_LOG"
  exit 1
fi
POTION_RUNTIME="$POTION_ROOT/$POTION_ABI"
case "$POTION_ABI" in
  armhf) POTION_LOADER="$POTION_RUNTIME/lib/ld-linux-armhf.so.3" ;;
  armel) POTION_LOADER="$POTION_RUNTIME/lib/ld-linux.so.3" ;;
esac
POTION_LIBRARY_PATH="$POTION_RUNTIME/lib"
POTION_DAEMON="$POTION_RUNTIME/bin/potiond"
POTION_PRELOAD="$POTION_RUNTIME/lib/libmesquite-whisper-touch.so"
POTION_USE_WHISPER_TOUCH=0
[ "$POTION_ABI" = armel ] && POTION_USE_WHISPER_TOUCH=1
if [ ! -x "$POTION_DAEMON" ] || [ ! -x "$POTION_LOADER" ] ||
   { [ "$POTION_USE_WHISPER_TOUCH" = 1 ] && [ ! -f "$POTION_PRELOAD" ]; }; then
  echo "Incomplete bundled $POTION_ABI runtime" >> "$POTION_LOG"
  exit 1
fi
if [ "${POTION_LAUNCHER_TEST:-0}" = 1 ]; then
  echo "ABI=$POTION_ABI"
  echo "RUNTIME=$POTION_RUNTIME"
  echo "EXECUTABLE=$POTION_DAEMON"
  echo "LOADER=$POTION_LOADER"
  echo "LIBRARY_PATH=$POTION_LIBRARY_PATH"
  if [ "$POTION_USE_WHISPER_TOUCH" = 1 ]; then
    echo "WHISPER_TOUCH_PRELOAD=enabled"
    echo "PRELOAD=$POTION_PRELOAD"
  else
    echo "WHISPER_TOUCH_PRELOAD=disabled"
    echo "PRELOAD="
  fi
  echo "ASSETS=$POTION_ROOT/share/potion"
  exit 0
fi
if [ -n "$POTION_PAGE" ]; then
  POTION_PAGE=$(printf '%s' "$POTION_PAGE" | tr -d '-')
  case "$POTION_PAGE" in *[!0-9a-fA-F]*|'') echo "Invalid Notion page ID" >&2; exit 2;; esac
  [ "${#POTION_PAGE}" -eq 32 ] || { echo "Invalid Notion page ID" >&2; exit 2; }
fi
if [ "${POTION_LAUNCHER:-0}" != 1 ]; then POTION_LAUNCHER=1 setsid "$0" "$@" </dev/null >/dev/null 2>&1 & exit 0; fi
lipc-set-prop com.lab126.appmgrd stop "app://$POTION_APP_ID" >/dev/null 2>&1 || true
if [ -f "$POTION_PID_FILE" ]; then read -r OLD_PID < "$POTION_PID_FILE" || OLD_PID=; [ -n "$OLD_PID" ] && kill "$OLD_PID" 2>/dev/null || true; fi
rm -f "$POTION_PID_FILE"; sleep 1; mkdir -p "$POTION_DATA"; chmod 700 "$POTION_DATA"
: >"$POTION_LOG"
POTION_START_ARGS=
[ -z "$POTION_PAGE" ] || POTION_START_ARGS="--start-page $POTION_PAGE"
# POTION_PAGE is validated above as exactly 32 hexadecimal characters.
# shellcheck disable=SC2086
setsid "$POTION_LOADER" --library-path "$POTION_LIBRARY_PATH" "$POTION_DAEMON" --assets "$POTION_ROOT/share/potion" --data-dir "$POTION_DATA" --token-import "$POTION_ROOT/notion-token.txt" --ca-bundle "$POTION_ROOT/etc/ca-certificates.crt" $POTION_START_ARGS --port 8766 >>"$POTION_LOG" 2>&1 </dev/null &
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
POTION_MESQUITE_ENV="FONTCONFIG_FILE=$POTION_ROOT/etc/fontconfig-potion.conf"
if [ "$POTION_USE_WHISPER_TOUCH" = 1 ]; then
  POTION_MESQUITE_ENV="$POTION_MESQUITE_ENV LD_PRELOAD=$POTION_PRELOAD"
fi
sqlite3 /var/local/appreg.db <<EOF
BEGIN IMMEDIATE;
INSERT OR IGNORE INTO interfaces(interface) VALUES('application');
INSERT OR IGNORE INTO handlerIds(handlerId) VALUES('$POTION_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','lipcId','$POTION_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','command','/usr/bin/env $POTION_MESQUITE_ENV /usr/bin/mesquite -l $POTION_APP_ID -c $POTION_URL');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','supportedOrientation','UDLR');
INSERT OR REPLACE INTO properties(handlerId,name,value) VALUES('$POTION_APP_ID','unloadPolicy','unloadOnPause');
COMMIT;
EOF
lipc-set-prop com.lab126.appmgrd start "app://$POTION_APP_ID" >>"$POTION_LOG" 2>&1 &
