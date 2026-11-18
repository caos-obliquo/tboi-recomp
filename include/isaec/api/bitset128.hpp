// The game's BitSet128 type.
//
// The shipped binary stores a BitSet128 in a 16 byte payload of four 32 bit
// words; the Lua userdata around it is 24 bytes. The class is registered by
// the giant function at 0x00866960 (its block starts at 0x008679c2) and its
// methods and metamethods are the native routines named below.
//
// Where the numbers come from
// ---------------------------
// Reference binary: v1.9.7.17.J460, sha256
// 3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b.
//
//   * Get 0x0085e560 and Set 0x0085e5c0 index the four words in order: bits
//     0..31 word 0, 32..63 word 1, 64..95 word 2, 96..127 word 3, using
//     `index & 31` as the bit. Get returns false for a negative index and for
//     an index of 128 or more; Set is a no-op for both. Neither reads or
//     writes outside the 16 byte payload.
//   * __eq 0x0085e530 compares all four words.
//   * __band 0x0085e4d0, __bor 0x0085e4f0 and __bxor 0x0085e510 call the
//     four-word helpers 0x00417170, 0x00435c20 and 0x0085dae0. They build a
//     new value; the receiver is not modified.
//   * __bnot 0x00592200 writes the complement of each word into a new value;
//     it does not modify the receiver.
//   * __tostring 0x0085e490 formats
//     `sprintf(buf, 0x100, "BitSet128: %016llx%016llx", word[2], word[3],
//     word[0], word[1])`: the high 64 bits come first, then the low 64 bits,
//     each as 16 hexadecimal digits.
//   * the callable constructor's native 0x008bc330 reads two Lua integers,
//     `low` then `high`, and stores them as two little-endian 64 bit halves.
//
// Nothing in this header includes Lua: the value and the arithmetic are
// usable, and testable, without an interpreter.

#ifndef ISAEC_API_BITSET128_HPP
#define ISAEC_API_BITSET128_HPP

#include <string>

#include "isaec/platform/types.hpp"

namespace isaec::api {

// The size of the payload the original keeps in the userdata.
inline constexpr usize kBitSet128Bytes = 16;

// A 128 bit set. `words[0]` and `words[1]` are the low 64 bits (the Lua
// property `l`), `words[2]` and `words[3]` are the high 64 bits (`h`).
struct BitSet128 {
    u32 words[4] = {0, 0, 0, 0};
};

[[nodiscard]] inline bool operator==(const BitSet128& a, const BitSet128& b) noexcept {
    return a.words[0] == b.words[0] && a.words[1] == b.words[1] &&
           a.words[2] == b.words[2] && a.words[3] == b.words[3];
}

[[nodiscard]] inline bool operator!=(const BitSet128& a, const BitSet128& b) noexcept {
    return !(a == b);
}

// Get 0x0085e560. A negative index and an index of 128 or more are false.
[[nodiscard]] bool bitset128_get(const BitSet128& set, i32 index) noexcept;

// Set 0x0085e5c0. A negative index and an index of 128 or more are ignored.
void bitset128_set(BitSet128& set, i32 index, bool value) noexcept;

// The bitwise metamethod bodies. Each returns a new value and leaves both
// operands untouched.
[[nodiscard]] BitSet128 bitset128_bit_and(const BitSet128& a, const BitSet128& b) noexcept;
[[nodiscard]] BitSet128 bitset128_bit_or(const BitSet128& a, const BitSet128& b) noexcept;
[[nodiscard]] BitSet128 bitset128_bit_xor(const BitSet128& a, const BitSet128& b) noexcept;
[[nodiscard]] BitSet128 bitset128_bit_not(const BitSet128& set) noexcept;

// __tostring 0x0085e490, with the high half first.
[[nodiscard]] std::string bitset128_to_string(const BitSet128& set);

}  // namespace isaec::api

#endif  // ISAEC_API_BITSET128_HPP
