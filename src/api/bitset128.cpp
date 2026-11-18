// The BitSet128 arithmetic, translated from the reference binary. See
// include/isaec/api/bitset128.hpp for the native addresses and the boundary
// rules.

#include "isaec/api/bitset128.hpp"

#include <cstdio>

namespace isaec::api {

bool bitset128_get(const BitSet128& set, i32 index) noexcept {
    // The native masks the index rather than shifting by it, and both the
    // negative and the >= 128 paths end in a zero mask.
    if (index < 0 || index >= 128) return false;
    const u32 word = static_cast<u32>(index) >> 5;
    const u32 bit = static_cast<u32>(index) & 31u;
    return (set.words[word] & (1u << bit)) != 0;
}

void bitset128_set(BitSet128& set, i32 index, bool value) noexcept {
    // The native's set helper and its inline clear both produce a zero mask
    // for an out-of-range index, so the four words stay unchanged.
    if (index < 0 || index >= 128) return;
    const u32 word = static_cast<u32>(index) >> 5;
    const u32 bit = static_cast<u32>(index) & 31u;
    if (value) {
        set.words[word] |= 1u << bit;
    } else {
        set.words[word] &= ~(1u << bit);
    }
}

BitSet128 bitset128_bit_and(const BitSet128& a, const BitSet128& b) noexcept {
    BitSet128 result;
    for (usize i = 0; i < 4; ++i) result.words[i] = a.words[i] & b.words[i];
    return result;
}

BitSet128 bitset128_bit_or(const BitSet128& a, const BitSet128& b) noexcept {
    BitSet128 result;
    for (usize i = 0; i < 4; ++i) result.words[i] = a.words[i] | b.words[i];
    return result;
}

BitSet128 bitset128_bit_xor(const BitSet128& a, const BitSet128& b) noexcept {
    BitSet128 result;
    for (usize i = 0; i < 4; ++i) result.words[i] = a.words[i] ^ b.words[i];
    return result;
}

BitSet128 bitset128_bit_not(const BitSet128& set) noexcept {
    BitSet128 result;
    for (usize i = 0; i < 4; ++i) result.words[i] = ~set.words[i];
    return result;
}

std::string bitset128_to_string(const BitSet128& set) {
    // The two 64 bit halves the native prints as `%016llx`: word[2..3] first,
    // word[0..1] second, both little-endian within their half.
    const u64 low = static_cast<u64>(set.words[0]) |
                    (static_cast<u64>(set.words[1]) << 32);
    const u64 high = static_cast<u64>(set.words[2]) |
                     (static_cast<u64>(set.words[3]) << 32);

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "BitSet128: %016llx%016llx",
                  static_cast<unsigned long long>(high),
                  static_cast<unsigned long long>(low));
    return std::string(buffer);
}

}  // namespace isaec::api
