#!/bin/sh
set -eu

WORKSPACE=/workspace
OUTPUT=/out
CACHE=/cache
REQUESTED_ABI=${KINDLE_ABI:-universal}
export KINDLE_SDK_ROOT=/opt/kindle-sdk
export CARGO_HOME="$CACHE/cargo-home"
. "$WORKSPACE/scripts/kindle-abi.sh"
. "$WORKSPACE/scripts/kindle-optimization.sh"
kindle_optimization_configure
export CARGO_TARGET_DIR="$CACHE/potion-math-target-$KINDLE_OPT_PROFILE"

case "$REQUESTED_ABI" in
  universal|all) BUILD_ABIS="armel armhf" ;;
  armel|armhf) BUILD_ABIS=$REQUESTED_ABI ;;
  *) echo "KINDLE_ABI must be universal, armel, or armhf" >&2; exit 2 ;;
esac
mkdir -p "$CARGO_HOME" "$CARGO_TARGET_DIR"

# rsync preserves mtimes; cached objects must follow input content as well as flags.
CMAKE_INPUT_FINGERPRINT=$(
  {
    printf '%s\n' "$WORKSPACE/CMakeLists.txt"
    find "$WORKSPACE/cmake" "$WORKSPACE/include" "$WORKSPACE/src" -type f -print
  } | LC_ALL=C sort | while IFS= read -r POTION_INPUT; do
    sha256sum "$POTION_INPUT"
  done | sha256sum | awk '{print $1}'
)

for BUILD_ABI in $BUILD_ABIS; do
  kindle_abi_configure "$BUILD_ABI"
  kindle_rust_flags_configure
  sh "$WORKSPACE/scripts/check-rust-float-abi.sh"
  BUILD="$CACHE/potion-cmake-$KINDLE_ABI-$KINDLE_OPT_PROFILE"
  ABI_STAGE="$CACHE/potion-package-stage-$KINDLE_ABI"
  RUST_TARGET_ENV=$(printf '%s' "$KINDLE_RUST_TARGET" | tr '[:lower:]-' '[:upper:]_')
  RUST_TARGET_VAR=$(printf '%s' "$KINDLE_RUST_TARGET" | tr '-' '_')
  export "CARGO_TARGET_${RUST_TARGET_ENV}_LINKER=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-gcc"
  export "CC_${RUST_TARGET_VAR}=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-gcc"
  export "CXX_${RUST_TARGET_VAR}=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-g++"
  export "AR_${RUST_TARGET_VAR}=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-ar"
  export "CFLAGS_${RUST_TARGET_VAR}=$KINDLE_ARCH_FLAGS -O$KINDLE_CPP_OPT_LEVEL"
  export "CXXFLAGS_${RUST_TARGET_VAR}=$KINDLE_ARCH_FLAGS -O$KINDLE_CPP_OPT_LEVEL"

  echo "Building Potion runtime for $KINDLE_ABI"
  cargo build --locked --release --target "$KINDLE_RUST_TARGET" \
    --manifest-path "$WORKSPACE/backend/math/Cargo.toml"
  MATH_LIBRARY="$CARGO_TARGET_DIR/$KINDLE_RUST_TARGET/release/libpotion_math.a"
  cmake -S "$WORKSPACE" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$KINDLE_ARCH_FLAGS" -DCMAKE_CXX_FLAGS="$KINDLE_ARCH_FLAGS" \
    -DCMAKE_C_FLAGS_RELEASE="-O$KINDLE_CPP_OPT_LEVEL -DNDEBUG" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O$KINDLE_CPP_OPT_LEVEL -DNDEBUG" \
    -DPOTION_ENABLE_IPO="$KINDLE_IPO" -DPOTION_VECTOR_REPORT="$KINDLE_VECTOR_REPORT" \
    -DCMAKE_TOOLCHAIN_FILE="$WORKSPACE/cmake/kindle-toolchain.cmake" \
    -DPOTION_BUILD_TESTS=OFF -DPOTION_BUILD_SIMULATOR=OFF \
    -DPOTION_MATH_LIBRARY="$MATH_LIBRARY"
  FINGERPRINT_FILE="$BUILD/.potion-cmake-input-fingerprint"
  if [ ! -f "$FINGERPRINT_FILE" ] ||
     [ "$(cat "$FINGERPRINT_FILE")" != "$CMAKE_INPUT_FINGERPRINT" ]; then
    echo "Potion CMake inputs changed; invalidating cached C++ objects."
    cmake --build "$BUILD" --target clean
  fi
  cmake --build "$BUILD" --target potiond --parallel
  printf '%s\n' "$CMAKE_INPUT_FINGERPRINT" > "$FINGERPRINT_FILE"
  rm -rf "$ABI_STAGE"
  "$WORKSPACE/scripts/package-kindle.sh" "$KINDLE_ABI" \
    "$KINDLE_SDK_ROOT/$KINDLE_ABI" "$BUILD" "$ABI_STAGE"
  "$WORKSPACE/scripts/validate-kindle-runtime.sh" "$KINDLE_ABI" "$ABI_STAGE"
