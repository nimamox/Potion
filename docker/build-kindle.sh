#!/bin/sh
set -eu
WORKSPACE=/workspace; OUTPUT=/out; CACHE=/cache
BUILD="$CACHE/potion-cmake-armel"; STAGE="$CACHE/potion-package-stage"
MATH_TARGET="$CACHE/potion-math-target"
export KINDLE_SDK_ROOT=/opt/kindle-sdk
export CROSS_COMPILE=/opt/kindle-sdk/bin/arm-linux-gnueabi-
export CARGO_HOME="$CACHE/cargo-home"
export CARGO_TARGET_DIR="$MATH_TARGET"
export CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABI_LINKER="$KINDLE_SDK_ROOT/bin/arm-linux-gnueabi-gcc"
mkdir -p "$CARGO_HOME" "$CARGO_TARGET_DIR"
cargo build --locked --release --target armv7-unknown-linux-gnueabi \
  --manifest-path "$WORKSPACE/backend/math/Cargo.toml"
MATH_LIBRARY="$MATH_TARGET/armv7-unknown-linux-gnueabi/release/libpotion_math.a"
cmake -S "$WORKSPACE" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_TOOLCHAIN_FILE="$WORKSPACE/cmake/kindle-toolchain.cmake" \
  -DPOTION_BUILD_TESTS=OFF -DPOTION_BUILD_SIMULATOR=OFF \
  -DPOTION_MATH_LIBRARY="$MATH_LIBRARY"
cmake --build "$BUILD" --target potiond --parallel
rm -rf "$STAGE" "$OUTPUT/potion" "$OUTPUT/extensions/Potion" "$OUTPUT/documents/Potion.sh"
"$WORKSPACE/scripts/package-kindle.sh" "$KINDLE_SDK_ROOT/armel" "$BUILD" "$STAGE"
qemu-arm -r 3.0.35 "$STAGE/lib/ld-linux.so.3" --library-path "$STAGE/lib" "$STAGE/bin/potiond" --help >/dev/null
mkdir -p "$OUTPUT/extensions" "$OUTPUT/documents"
mv "$STAGE" "$OUTPUT/potion"
mv "$OUTPUT/potion/kual-extension/Potion" "$OUTPUT/extensions/Potion"
rmdir "$OUTPUT/potion/kual-extension"
mv "$OUTPUT/potion/library-launcher/Potion.sh" "$OUTPUT/documents/Potion.sh"
rmdir "$OUTPUT/potion/library-launcher"
(cd "$OUTPUT/potion" && sha256sum bin/potiond > potiond.sha256)
echo "Built Kindle bundle: $OUTPUT/potion"
echo "Built KUAL extension: $OUTPUT/extensions/Potion"
echo "Built Library launcher: $OUTPUT/documents/Potion.sh"
