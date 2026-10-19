// Stub PNG decoder for builds configured without libpng.
//
// This exists so that a target with no PNG can still compile and link. The
// archives only carry PNG bytes, so on such a target nothing decodes: every
// call reports failure rather than pretending to succeed.
//
// It is selected by CMake when ISAEC_WITH_PNG is OFF. See the option's
// definition in the top-level CMakeLists.txt, which is the place to look for
// why a given build has or has not got PNG.
//
// This is not a fallback that silently degrades a host build. On the host,
// libpng is REQUIRED and this file is never compiled; the only supported way
// to reach it is to turn the option off on purpose.

#include "isaec/data/image.hpp"

#include <optional>

namespace isaec::data {

std::optional<Image> decode_png(const u8*, usize) {
    return std::nullopt;
}

}  // namespace isaec::data