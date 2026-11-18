// The game's Vector type.
//
// A Vector is the 2D float pair the game exposes to Lua as the class
// "Vector". The shipped binary stores it in a 16 byte userdata
// (lua_newuserdata(L, 0x10)); the payload itself is two 32 bit floats at
// offset 0, which is what this struct mirrors.
//
// Where the numbers come from
// ----------------------------
// Both the ABI and the arithmetic are recovered from the reference binary
// (v1.9.7.17.J460, sha256
// 3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b):
//
//   * The registration function at 0x00866960 binds each Lua method to a
//     native function pointer, which the closure invokes through the upvalue
//     at pseudo-index 0xfff0b9d7.
//   * The arithmetic below is a translation of those native functions
//     (0xa0fea0..0xa10660). The comments name the function each block came
//     from.
//
// The arithmetic metamethods are registered by the same function: `__sub`
// 0xa10480, `__mul` 0xa104e0, `__div` 0xa105a0, `__unm` 0xa10660, and
// `__tostring` 0x85e630. The four binary arithmetic closures accept only a
// Vector as the second operand (helper 0x8baa50 rejects numbers), so the
// recomp does not offer scalar `v * 2` / `v / 2`.
//
// The Lua-visible results were checked against the owned game running under
// Wine (see docs/binding-surface-findings.md, section 18). The oracle host was
// v1.9.7.12.J273 because that is the build the installed launcher runs; the
// functions used by these tests are byte-identical at their remapped
// addresses, with the exceptions noted on Lerp and on `__mul`/`__div`.
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

// Scalar multiplication. The `Vector * Vector` metamethod does not use this;
// it scales by the other vector's x component (see vector_multiply).
[[nodiscard]] Vector vector_scale(const Vector& v, f32 factor) noexcept;

// `Vector * Vector` (0xa104e0) and `Vector / Vector` (0xa105a0). Both use the
// second operand's x component for both components, not component-wise
// multiplication or division. The operand must be a Vector; the binary's
// shared closure rejects numbers.
[[nodiscard]] Vector vector_multiply(const Vector& a, const Vector& b) noexcept;
[[nodiscard]] Vector vector_divide(const Vector& a, const Vector& b) noexcept;

// -- products ---------------------------------------------------------------

// Both products are accumulated the way the native x87 code does: in double
// precision, then rounded once to float.
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

// Vector:Lerp. The native routine (0xa10210) mutates the receiver:
//
//     v += (goal - v) * t
//
// The fourth Lua argument is a clamp flag: when it is truthy the native code
// runs `t = min(max(t, 0), 1)` before the interpolation. The Repentogon build
// used as the oracle has a two-argument version of this routine, which is why
// the clamp flag is established by the reference binary's disassembly and not
// by the oracle.
void vector_lerp(Vector& v, const Vector& goal, f32 t, bool clamp) noexcept;

// The unit vector with the same direction, or `v` unchanged when it has no
// length to preserve. The native guard is `length > 0`, so a zero vector and a
// NaN vector are both copied through; `1/length` is the scale factor.
[[nodiscard]] Vector vector_normalized(const Vector& v) noexcept;

// `v` scaled to the given length, or `v` unchanged when it has no length.
[[nodiscard]] Vector vector_resized(const Vector& v, f32 factor) noexcept;

// `v` rotated counter-clockwise by `degrees`. The native code converts the
// angle with the float constant 0.017453292f, then evaluates cos/sin in
// double precision on that promoted float, then rounds both to float.
[[nodiscard]] Vector vector_rotated(const Vector& v, f32 degrees) noexcept;

// Vector.FromAngle: the unit vector at `degrees`, a class-table static rather
// than an instance method in the binary.
[[nodiscard]] Vector vector_from_angle(f32 degrees) noexcept;

// The direction of `v` in degrees, in the range (-180, 180]. The native code
// is `float(atan2(y, x)) * 57.29578f`; in particular there is no
// `-180 -> 180` remap.
[[nodiscard]] f32 vector_angle_degrees(const Vector& v) noexcept;

// -- clamping ---------------------------------------------------------------

// Vector:Clamp. The native routine (0xa103c0) clamps in place with
// `MINSS(MAXSS(value, min), max)` per component. Vector:Clamped
// (0xa103f0) does the same into a fresh vector.
void vector_clamp(Vector& v, f32 min_x, f32 min_y, f32 max_x, f32 max_y) noexcept;
[[nodiscard]] Vector vector_clamped(const Vector& v, f32 min_x, f32 min_y, f32 max_x,
                                    f32 max_y) noexcept;

}  // namespace isaec::api

#endif  // ISAEC_API_VECTOR_HPP
