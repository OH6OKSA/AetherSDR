#!/usr/bin/env bash
# Builds static FFTW (double and float) for one Android ABI with the NDK.
# WDSP and NR2 need double precision; NR4 (libspecbleach) needs float.
#   mobile/tools/build-fftw-android.sh ABI PREFIX
# ABI: arm64-v8a or x86_64. Needs ANDROID_NDK_ROOT.
set -euo pipefail
ABI="${1:?ABI (arm64-v8a or x86_64)}"
PREFIX="${2:?install prefix}"
FFTW_VERSION="${FFTW_VERSION:-3.3.10}"
FFTW_SHA256="${FFTW_SHA256:-56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467}"
API="${ANDROID_API:-28}"
: "${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
curl -fsSL -o "$work/fftw.tar.gz" "https://www.fftw.org/fftw-${FFTW_VERSION}.tar.gz"
echo "${FFTW_SHA256}  $work/fftw.tar.gz" | sha256sum -c -
tar xzf "$work/fftw.tar.gz" -C "$work"
for prec in double float; do
    extra=""
    if [ "$prec" = float ]; then
        extra="-DENABLE_FLOAT=ON"
        if [ "$ABI" = arm64-v8a ]; then extra="$extra -DENABLE_NEON=ON"; fi
    fi
    # shellcheck disable=SC2086
    cmake -S "$work/fftw-${FFTW_VERSION}" -B "$work/build-$prec" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" $extra
    cmake --build "$work/build-$prec" --parallel
    cmake --install "$work/build-$prec"
done
ls -la "$PREFIX/lib"
