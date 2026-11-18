// The RNG arithmetic, translated from the reference binary. See rng.hpp for
// the native addresses and for the one deliberate difference (a zero seed).

#include "isaec/api/rng.hpp"

namespace isaec::api {
namespace {

// The shift table at 0x00b1f4c8, 81 rows of three little-endian uint32.
// Row 35 is {5, 9, 7}, the constructor's row.
constexpr u32 kShiftTable[kRngShiftRowCount][3] = {
    {1, 3, 10},   {1, 5, 16},   {1, 5, 19},   {1, 9, 29},   {1, 11, 6},   {1, 11, 16},
    {1, 19, 3},   {1, 21, 20},  {1, 27, 27},  {2, 5, 15},   {2, 5, 21},   {2, 7, 7},
    {2, 7, 9},    {2, 7, 25},   {2, 9, 15},   {2, 15, 17},  {2, 15, 25},  {2, 21, 9},
    {3, 1, 14},   {3, 3, 26},   {3, 3, 28},   {3, 3, 29},   {3, 5, 20},   {3, 5, 22},
    {3, 5, 25},   {3, 7, 29},   {3, 13, 7},   {3, 23, 25},  {3, 25, 24},  {3, 27, 11},
    {4, 3, 17},   {4, 3, 27},   {4, 5, 15},   {5, 3, 21},   {5, 7, 22},   {5, 9, 7},
    {5, 9, 28},   {5, 9, 31},   {5, 13, 6},   {5, 15, 17},  {5, 17, 13},  {5, 21, 12},
    {5, 27, 8},   {5, 27, 21},  {5, 27, 25},  {5, 27, 28},  {6, 1, 11},   {6, 3, 17},
    {6, 17, 9},   {6, 21, 7},   {6, 21, 13},  {7, 1, 9},    {7, 1, 18},   {7, 1, 25},
    {7, 13, 25},  {7, 17, 21},  {7, 25, 12},  {7, 25, 20},  {8, 7, 23},   {8, 9, 23},
    {9, 5, 14},   {9, 5, 25},   {9, 11, 19},  {9, 21, 16},  {10, 9, 21},  {10, 9, 25},
    {11, 7, 12},  {11, 7, 16},  {11, 17, 13}, {11, 21, 13}, {12, 9, 23},  {13, 3, 17},
    {13, 3, 27},  {13, 5, 19},  {13, 17, 15}, {14, 1, 15},  {14, 13, 15}, {15, 1, 29},
    {17, 15, 20}, {17, 15, 23}, {17, 15, 26},
};

// The float at 0x00ba9ff0: 0x2f7ffffe. Written through its bit pattern so the
// multiply uses the same value as the binary rather than a decimal
// approximation that might round differently.
constexpr u32 kRandomFloatScaleBits = 0x2f7ffffe;

f32 random_float_scale() noexcept {
    union {
        u32 bits;
        f32 value;
    } converter{};
    converter.bits = kRandomFloatScaleBits;
    return converter.value;
}

}  // namespace

Rng rng_default() noexcept {
    Rng rng{};
    rng.seed = kRngDefaultSeed;
    rng.shifts[0] = kShiftTable[kRngDefaultShiftRow][0];
    rng.shifts[1] = kShiftTable[kRngDefaultShiftRow][1];
    rng.shifts[2] = kShiftTable[kRngDefaultShiftRow][2];
    return rng;
}

void rng_set_seed(Rng& rng, u32 seed, u32 shift_index) noexcept {
    rng.seed = seed;

    u32 row = shift_index;
    if (row > kRngShiftRowCount - 1) {
        row = kRngShiftRowCount - 1;
    }
    rng.shifts[0] = kShiftTable[row][0];
    rng.shifts[1] = kShiftTable[row][1];
    rng.shifts[2] = kShiftTable[row][2];
}

u32 rng_get_seed(const Rng& rng) noexcept { return rng.seed; }

u32 rng_next(Rng& rng) noexcept {
    u32 state = rng.seed;
    if (state == 0) {
        return 0;
    }

    state ^= state >> (rng.shifts[0] & 0x1fu);
    state ^= state << (rng.shifts[1] & 0x1fu);
    state ^= state >> (rng.shifts[2] & 0x1fu);
    rng.seed = state;
    return state;
}

u32 rng_random_int(Rng& rng, u32 bound) noexcept {
    const u32 value = rng_next(rng);
    if (bound == 0) {
        return 0;
    }
    return value % bound;
}

f32 rng_random_float(Rng& rng) noexcept {
    const u32 value = rng_next(rng);

    // The native code treats the seed as unsigned: cvtdq2pd on the signed
    // interpretation, then adds 2^32 when the sign bit is set.
    double number = static_cast<double>(static_cast<i32>(value));
    if ((value & 0x80000000u) != 0) {
        number += 4294967296.0;
    }
    const f32 rounded = static_cast<f32>(number);
    return rounded * random_float_scale();
}

}  // namespace isaec::api
