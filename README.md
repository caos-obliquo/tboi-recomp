# isaac-engine

A clean-room, OS-agnostic engine for *The Binding of Isaac*. Written from the
public interface, not from Nicalis's code.

**Status: scaffold.** Nothing here plays the game yet. See *Where this actually
stands* before you get excited.

## Why this exists

Isaac ships as a 32-bit Windows PE linked against DirectX 9 and Win32 audio. It
cannot run on Android, and it will never run there. What *can* be rebuilt is the
part of the game that is genuinely portable: a data reader, a Lua VM, and the
native functions the shipped Lua calls into.

That last part is the whole game. The binary registers 655 native methods across
9 classes (`Entity` 261, `Input` 118, `Isaac` 76, `HUD` 42, `Sprite` 33,
`Vector` 22, `KColor` 18, `Font` 8, `EntityPlayer` 77). They were recovered by
reading the shipped binary's binding tables. Implementing those 655 functions is
roughly 90% of the work; rendering and input are solved problems.

## Design

One source tree, two targets:

| Target | Toolchain file |
|---|---|
| linux-x86_64 | host default |
| arm64-android | `cmake/toolchain-android-arm64.cmake` |

Data is **never** shipped. The engine is pointed at a copy the user already owns,
the way OpenMW points at Morrowind and OpenTTD points at Transport Tycoon. No
Nicalis Lua source, sprites, audio, video, or bytecode is in this repository.

Namespace is `isaec`, not `isaac` — deliberately, so nothing here reads as an
official Nicalis artifact.

### The binding model

Isaac does not use `lua_register`. It uses a custom metatable model, and the
recovered keys are the reason this engine's binding core looks the way it does:

```
__class __parent __index __propget __propset __newindex __const __metatable __type
```

Inheritance is single-parent through `__parent`. A property registered without a
setter is additionally mirrored into `__const`, so it reads as read-only from
Lua. `include/isaec/api/binding.hpp` implements exactly this model. Do not
"simplify" it into a `lua_register` table — that is a different API.

The header is deliberately free of any Lua include so that callers can register
classes without Lua on their include path. Only `binding.cpp` includes Lua.

## Layout

```
include/isaec/
  api/binding.hpp          class registry, inheritance, property model
  api/registry.hpp         declarative ApiSpec -> Binding, missing-symbol table
  api/generated/           codegen output, do not edit
  data/source.hpp          DataSource: one interface over directories
  data/collection.hpp      ordered string-keyed collection
  engine/engine.hpp        mount / start / tick / stop
  engine/renderer.hpp      Renderer: pure virtual, platform fills it in
  engine/clock.hpp         SteadyClock
  platform/                null_renderer (headless)
src/                       implementations
tools/gen-api-spec.py      regenerates the binding spec from recovered data
bindings/                  isaac-api-spec.json, the checked-in source of truth
tests/                     16 cases, ctest
third_party/lua-5.3.6/     vendored, checksum recorded in third_party/CMakeLists.txt
```

Lua is pinned to **5.3** and vendored. Isaac's interpreter is Lua 5.3; 5.4,
5.5, and LuaJIT are all wrong. Do not switch this to `find_package(Lua)`.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build
```

Regenerate the binding spec after changing the recovered method list:

```sh
python3 tools/gen-api-spec.py
```

It is idempotent, and writes both the JSON and a C++ header, so a malformed
method name is caught at compile time rather than at load time.

## Where this actually stands

Done:

- Class registry with the recovered metatable model, single-parent inheritance,
  read-only property mirroring, cycle-safe lineage (16 tests, all green).
- Declarative `ApiSpec` -> `Binding` codegen path.
- The recovered 655-method surface registers, resolves, and inherits correctly.
- Directory-backed data source and a headless renderer.

Not done:

- **The 655 function bodies.** Every one is a `not_implemented_trampoline`.
  Signatures (`argc`, return shape) are not recovered; they need a disassembly
  pass over the registration table in the shipped binary.
- **The packed archive format.** Isaac's `.a` files begin `ARCH000\0` or
  `ARCH000\1` and carry no plaintext filenames. The runtime loader has been
  located only as far as a generic stdio file wrapper whose member selects the
  read strategy — the archive reader itself has not been found. Note that the
  manifest baked into the binary is build-pipeline residue and is never read at
  runtime, so it is not a usable shortcut.
- **Nothing renders.** `NullRenderer` counts calls and draws nothing.
- Only 9 of the ~23 classes Nicalis exposes have had their method tables
  extracted, and the `HUD` and `Isaac` buckets still contain some animation and
  table-field names that were not filtered out.

## Licence

MIT. See `LICENSE`.

This is an independent reimplementation. It contains no code, data, or text from
Nicalis or Edmund McMillen. Method *names* are facts about an interface and were
recovered by reading the user's own copy of the game; every implementation is
written from scratch here.
</content>
</</invoke>