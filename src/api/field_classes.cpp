// The installation of the field classes. See field_classes.hpp for the
// tables and where the numbers come from.

#include "isaec/api/field_classes.hpp"

#include "isaec/api/binding.hpp"

namespace isaec::api {
namespace {

template <usize N>
constexpr usize count_of(const FieldSpec (&)[N]) {
    return N;
}

}  // namespace

void install_field_classes(Binding& binding) {
    FieldClass::install(binding, "ActiveItemDesc", kActiveItemDescSize, kActiveItemDescFields,
                         count_of(kActiveItemDescFields));
    FieldClass::install(binding, "KColor", kKColorSize, kKColorFields, count_of(kKColorFields));
    FieldClass::install(binding, "GridEntityDesc", kGridEntityDescSize, kGridEntityDescFields,
                         count_of(kGridEntityDescFields));
    FieldClass::install(binding, "TemporaryEffect", kTemporaryEffectSize, kTemporaryEffectFields,
                         count_of(kTemporaryEffectFields));
    FieldClass::install(binding, "QueueItemData", kQueueItemDataSize, kQueueItemDataFields,
                         count_of(kQueueItemDataFields));
    FieldClass::install(binding, "GridEntityPit", kGridEntityPitSize, kGridEntityPitFields,
                         count_of(kGridEntityPitFields));
    FieldClass::install(binding, "GridEntitySpikes", kGridEntitySpikesSize, kGridEntitySpikesFields,
                         count_of(kGridEntitySpikesFields));
    FieldClass::install(binding, "GridEntityTNT", kGridEntityTNTSize, kGridEntityTNTFields,
                         count_of(kGridEntityTNTFields));
    FieldClass::install(binding, "Sprite", kSpriteSize, kSpriteFields, count_of(kSpriteFields));
}

}  // namespace isaec::api
