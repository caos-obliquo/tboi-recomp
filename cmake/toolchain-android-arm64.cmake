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

set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-24)
set(ANDROID_STL c++_static)
set(CMAKE_ANDROID_ARCH_ABI ${ANDROID_ABI})
set(CMAKE_ANDROID_NDK ${ANDROID_NDK})

# Prebuilt toolchain binaries shipped inside the NDK.
set(CMAKE_C_COMPILER   "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${ANDROID_PLATFORM}-clang")
set(CMAKE_CXX_COMPILER "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${ANDROID_PLATFORM}-clang++")

# NDK ships the runtime libraries in per-ABI directories; the CMake Android
# support modules find them from these paths.
set(ANDROID_NDK_LLVM  "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64")
set(ANDROID_NDK_TOOLCHAIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64")
set(ANDROID_NDK_ROOT  "${ANDROID_NDK}")

# Desktop-only dependencies are disabled here; a mobile build must not try to
# find system Lua, X11, or any other host library.
set(ISAEC_WITH_LUA OFF CACHE BOOL "" FORCE)
