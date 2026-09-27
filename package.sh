#!/usr/bin/env bash
set -euo pipefail
ABI="${1:-arm64-v8a}"
BUILD="build-${ABI}"
cmake -S . -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME:-$ANDROID_NDK}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM=android-28 \
  -DANDROID_STL=c++_shared \
  -DMOD_ID=sound_physics_lite \
  -DMOD_NAME="Sound Physics Lite" \
  -DMOD_LIBRARY_NAME=SoundPhysicsLite
cmake --build "$BUILD" --target levi_package -j2
cp "$BUILD/SPL.levipack" ./SPL.levipack
