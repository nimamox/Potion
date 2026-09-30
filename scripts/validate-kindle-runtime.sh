#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 armel|armhf RUNTIME_DIRECTORY" >&2
  exit 2
fi
KINDLE_ABI=$1
RUNTIME=$2
POTION_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${KINDLE_SDK_ROOT:?Set KINDLE_SDK_ROOT to the SDK root}"
. "$POTION_ROOT/scripts/kindle-abi.sh"
kindle_abi_configure "$KINDLE_ABI"
READELF="$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-readelf"
DAEMON="$RUNTIME/bin/potiond"
LOADER="$RUNTIME/lib/$KINDLE_LOADER"
PRELOAD="$RUNTIME/lib/libmesquite-whisper-touch.so"
test -x "$DAEMON"
test -x "$LOADER"
test -f "$PRELOAD"

INTERPRETER=$($READELF -l "$DAEMON" | sed -n 's/.*interpreter: \([^]]*\)].*/\1/p')
[ "$(basename "$INTERPRETER")" = "$KINDLE_LOADER" ] || {
  echo "$KINDLE_ABI potiond uses unexpected interpreter: $INTERPRETER" >&2
  exit 1
}
case "$KINDLE_ABI" in
  armel) EXPECTED_FLAG='soft-float ABI'; REJECTED_FLAG='hard-float ABI' ;;
  armhf) EXPECTED_FLAG='hard-float ABI'; REJECTED_FLAG='soft-float ABI' ;;
esac
ELF_LIST=$(mktemp "${TMPDIR:-/tmp}/potion-elf-list.XXXXXX")
trap 'rm -f "$ELF_LIST"' EXIT HUP INT TERM
find "$RUNTIME/bin" "$RUNTIME/lib" -type f -print | LC_ALL=C sort |
while IFS= read -r OBJECT; do
  if "$READELF" -h "$OBJECT" >/dev/null 2>&1; then printf '%s\n' "$OBJECT"; fi
done > "$ELF_LIST"
while IFS= read -r OBJECT; do
  HEADER=$($READELF -h "$OBJECT")
  printf '%s\n' "$HEADER" | grep -q 'Machine:.*ARM' || {
    echo "Non-ARM ELF in $KINDLE_ABI runtime: $OBJECT" >&2; exit 1;
  }
  printf '%s\n' "$HEADER" | grep -q "$EXPECTED_FLAG" || {
    echo "Wrong or missing $EXPECTED_FLAG marker: $OBJECT" >&2; exit 1;
  }
  if printf '%s\n' "$HEADER" | grep -q "$REJECTED_FLAG"; then
    echo "Mixed ABI marker in $KINDLE_ABI runtime: $OBJECT" >&2; exit 1
  fi
  for NEEDED in $($READELF -d "$OBJECT" 2>/dev/null |
    sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p'); do
    [ -f "$RUNTIME/lib/$NEEDED" ] || {
      echo "Unresolved packaged dependency $NEEDED required by $OBJECT" >&2
      exit 1
    }
  done
done < "$ELF_LIST"
if [ "$KINDLE_ABI" = armhf ]; then
  $READELF -A "$DAEMON" | grep -q 'Tag_ABI_VFP_args: VFP registers' || {
    echo "ARMHF potiond does not advertise VFP register arguments" >&2; exit 1;
  }
else
  if $READELF -A "$DAEMON" | grep -q 'Tag_ABI_VFP_args: VFP registers'; then
    echo "ARMEL potiond unexpectedly advertises VFP register arguments" >&2; exit 1
  fi
fi
case "$KINDLE_ABI" in
  armel) test ! -e "$RUNTIME/lib/ld-linux-armhf.so.3" ;;
  armhf) test ! -e "$RUNTIME/lib/ld-linux.so.3" ;;
esac

# QEMU validates CPU/userspace execution, not Kindle firmware or Mesquite.
qemu-arm -r 3.0.35 "$LOADER" --library-path "$RUNTIME/lib" \
  "$DAEMON" --help >/dev/null
if ! "$POTION_ROOT/scripts/smoke-kindle-backend.sh" "$KINDLE_ABI" \
  "$RUNTIME" "$POTION_ROOT/assets"; then
  echo "Warning: deeper $KINDLE_ABI QEMU backend smoke test was not feasible." >&2
fi
echo "Validated $KINDLE_ABI ELF ABI, loader, dependency closure, and QEMU loader smoke test."
