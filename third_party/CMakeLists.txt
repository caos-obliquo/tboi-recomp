# Lua 5.3.6, vendored.
#
# The game ships lua51.dll plus Lua5.3.3f.dll, and its scripts use 5.3 language
# features (integer division, bitwise operators, integer subtype). Binding
# against the host's 5.4 or 5.5 changes semantics in ways a mod author would
# never expect, so the interpreter version is pinned here rather than discovered.
#
# Provenance:
#   https://www.lua.org/ftp/lua-5.3.6.tar.gz
#   sha256 fc5fd69bb8736323f026672b1b7235da613d7177e72558893a0bdcd320466d60
#
# Lua is MIT licensed; see lua-5.3.6/doc/readme.html.

set(ISAEC_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lua-5.3.6)

add_library(isaec_lua_interp STATIC
    ${ISAEC_LUA_DIR}/src/lapi.c
    ${ISAEC_LUA_DIR}/src/lcode.c
    ${ISAEC_LUA_DIR}/src/lctype.c
    ${ISAEC_LUA_DIR}/src/ldebug.c
    ${ISAEC_LUA_DIR}/src/ldo.c
    ${ISAEC_LUA_DIR}/src/ldump.c
    ${ISAEC_LUA_DIR}/src/lfunc.c
    ${ISAEC_LUA_DIR}/src/lgc.c
    ${ISAEC_LUA_DIR}/src/llex.c
    ${ISAEC_LUA_DIR}/src/lmem.c
    ${ISAEC_LUA_DIR}/src/lobject.c
    ${ISAEC_LUA_DIR}/src/lopcodes.c
    ${ISAEC_LUA_DIR}/src/lparser.c
    ${ISAEC_LUA_DIR}/src/lstate.c
    ${ISAEC_LUA_DIR}/src/lstring.c
    ${ISAEC_LUA_DIR}/src/ltable.c
    ${ISAEC_LUA_DIR}/src/ltm.c
    ${ISAEC_LUA_DIR}/src/lundump.c
    ${ISAEC_LUA_DIR}/src/lvm.c
    ${ISAEC_LUA_DIR}/src/lzio.c
    ${ISAEC_LUA_DIR}/src/lauxlib.c
    ${ISAEC_LUA_DIR}/src/lbaselib.c
    ${ISAEC_LUA_DIR}/src/lbitlib.c
    ${ISAEC_LUA_DIR}/src/lcorolib.c
    ${ISAEC_LUA_DIR}/src/ldblib.c
    ${ISAEC_LUA_DIR}/src/liolib.c
    ${ISAEC_LUA_DIR}/src/lmathlib.c
    ${ISAEC_LUA_DIR}/src/loadlib.c
    ${ISAEC_LUA_DIR}/src/loslib.c
    ${ISAEC_LUA_DIR}/src/lstrlib.c
    ${ISAEC_LUA_DIR}/src/ltablib.c
    ${ISAEC_LUA_DIR}/src/lutf8lib.c
    ${ISAEC_LUA_DIR}/src/linit.c
)

target_include_directories(isaec_lua_interp PUBLIC ${ISAEC_LUA_DIR}/src)
set_target_properties(isaec_lua_interp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
)
