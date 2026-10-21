// The game's Vector type.
//
// A Vector is the 2D float pair the game exposes to Lua as the class
// "Vector". The shipped binary stores it in a 16 byte userdata
// (lua_newuserdata(L, 0x10)); the payload itself is two 32 bit floats at
// offset 0, which is what this struct mirrors.
//
// Where the numbers come from
// ----------------------------
// The *shape* of the ABI is recovered from the binary (see
// docs/binding-surface-findings.md): the receiver is the first argument, the
// visible arguments start at index 2, a method returns 0 or 1 values, and a
// method that returns a new vector allocates a fresh userdata.
//
// The *math* is not. The shipped closures do not contain the arithmetic: they
// call the game's C++ Vector through a vtable pointer, so the numbers below
// are the documented Isaac API reimplemented from scratch, not a disassembly
// of the original. Every function here is therefore a claim about the public
// behaviour of the game, and a discrepancy against the shipped build would
// show up here first.
//
// Nothing in this header includes Lua: the value type and the arithmetic are
// usable (and testable) without an interpreter.

#ifndef ISAEC_API_VECTOR_HPP
#define ISAEC_API_VECTOR_HPP

#include "isaec/platform/types.hpp"

namespace isaec::api {

// The size of the userdata the original allocates for a Vector. The payload
// is only eight bytes; the rest of the allocation is the game's own padding,
// kept here so a ported binding allocates the same amount.
inline constexpr usize kVectorUserdataBytes = 16;

// A 2D vector of 32 bit floats.
struct Vector {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

[[nodiscard]] constexpr bool operator==(const Vector& a, const Vector& b) noexcept {
    return a.x == b.x && a.y == b.y;
}

[[nodiscard]] constexpr bool operator!=(const Vector& a, const Vector& b) noexcept {
    return !(a == b);
}

// -- arithmetic -------------------------------------------------------------

[[nodiscard]] Vector vector_add(const Vector& a, const Vector& b) noexcept;
[[nodiscard]] Vector vector_sub(const Vector& a, const Vector& b) noexcept;
[[nodiscard]] Vector vector_negate(const Vector& v) noexcept;

// Scalar multiplication and division. Dividing by zero yields a zero vector
// rather than an infinity, because the game's own vector type is a plain
// float pair and a NaN would silently poison every later computation.
[[nodiscard]] Vector vector_scale(const Vector& v, f32 factor) noexcept;
[[nodiscard]] Vector vector_divide(const Vector& v, f32 divisor) noexcept;

// -- products ---------------------------------------------------------------

[[nodiscard]] f32 vector_dot(const Vector& a, const Vector& b) noexcept;

// The z component of the 3D cross product of two 2D vectors. This is the
// value Isaac's Vector:Cross returns.
[[nodiscard]] f32 vector_cross(const Vector& a, const Vector& b) noexcept;

// -- lengths and distances --------------------------------------------------

[[nodiscard]] f32 vector_length_squared(const Vector& v) noexcept;
[[nodiscard]] f32 vector_length(const Vector& v) noexcept;
[[nodiscard]] f32 vector_distance_squared(const Vector& a, const Vector& b) noexcept;
[[nodiscard]] f32 vector_distance(const Vector& a, const Vector& b) noexcept;

// -- interpolation and reshaping -------------------------------------------

// Linear interpolation: a at t = 0, b at t = 1.
[[nodiscard]] Vector vector_lerp(const Vector& a, const Vector& b, f32 t) noexcept;

// The unit vector with the same direction, or the zero vector when `v` has no
// length to preserve.
[[nodiscard]] Vector vector_normalized(const Vector& v) noexcept;

// `v` scaled by `factor`.
[[nodiscard]] Vector vector_resized(const Vector& v, f32 factor) noexcept;

// `v` rotated counter-clockwise by `degrees`.
[[nodiscard]] Vector vector_rotated(const Vector& v, f32 degrees) noexcept;

// The direction of `v` in degrees, in the range (-180, 180].
[[nodiscard]] f32 vector_angle_degrees(const Vector& v) noexcept;

}  // namespace isaec::api

#endif  // ISAEC_API_VECTOR_HPP
