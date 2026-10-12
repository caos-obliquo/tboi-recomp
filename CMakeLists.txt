cmake_minimum_required(VERSION 3.16)
project(isaec_engine VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ISAEC_BUILD_TESTS "Build the test suite" ON)
option(ISAEC_WITH_LUA "Build the Lua binding layer when Lua 5.3 is available" ON)

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
# Core library
# ---------------------------------------------------------------------------
add_library(isaec_core
  src/engine/engine.cpp
  src/engine/clock.cpp
  src/data/source.cpp
  src/data/collection.cpp
)

target_include_directories(isaec_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

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
# Tests
# ---------------------------------------------------------------------------
if(ISAEC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS isaec_core isaec_lua
        ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
