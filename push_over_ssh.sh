#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo "Usage: $0 USER@KINDLE_HOST" >&2; exit 2; }
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
TARGET=$1; BUNDLE="$ROOT/dist/potion"; EXTENSION="$ROOT/dist/extensions/Potion"; LIBRARY_LAUNCHER="$ROOT/dist/documents/Potion.sh"
[ -x "$BUNDLE/bin/potiond" ] || { echo "Run ./build_on_docker.sh first." >&2; exit 1; }
[ -f "$EXTENSION/config.xml" ] || { echo "Missing Potion KUAL extension." >&2; exit 1; }
[ -x "$LIBRARY_LAUNCHER" ] || { echo "Missing Potion Library launcher. Run ./build_on_docker.sh first." >&2; exit 1; }
ssh "$TARGET" 'if [ -f /var/tmp/potiond.pid ]; then kill "$(cat /var/tmp/potiond.pid)" 2>/dev/null || true; rm -f /var/tmp/potiond.pid; fi; mkdir -p /mnt/us/potion /mnt/us/extensions/Potion /mnt/us/documents'
if command -v rsync >/dev/null; then
  rsync -az --delete --exclude notion-token.txt --no-owner --no-group "$BUNDLE/" "$TARGET:/mnt/us/potion/"
  rsync -az --delete --no-owner --no-group "$EXTENSION/" "$TARGET:/mnt/us/extensions/Potion/"
  rsync -az --no-owner --no-group "$LIBRARY_LAUNCHER" "$TARGET:/mnt/us/documents/Potion.sh"
else
  scp -pr "$BUNDLE/." "$TARGET:/mnt/us/potion/"
  scp -pr "$EXTENSION/." "$TARGET:/mnt/us/extensions/Potion/"
  scp -p "$LIBRARY_LAUNCHER" "$TARGET:/mnt/us/documents/Potion.sh"
fi
echo "Installed Potion at $TARGET:/mnt/us/potion"
echo "Installed KUAL extension at $TARGET:/mnt/us/extensions/Potion"
echo "Installed Library launcher at $TARGET:/mnt/us/documents/Potion.sh"
