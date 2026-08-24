#!/bin/sh
set -eu
[ "$#" -eq 3 ] || { echo "Usage: $0 SYSROOT BUILD_DIR OUTPUT" >&2; exit 2; }
SYSROOT=$1; BUILD=$2; OUTPUT=$3
ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
READELF=${CROSS_COMPILE:-arm-linux-gnueabi-}readelf
STRIP=${CROSS_COMPILE:-arm-linux-gnueabi-}strip
[ -x "$BUILD/potiond" ] || { echo "Missing $BUILD/potiond" >&2; exit 1; }
[ ! -e "$OUTPUT" ] || { echo "Output already exists: $OUTPUT" >&2; exit 1; }
mkdir -p "$OUTPUT/bin" "$OUTPUT/lib" "$OUTPUT/etc" "$OUTPUT/share/potion" \
  "$OUTPUT/kual-extension/Potion" "$OUTPUT/library-launcher"
cp "$BUILD/potiond" "$OUTPUT/bin/potiond"
cp "$ROOT/assets/index.html" "$ROOT/assets/app.css" "$ROOT/assets/app.js" \
  "$ROOT/assets/config.xml" "$ROOT/assets/potion_logo.png" "$ROOT/logo/potion_logo_orig_size.png" "$OUTPUT/share/potion/"
cp -R "$ROOT/assets/vendor" "$OUTPUT/share/potion/"
cp "$ROOT/scripts/run-kindle.sh" "$OUTPUT/potion.sh"
cp "$ROOT/packaging/README-KINDLE.txt" "$OUTPUT/README.txt"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/SOURCE.md" "$OUTPUT/"
cp -R "$ROOT/LICENSES" "$OUTPUT/LICENSES"
cp "$ROOT/assets/kual/config.xml" "$ROOT/assets/kual/menu.json" "$OUTPUT/kual-extension/Potion/"
ICON_DATA=$(base64 < "$ROOT/logo/Potion_thumb_library.png" | tr -d '\r\n')
sed "s|@ICON_DATA@|$ICON_DATA|" "$ROOT/packaging/library/Potion.sh.in" \
  > "$OUTPUT/library-launcher/Potion.sh"
cp /etc/ssl/certs/ca-certificates.crt "$OUTPUT/etc/ca-certificates.crt"
chmod 755 "$OUTPUT/bin/potiond" "$OUTPUT/potion.sh" "$OUTPUT/library-launcher/Potion.sh"

QUEUE=$(mktemp "${TMPDIR:-/tmp}/potion-queue.XXXXXX")
SEEN=$(mktemp "${TMPDIR:-/tmp}/potion-seen.XXXXXX")
trap 'rm -f "$QUEUE" "$SEEN"' EXIT HUP INT TERM
echo "$OUTPUT/bin/potiond" > "$QUEUE"
find_library() {
  NAME=$1; [ -f "$OUTPUT/lib/$NAME" ] && return
  SOURCE=$(find -L "$SYSROOT/lib" "$SYSROOT/usr/lib" \( -type f -o -type l \) -name "$NAME" 2>/dev/null | head -n 1)
  [ -n "$SOURCE" ] || { echo "Missing target library: $NAME" >&2; exit 1; }
  cp -L "$SOURCE" "$OUTPUT/lib/$NAME"; echo "$OUTPUT/lib/$NAME" >> "$QUEUE"
}
INTERPRETER=$($READELF -l "$BUILD/potiond" | sed -n 's/.*interpreter: \([^]]*\)].*/\1/p')
[ -n "$INTERPRETER" ] || { echo "Cannot determine dynamic loader" >&2; exit 1; }
find_library "$(basename "$INTERPRETER")"
INDEX=1
while :; do
  OBJECT=$(sed -n "${INDEX}p" "$QUEUE"); [ -n "$OBJECT" ] || break; INDEX=$((INDEX + 1))
  for NEEDED in $($READELF -d "$OBJECT" 2>/dev/null | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p'); do
    if ! grep -Fxq "$NEEDED" "$SEEN"; then echo "$NEEDED" >> "$SEEN"; find_library "$NEEDED"; fi
  done
done
$STRIP --strip-unneeded "$OUTPUT/bin/potiond"
for LIBRARY in "$OUTPUT/lib/"*; do $STRIP --strip-unneeded "$LIBRARY" 2>/dev/null || true; done
echo "Potion Kindle bundle created at $OUTPUT"
