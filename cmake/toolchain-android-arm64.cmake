# Android arm64 toolchain.
#
# Usage:
#   cmake -S . -B build-android \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android-arm64.cmake \
#     -DANDROID_NDK=$HOME/Android/Sdk/ndk/26.1.10909125
#   cmake --build build-android -j
#
# The NDK is read from the ANDROID_NDK environment variable if the -D argument is
# omitted. Nothing here is required for a host (desktop Linux) build.

if(NOT DEFINED ANDROID_NDK)
  if(DEFINED ENV{ANDROID_NDK})
    set(ANDROID_NDK "$ENV{ANDROID_NDK}")
  endif()
endif()

if(NOT ANDROID_NDK)
  message(FATAL_ERROR
    "ANDROID_NDK is not set. Pass -DANDROID_NDK=/path/to/ndk or export it.")
endif()

set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 24)

set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-24)
set(ANDROID_STL c++_static)
set(CMAKE_ANDROID_ARCH_ABI ${ANDROID_ABI})
set(CMAKE_ANDROID_NDK ${ANDROID_NDK})
set(CMAKE_ANDROID_STL_TYPE ${ANDROID_STL})

# The NDK names its compiler wrappers aarch64-linux-android<API>-clang, where
# <API> is the bare API level. ANDROID_PLATFORM is the string "android-24", so
# it cannot be concatenated directly into the binary name -- doing so yields
# "aarch64-linux-androidandroid-24-clang", which does not exist.
string(REGEX REPLACE "^android-" "" ANDROID_API_LEVEL "${ANDROID_PLATFORM}")

# Host tag for the NDK's own prebuilt tree. Hardcoding linux-x86_64 works on the
# CI runner but breaks on Apple silicon, where the NDK ships darwin-arm64.
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(ANDROID_NDK_HOST_TAG darwin-arm64)
else()
  set(ANDROID_NDK_HOST_TAG linux-x86_64)
endif()
set(ANDROID_NDK_TOOLCHAIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_TAG}")

set(CMAKE_C_COMPILER   "${ANDROID_NDK_TOOLCHAIN}/bin/aarch64-linux-android${ANDROID_API_LEVEL}-clang")
set(CMAKE_CXX_COMPILER "${ANDROID_NDK_TOOLCHAIN}/bin/aarch64-linux-android${ANDROID_API_LEVEL}-clang++")

# NDK ships the runtime libraries in per-ABI directories; the CMake Android
# support modules find them from these paths.
set(ANDROID_NDK_LLVM  "${ANDROID_NDK_TOOLCHAIN}")
set(ANDROID_NDK_ROOT  "${ANDROID_NDK}")

# Desktop-only dependencies are disabled here; a mobile build must not try to
# find system Lua, X11, or any other host library.
set(ISAEC_WITH_LUA OFF CACHE BOOL "" FORCE)

# The Android sysroot ships no libpng. Leaving this ON makes the REQUIRED
# pkg-config fallback resolve against the *host* packages: configure then
# succeeds and the link fails later with `ld.lld: unable to find library
# -lpng16`, which is exactly the failure CI hit. Measured, not predicted.
#
# Consequence of turning it off: nothing decodes images on Android. That is a
# real gap, not a workaround -- see issue #16. Until a cross-built libpng
# exists for arm64-v8a, an Android build cannot read the archives' PNGs, and
# this file makes that explicit rather than producing a binary that links a
# host library it cannot use.
set(ISAEC_WITH_PNG OFF CACHE BOOL "" FORCE)
