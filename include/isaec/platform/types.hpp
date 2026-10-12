// Fixed-width aliases and math constants.
//
// This header is included by every other public header in the project, so it
// must stay free of OS, graphics, and windowing includes. It has to compile
// unchanged on glibc/x86_64 and on Android/arm64.

#ifndef ISAEC_PLATFORM_TYPES_HPP
#define ISAEC_PLATFORM_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace isaec {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

inline constexpr f64 PI = 3.14159265358979323846;
inline constexpr f64 TAU = PI * 2.0;

// A borrowed, read-only view over bytes. Non-owning: the caller keeps the
// storage alive.
struct ByteView {
    const u8* data = nullptr;
    usize size = 0;

    constexpr bool empty() const noexcept { return size == 0; }
    constexpr explicit operator bool() const noexcept { return data != nullptr; }
};

}  // namespace isaec

#endif  // ISAEC_PLATFORM_TYPES_HPP
