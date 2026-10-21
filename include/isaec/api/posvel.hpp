// The game's PosVel class.
//
// A PosVel is a position and a velocity, both Vectors. The original keeps
// the two at fixed offsets inside the game object, and the recovered surface
// says so: the property upvalues of Position and Velocity are 0 and 8.
//
// The recovered closure bodies (getter 0x008bde40, setter 0x008bdee0) copy
// exactly eight bytes -- two 32 bit floats -- to and from those offsets, and
// the getter hands out a *new* Vector userdata built from the copy. The
// setter takes a Vector and copies its payload back.
//
// Nothing here is invented: unlike the Vector arithmetic, moving a pair of
// vectors in and out of a struct needs no knowledge of the game's C++, so
// this class is a faithful port of the original behaviour.

#ifndef ISAEC_API_POSVEL_HPP
#define ISAEC_API_POSVEL_HPP

#include "isaec/api/vector.hpp"

namespace isaec::api {

// The offset of the position inside the game object (the upvalue 0).
inline constexpr usize kPosVelPositionOffset = 0;

// The offset of the velocity inside the game object (the upvalue 8).
inline constexpr usize kPosVelVelocityOffset = 8;

// The payload of a PosVel instance: the position and the velocity, two
// vectors of four bytes each.
inline constexpr usize kPosVelUserdataBytes = 16;

// A position and a velocity.
struct PosVel {
    Vector position;
    Vector velocity;
};

[[nodiscard]] constexpr bool operator==(const PosVel& a, const PosVel& b) noexcept {
    return a.position == b.position && a.velocity == b.velocity;
}

[[nodiscard]] constexpr bool operator!=(const PosVel& a, const PosVel& b) noexcept {
    return !(a == b);
}

}  // namespace isaec::api

#endif  // ISAEC_API_POSVEL_HPP
