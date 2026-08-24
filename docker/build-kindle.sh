#!/bin/sh
set -eu
WORKSPACE=/workspace; OUTPUT=/out; CACHE=/cache
BUILD="$CACHE/potion-cmake-armel"; STAGE="$CACHE/potion-package-stage"
export KINDLE_SDK_ROOT=/opt/kindle-sdk
export CROSS_COMPILE=/opt/kindle-sdk/bin/arm-linux-gnueabi-
cmake -S "$WORKSPACE" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_TOOLCHAIN_FILE="$WORKSPACE/cmake/kindle-toolchain.cmake" \
  -DPOTION_BUILD_TESTS=OFF -DPOTION_BUILD_SIMULATOR=OFF
cmake --build "$BUILD" --target potiond --parallel
rm -rf "$STAGE" "$OUTPUT/potion" "$OUTPUT/extensions/Potion"
"$WORKSPACE/scripts/package-kindle.sh" "$KINDLE_SDK_ROOT/armel" "$BUILD" "$STAGE"
qemu-arm -r 3.0.35 "$STAGE/lib/ld-linux.so.3" --library-path "$STAGE/lib" "$STAGE/bin/potiond" --help >/dev/null
mkdir -p "$OUTPUT/extensions"
mv "$STAGE" "$OUTPUT/potion"
mv "$OUTPUT/potion/kual-extension/Potion" "$OUTPUT/extensions/Potion"
rmdir "$OUTPUT/potion/kual-extension"
(cd "$OUTPUT/potion" && sha256sum bin/potiond > potiond.sha256)
echo "Built Kindle bundle: $OUTPUT/potion"
echo "Built KUAL extension: $OUTPUT/extensions/Potion"