done

FIRST_ABI=${BUILD_ABIS%% *}
COMMON_STAGE="$CACHE/potion-package-stage-$FIRST_ABI"
PACKAGE_STAGE="$CACHE/potion-universal-stage"
DIST_STAGE="$CACHE/potion-dist-stage"
rm -rf "$PACKAGE_STAGE" "$DIST_STAGE"
mkdir -p "$PACKAGE_STAGE" "$DIST_STAGE/extensions" "$DIST_STAGE/documents"

# Shared browser assets, configuration, notices, fonts, and CA certificates are
# copied once; only native executables/libraries are duplicated by ABI.
for COMMON_PATH in potion.sh share etc README.txt LICENSE LICENSES SOURCE.md \
  THIRD_PARTY_NOTICES.md kual-extension library-launcher; do
  cp -a "$COMMON_STAGE/$COMMON_PATH" "$PACKAGE_STAGE/$COMMON_PATH"
done
for BUILD_ABI in $BUILD_ABIS; do
  mkdir -p "$PACKAGE_STAGE/$BUILD_ABI"
  cp -a "$CACHE/potion-package-stage-$BUILD_ABI/bin" \
    "$CACHE/potion-package-stage-$BUILD_ABI/lib" "$PACKAGE_STAGE/$BUILD_ABI/"
  test ! -e "$PACKAGE_STAGE/$BUILD_ABI/share"
  (
    cd "$PACKAGE_STAGE"
    sha256sum "$BUILD_ABI/bin/potiond" > "potiond-$BUILD_ABI.sha256"
  )

  LAUNCH_PLAN=$(POTION_LAUNCHER_TEST=1 POTION_LAUNCHER_TEST_ABI="$BUILD_ABI" \
    "$PACKAGE_STAGE/potion.sh")
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "ABI=$BUILD_ABI" >/dev/null
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "EXECUTABLE=$PACKAGE_STAGE/$BUILD_ABI/bin/potiond" >/dev/null
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "LIBRARY_PATH=$PACKAGE_STAGE/$BUILD_ABI/lib" >/dev/null
  case "$BUILD_ABI" in
    armel)
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "WHISPER_TOUCH_PRELOAD=enabled" >/dev/null
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
        "PRELOAD=$PACKAGE_STAGE/$BUILD_ABI/lib/libmesquite-whisper-touch.so" >/dev/null
      ;;
    armhf)
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "WHISPER_TOUCH_PRELOAD=disabled" >/dev/null
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "PRELOAD=" >/dev/null
      ;;
  esac
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "ASSETS=$PACKAGE_STAGE/share/potion" >/dev/null
  case "$BUILD_ABI" in
    armel) EXPECTED_LOADER=ld-linux.so.3 ;;
    armhf) EXPECTED_LOADER=ld-linux-armhf.so.3 ;;
  esac
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "LOADER=$PACKAGE_STAGE/$BUILD_ABI/lib/$EXPECTED_LOADER" >/dev/null
done

mv "$PACKAGE_STAGE/kual-extension/Potion" "$DIST_STAGE/extensions/Potion"
rmdir "$PACKAGE_STAGE/kual-extension"
mv "$PACKAGE_STAGE/library-launcher/Potion.sh" "$DIST_STAGE/documents/Potion.sh"
rmdir "$PACKAGE_STAGE/library-launcher"
mv "$PACKAGE_STAGE" "$DIST_STAGE/potion"

# A failed second ABI must not overwrite the last complete local distribution.
rm -rf "$OUTPUT/potion" "$OUTPUT/extensions" "$OUTPUT/documents"
cp -a "$DIST_STAGE/potion" "$OUTPUT/potion"
cp -a "$DIST_STAGE/extensions" "$OUTPUT/extensions"
cp -a "$DIST_STAGE/documents" "$OUTPUT/documents"

echo "Built Kindle runtimes: $BUILD_ABIS"
echo "Built Kindle bundle: $OUTPUT/potion"
echo "Built KUAL extension: $OUTPUT/extensions/Potion"
echo "Built Library launcher: $OUTPUT/documents/Potion.sh"
