// The field layouts of the game classes whose members are plain fields.
//
// The tables come from tools/probe_field_layout.py, which reads the offset
// out of the property's upvalue and the type out of the getter body. The
// numbers are therefore measurements of the shipped binary, not guesses.
//
// The classes listed here are the ones whose *whole* member set is a plain
// field, plus the classes whose remaining members are objects (a pointer
// into the game's heap, which the port cannot reproduce). The object members
// keep the loud stub of the generated spec: they are simply not listed.
//
// The Repentogon .zhl layouts confirm the ones they cover: ActiveItemDesc
// (item 0, charge 8, battery 12, sub 16, timed 20, varData 28, size 0x20),
// KColor (red 0, green 4, blue 8, alpha 12, size 0x10) and GridEntityDesc
// (type 0, variant 4, state 8, spawnCount 12, spawnSeed 16, varData 20,
// initialized 24, variableSeed 28, size 0x20).

#ifndef ISAEC_API_FIELD_CLASSES_HPP
#define ISAEC_API_FIELD_CLASSES_HPP

#include "isaec/api/field_class.hpp"

namespace isaec::api {

class Binding;

// -- ActiveItemDesc ---------------------------------------------------------
// Size 0x20. The item id, the charges and the var data are integers, the
// partial charge is a float.
inline constexpr FieldSpec kActiveItemDescFields[] = {
    {"Item", 0, FieldType::Int},
    {"Charge", 8, FieldType::Int},
    {"BatteryCharge", 12, FieldType::Int},
    {"SubCharge", 16, FieldType::Int},
    {"TimedRechargeCooldown", 20, FieldType::Int},
    {"PartialCharge", 24, FieldType::Float},
    {"VarData", 28, FieldType::Int},
};
inline constexpr usize kActiveItemDescSize = 32;

// -- KColor -----------------------------------------------------------------
// Size 0x10. Four floats, the same shape as the Color class.
inline constexpr FieldSpec kKColorFields[] = {
    {"Red", 0, FieldType::Float},
    {"Green", 4, FieldType::Float},
    {"Blue", 8, FieldType::Float},
    {"Alpha", 12, FieldType::Float},
};
inline constexpr usize kKColorSize = 16;

// -- GridEntityDesc ---------------------------------------------------------
// Size 0x20. The "initialized" flag is a byte, everything else an integer.
inline constexpr FieldSpec kGridEntityDescFields[] = {
    {"Type", 0, FieldType::Int},
    {"Variant", 4, FieldType::Int},
    {"State", 8, FieldType::Int},
    {"SpawnCount", 12, FieldType::Int},
    {"SpawnSeed", 16, FieldType::Int},
    {"VarData", 20, FieldType::Int},
    {"Initialized", 24, FieldType::Bool},
    {"VariableSeed", 28, FieldType::Int},
};
inline constexpr usize kGridEntityDescSize = 32;

// -- TemporaryEffect --------------------------------------------------------
// Size 0x0c. The "Item" member is a pointer into the game's heap and is not
// listed; the count and the cooldown are integers.
inline constexpr FieldSpec kTemporaryEffectFields[] = {
    {"Count", 4, FieldType::Int},
    {"Cooldown", 8, FieldType::Int},
};
inline constexpr usize kTemporaryEffectSize = 12;

// -- QueueItemData ----------------------------------------------------------
// Size 0x09. The "Item" member is a pointer into the game's heap and is not
// listed; the charge is an integer and the touched flag a byte.
inline constexpr FieldSpec kQueueItemDataFields[] = {
    {"Charge", 4, FieldType::Int},
    {"Touched", 8, FieldType::Bool},
};
inline constexpr usize kQueueItemDataSize = 9;

// -- GridEntityPit ----------------------------------------------------------
// Size 0x155. Only the ladder flag is exposed.
inline constexpr FieldSpec kGridEntityPitFields[] = {
    {"HasLadder", 340, FieldType::Bool},
};
inline constexpr usize kGridEntityPitSize = 341;

// -- GridEntitySpikes -------------------------------------------------------
// Size 0x158. Only the timeout is exposed.
inline constexpr FieldSpec kGridEntitySpikesFields[] = {
    {"Timeout", 340, FieldType::Int},
};
inline constexpr usize kGridEntitySpikesSize = 344;

// -- GridEntityTNT ----------------------------------------------------------
// Size 0x158. Only the frame counter is exposed.
inline constexpr FieldSpec kGridEntityTNTFields[] = {
    {"FrameCnt", 340, FieldType::Int},
};
inline constexpr usize kGridEntityTNTSize = 344;

// -- Sprite -----------------------------------------------------------------
// Size 0x108. The offset, the scale and the colour are objects (pointers
// into the game's heap) and are not listed; the rotation and the playback
// speed are floats, the two flip flags are bytes.
inline constexpr FieldSpec kSpriteFields[] = {
    {"Rotation", 164, FieldType::Float},
    {"FlipX", 256, FieldType::Bool},
    {"FlipY", 257, FieldType::Bool},
    {"PlaybackSpeed", 260, FieldType::Float},
};
inline constexpr usize kSpriteSize = 264;

// Installs every field class above. Call it after the generated spec has
// been applied: the class tables have to exist before the properties are
// registered.
void install_field_classes(Binding& binding);

}  // namespace isaec::api

#endif  // ISAEC_API_FIELD_CLASSES_HPP
