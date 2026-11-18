// The game's RNG class.
//
// The shipped binary exposes an "RNG" userdata with five methods and a
// callable class table. The ABI and the arithmetic below are translated from
// the reference binary (v1.9.7.17.J460, sha256
// 3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b):
//
//   * the registrar at 0x008a6d90 creates the class and the closures;
//   * the method table lives in the shared registration function at
//     0x00866960, around 0x00867742;
//   * the native routines are 0x0040c3a0 (GetSeed), 0x007e8fe0 (SetSeed),
//     0x007e9020 (RandomInt), 0x007e9080 (RandomFloat) and 0x007e90f0
//     (Next).
//
// The payload is 16 bytes: a 32 bit seed followed by three 32 bit shift
// counts. The shift counts are selected by SetSeed from a table of 81 rows at
// 0x00b1f4c8; the constructor uses row 35, {5, 9, 7}, with the seed
// 0xaa17414f. Constructor arguments are ignored by the binary.
//
// Nothing here includes Lua: the state and the arithmetic are usable, and
// testable, without an interpreter.

#ifndef ISAEC_API_RNG_HPP
#define ISAEC_API_RNG_HPP

#include "isaec/platform/types.hpp"

namespace isaec::api {

// The size of the userdata the original allocates for an RNG. The payload is
// 16 bytes; the remaining eight bytes are the luabridge wrapper.
inline constexpr usize kRngUserdataBytes = 16;

// The seed the binary's constructor installs, and the shift row it selects.
inline constexpr u32 kRngDefaultSeed = 0xaa17414fu;
inline constexpr usize kRngDefaultShiftRow = 35;

// Number of rows in the shift table at 0x00b1f4c8. SetSeed clamps a larger
// index to the last row.
inline constexpr u32 kRngShiftRowCount = 81;

// The payload. `seed` is the current state; `shifts` are the three counts
// applied by the xorshift step, in the order the native code applies them.
struct Rng {
    u32 seed = 0;
    u32 shifts[3] = {0, 0, 0};
};

[[nodiscard]] constexpr bool operator==(const Rng& a, const Rng& b) noexcept {
    return a.seed == b.seed && a.shifts[0] == b.shifts[0] && a.shifts[1] == b.shifts[1] &&
           a.shifts[2] == b.shifts[2];
}

[[nodiscard]] constexpr bool operator!=(const Rng& a, const Rng& b) noexcept {
    return !(a == b);
}

// The state the constructor produces: seed 0xaa17414f and shift row 35.
[[nodiscard]] Rng rng_default() noexcept;

// Select the shifts from `shift_index`, clamped to the last row, and set the
// seed. This is the native SetSeed, including its in-place mutation.
void rng_set_seed(Rng& rng, u32 seed, u32 shift_index) noexcept;

// The current seed. This is the native GetSeed.
[[nodiscard]] u32 rng_get_seed(const Rng& rng) noexcept;

// Advance the state once and return the new seed. This is the native Next.
//
// The shipped code prints "RNG Seed is zero!" and executes INT3 when the seed
// is zero. A recompilation must not abort the process, so a zero seed here is
// left untouched and zero is returned. That is the one behaviour deliberately
// not identical to the binary; it is unobservable in the original because the
// original never returns.
[[nodiscard]] u32 rng_next(Rng& rng) noexcept;

// Advance once and return a value in [0, bound). A zero bound returns zero
// after the advance, exactly as the native code does.
[[nodiscard]] u32 rng_random_int(Rng& rng, u32 bound) noexcept;

// Advance once and return a float in [0, 1). The native code converts the new
// seed to an unsigned double, rounds it to float, and multiplies by the float
// constant at 0x00ba9ff0 (0x2f7ffffe, 2.3283062e-10f).
[[nodiscard]] f32 rng_random_float(Rng& rng) noexcept;

}  // namespace isaec::api

#endif  // ISAEC_API_RNG_HPP
