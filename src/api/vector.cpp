// The Vector arithmetic. See include/isaec/api/vector.hpp for where the
// numbers come from: every function below is a translation of the native
// routine named in its comment, recovered from the reference binary.

#include "isaec/api/vector.hpp"

#include <cmath>

namespace isaec::api {
namespace {

// The binary's square-root helper at 0x00435a50 promotes the float to double,
// calls the CRT sqrt and rounds the result back to float. Doing the same keeps
// the last bit identical to the game's output.
f32 sqrt_float(f32 value) noexcept {
    return static_cast<f32>(std::sqrt(static_cast<f64>(value)));
}

constexpr f32 kDegreesToRadians = 0.017453292f;
constexpr f32 kRadiansToDegrees = 57.29578f;

// The SSE scalar MAXSS/MINSS semantics: the second operand wins when the
// comparison is unordered, which is how a NaN is handled by the native code.
f32 maxss(f32 value, f32 other) noexcept { return value > other ? value : other; }
f32 minss(f32 value, f32 other) noexcept { return value < other ? value : other; }

}  // namespace

// 0xa10420, reached through the Vector __add metamethod.
Vector vector_add(const Vector& a, const Vector& b) noexcept { return Vector{a.x + b.x, a.y + b.y}; }

Vector vector_sub(const Vector& a, const Vector& b) noexcept { return Vector{a.x - b.x, a.y - b.y}; }

Vector vector_negate(const Vector& v) noexcept { return Vector{-v.x, -v.y}; }

Vector vector_scale(const Vector& v, f32 factor) noexcept { return Vector{v.x * factor, v.y * factor}; }

Vector vector_multiply(const Vector& a, const Vector& b) noexcept {
    return Vector{a.x * b.x, a.y * b.y};
}

Vector vector_divide(const Vector& a, const Vector& b) noexcept {
    return Vector{a.x / b.x, a.y / b.y};
}

Vector vector_scale_divide(const Vector& v, f32 divisor) noexcept {
    if (divisor == 0.0f) return Vector{0.0f, 0.0f};
    return Vector{v.x / divisor, v.y / divisor};
}

// 0xa10180. The native code loads both products into the x87 stack, adds them
// there and returns the extended result, so the single rounding happens on the
// way out. Double accumulation reproduces that rounding.
f32 vector_dot(const Vector& a, const Vector& b) noexcept {
    return static_cast<f32>(static_cast<f64>(a.x) * static_cast<f64>(b.x) +
                            static_cast<f64>(a.y) * static_cast<f64>(b.y));
}

// 0xa101a0: receiver.x * other.y - receiver.y * other.x.
f32 vector_cross(const Vector& a, const Vector& b) noexcept {
    return static_cast<f32>(static_cast<f64>(a.x) * static_cast<f64>(b.y) -
                            static_cast<f64>(a.y) * static_cast<f64>(b.x));
}

// 0xa0fed0.
f32 vector_length_squared(const Vector& v) noexcept { return v.x * v.x + v.y * v.y; }

// 0xa0fea0.
f32 vector_length(const Vector& v) noexcept { return sqrt_float(vector_length_squared(v)); }

// 0xa0ff90. The native code subtracts the receiver from the other vector, so
// the sign of a zero component matches the original.
f32 vector_distance_squared(const Vector& a, const Vector& b) noexcept {
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    return dx * dx + dy * dy;
}

// 0xa0ff30.
f32 vector_distance(const Vector& a, const Vector& b) noexcept {
    return sqrt_float(vector_distance_squared(a, b));
}

// 0xa10210. `t` is clamped only when the caller asked for it; MAXSS/MINSS
// turn a NaN `t` into zero, exactly as the native instructions do.
void vector_lerp(Vector& v, const Vector& goal, f32 t, bool clamp) noexcept {
    if (clamp) {
        t = maxss(t, 0.0f);
        t = minss(t, 1.0f);
    }
    v.x = v.x + (goal.x - v.x) * t;
    v.y = v.y + (goal.y - v.y) * t;
}

// 0xa10030. The copy is made first, so a zero-length vector is returned
// unchanged rather than turned into NaNs.
Vector vector_normalized(const Vector& v) noexcept {
    Vector result = v;
    const f32 len = vector_length(v);
    if (len > 0.0f) {
        const f32 scale = 1.0f / len;
        result.x *= scale;
        result.y *= scale;
    }
    return result;
}

// 0xa10100.
Vector vector_resized(const Vector& v, f32 factor) noexcept {
    Vector result = v;
    const f32 len = vector_length(v);
    if (len > 0.0f) {
        const f32 scale = factor / len;
        result.x *= scale;
        result.y *= scale;
    }
    return result;
}

// 0xa102e0. The angle is rounded to float before the conversion, and the CRT
// trigonometric functions then work in double precision on that value.
Vector vector_rotated(const Vector& v, f32 degrees) noexcept {
    const f32 radians = degrees * kDegreesToRadians;
    const f32 c = static_cast<f32>(std::cos(static_cast<f64>(radians)));
    const f32 s = static_cast<f32>(std::sin(static_cast<f64>(radians)));
    return Vector{v.x * c - v.y * s, v.y * c + v.x * s};
}

// 0xa10350, exposed as the class-table function Vector.FromAngle.
Vector vector_from_angle(f32 degrees) noexcept {
    const f32 radians = degrees * kDegreesToRadians;
    return Vector{static_cast<f32>(std::cos(static_cast<f64>(radians))),
                  static_cast<f32>(std::sin(static_cast<f64>(radians)))};
}

// 0xa10390. atan2 is evaluated in double precision, rounded to float, then
// multiplied by the float constant for 180/pi.
f32 vector_angle_degrees(const Vector& v) noexcept {
    const f32 radians = static_cast<f32>(std::atan2(static_cast<f64>(v.y), static_cast<f64>(v.x)));
    return radians * kRadiansToDegrees;
}

// 0xa103c0: MINSS(MAXSS(value, min), max) per component, including the
// unordered (NaN) case where the second operand wins.
void vector_clamp(Vector& v, f32 min_x, f32 min_y, f32 max_x, f32 max_y) noexcept {
    v.x = minss(maxss(v.x, min_x), max_x);
    v.y = minss(maxss(v.y, min_y), max_y);
}

// 0xa103f0.
Vector vector_clamped(const Vector& v, f32 min_x, f32 min_y, f32 max_x, f32 max_y) noexcept {
    Vector result = v;
    vector_clamp(result, min_x, min_y, max_x, max_y);
    return result;
}

}  // namespace isaec::api
