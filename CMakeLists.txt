cmake_minimum_required(VERSION 3.16)
project(isaec_engine VERSION 0.1.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ISAEC_BUILD_TESTS "Build the test suite" ON)
option(ISAEC_WITH_LUA "Build the Lua binding layer when Lua 5.3 is available" ON)
option(ISAEC_WITH_WINDOW "Build the SDL2 windowed renderer when SDL2 is available" ON)
option(ISAEC_WITH_PNG "Build the PNG decoder when libpng is available" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Lua 5.3
#
# Pinned, not discovered. The host may carry 5.4, 5.5 or LuaJIT, and each
# differs from 5.3 in ways that change how a mod script behaves. The game ships
# Lua 5.3, so third_party/ carries the same interpreter and links statically.
# ---------------------------------------------------------------------------
set(ISAEC_LUA_FOUND OFF)
if(ISAEC_WITH_LUA)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.3.6/src/lua.h)
    add_subdirectory(third_party)
    set(ISAEC_LUA_FOUND ON)
    message(STATUS "isaec: using vendored Lua 5.3.6")
  else()
    message(STATUS "isaec: third_party/lua-5.3.6 missing, binding layer compiles as a stub")
  endif()
endif()

# ---------------------------------------------------------------------------
# Windowing and image dependencies
#
# libpng is a core dependency: the archives hand back encoded PNG bytes and
# isaec_core is what turns them into pixels, so the test suite needs it too and
# it links PUBLIC.
#
# SDL2 is not. It is only the first windowed backend, and the whole point of
# engine/renderer.hpp is that no graphics API is baked into the core, so a host
# without SDL2 still builds and still runs the headless binary and the tests.
# This host ships SDL2Config.cmake but no PNGConfig.cmake, hence the two
# different discovery calls.
# ---------------------------------------------------------------------------
# PNG is a hard dependency whenever it is enabled. It is NOT discovered
# opportunistically: the archives only carry PNG bytes, so a build without a
# decoder cannot read a single image, and silently producing one would hide that.
#
# ISAEC_WITH_PNG exists for the one measured case where the platform has no
# libpng at all -- cross-compiling to Android, whose sysroot ships none. The
# Android toolchain file turns it off explicitly rather than letting it fail in
# the middle of a link. Nothing is lost silently: decode_png() then always
# reports failure, and the tests that need it are not built.
set(ISAEC_PNG_FOUND OFF)
if(ISAEC_WITH_PNG)
  find_package(PNG QUIET)
  if(NOT PNG_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ISAEC_PNG REQUIRED IMPORTED_TARGET libpng16)
    set(ISAEC_PNG_TARGET PkgConfig::ISAEC_PNG)
    set(ISAEC_PNG_FOUND ON)
    message(STATUS "isaec: libpng via pkg-config ${ISAEC_PNG_VERSION}")
  else()
    set(ISAEC_PNG_TARGET PNG::PNG)
    set(ISAEC_PNG_FOUND ON)
    message(STATUS "isaec: libpng via find_package ${PNG_VERSION}")
  endif()
else()
  message(STATUS "isaec: ISAEC_WITH_PNG=OFF, decode_png() will always report failure")
endif()

set(ISAEC_SDL_FOUND OFF)
if(ISAEC_WITH_WINDOW)
  find_package(SDL2 CONFIG QUIET)
  if(SDL2_FOUND)
    set(ISAEC_SDL_FOUND ON)
    message(STATUS "isaec: SDL2 ${SDL2_VERSION} via SDL2Config.cmake")
  else()
    message(STATUS "isaec: SDL2 not found, isaec_window will not be built")
  endif()
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
set(ISAEC_IMAGE_SRC src/data/image_stub.cpp)
if(ISAEC_PNG_FOUND)
  set(ISAEC_IMAGE_SRC src/data/image.cpp)
endif()

add_library(isaec_core
  src/engine/engine.cpp
  src/engine/clock.cpp
  src/data/source.cpp
  src/data/collection.cpp
  src/data/codec.cpp
  ${ISAEC_IMAGE_SRC}
  src/data/archive.cpp
)

target_include_directories(isaec_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(ISAEC_PNG_FOUND)
  target_link_libraries(isaec_core PUBLIC ${ISAEC_PNG_TARGET})
endif()

target_compile_options(isaec_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
)

set_source_files_properties(src/data/source.cpp
  PROPERTIES COMPILE_DEFINITIONS ISAEC_DATA_IO=1)

# ---------------------------------------------------------------------------
# Lua binding layer (separate target: it is the only thing that needs Lua)
# ---------------------------------------------------------------------------
add_library(isaec_lua STATIC
  src/api/binding.cpp
  src/api/registry.cpp
  src/api/lua_value.cpp
  src/api/vector.cpp
  src/api/vector_lua.cpp
  src/api/color.cpp
  src/api/color_lua.cpp
  src/api/posvel_lua.cpp
  src/api/field_class.cpp
  src/api/field_classes_generated.cpp
)
target_include_directories(isaec_lua PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(isaec_lua PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
)

if(ISAEC_LUA_FOUND)
  target_compile_definitions(isaec_lua PUBLIC ISAEC_HAS_LUA=1)
  target_link_libraries(isaec_lua PUBLIC isaec_lua_interp)
else()
  target_compile_definitions(isaec_lua PUBLIC ISAEC_HAS_LUA=0)
endif()

# ---------------------------------------------------------------------------
# Reference platform: headless null renderer + bootstrap
# ---------------------------------------------------------------------------
add_executable(isaec_headless
  src/platform/null_renderer.cpp
  src/platform/null_main.cpp
)
target_link_libraries(isaec_headless PRIVATE isaec_core isaec_lua)
target_compile_options(isaec_headless PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
)

# ---------------------------------------------------------------------------
# Reference platform: windowed SDL2 backend
#
# Separate from isaec_headless rather than folded into it: the headless binary
# must keep working on a host with no display and no SDL2, which is how CI and
# the test run are meant to stay simple.
# ---------------------------------------------------------------------------
if(ISAEC_SDL_FOUND)
  add_executable(isaec_window
    src/platform/sdl_renderer.cpp
    src/platform/sdl_main.cpp
  )
  target_link_libraries(isaec_window PRIVATE isaec_core SDL2::SDL2)
  target_compile_options(isaec_window PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
  )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ISAEC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS isaec_core isaec_lua
        ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
