// The Vector arithmetic. See include/isaec/api/vector.hpp for where the
// numbers come from: the ABI is recovered from the shipped binary, the
// arithmetic is the documented Isaac API reimplemented from scratch.

#include "isaec/api/vector.hpp"

#include <cmath>

namespace isaec::api {

Vector vector_add(const Vector& a, const Vector& b) noexcept { return Vector{a.x + b.x, a.y + b.y}; }

Vector vector_sub(const Vector& a, const Vector& b) noexcept { return Vector{a.x - b.x, a.y - b.y}; }

Vector vector_negate(const Vector& v) noexcept { return Vector{-v.x, -v.y}; }

Vector vector_scale(const Vector& v, f32 factor) noexcept { return Vector{v.x * factor, v.y * factor}; }

Vector vector_divide(const Vector& v, f32 divisor) noexcept {
    if (divisor == 0.0f) return Vector{0.0f, 0.0f};
    return Vector{v.x / divisor, v.y / divisor};
}

f32 vector_dot(const Vector& a, const Vector& b) noexcept { return a.x * b.x + a.y * b.y; }

f32 vector_cross(const Vector& a, const Vector& b) noexcept { return a.x * b.y - a.y * b.x; }

f32 vector_length_squared(const Vector& v) noexcept { return v.x * v.x + v.y * v.y; }

f32 vector_length(const Vector& v) noexcept { return std::sqrt(vector_length_squared(v)); }

f32 vector_distance_squared(const Vector& a, const Vector& b) noexcept {
    const Vector d = vector_sub(a, b);
    return vector_length_squared(d);
}

f32 vector_distance(const Vector& a, const Vector& b) noexcept { return std::sqrt(vector_distance_squared(a, b)); }

Vector vector_lerp(const Vector& a, const Vector& b, f32 t) noexcept {
    const Vector d = vector_sub(b, a);
    return vector_add(a, vector_scale(d, t));
}

Vector vector_normalized(const Vector& v) noexcept {
    const f32 len = vector_length(v);
    if (len == 0.0f) return Vector{0.0f, 0.0f};
    return Vector{v.x / len, v.y / len};
}

Vector vector_resized(const Vector& v, f32 factor) noexcept { return vector_scale(v, factor); }

Vector vector_rotated(const Vector& v, f32 degrees) noexcept {
    // The game works in degrees; the conversion is done here so the rest of
    // the code never has to think about it.
    const f64 radians = static_cast<f64>(degrees) * 3.14159265358979323846 / 180.0;
    const f32 c = static_cast<f32>(std::cos(radians));
    const f32 s = static_cast<f32>(std::sin(radians));
    return Vector{v.x * c - v.y * s, v.x * s + v.y * c};
}

f32 vector_angle_degrees(const Vector& v) noexcept {
    const f64 radians = std::atan2(static_cast<f64>(v.y), static_cast<f64>(v.x));
    f64 degrees = radians * 180.0 / 3.14159265358979323846;
    if (degrees <= -180.0) degrees += 360.0;
    return static_cast<f32>(degrees);
}

}  // namespace isaec::api
