// ANM2 animation documents, as shipped as XML inside the ARCH000 containers.
//
// The format is a plain XML dialect: an <AnimatedActor> root holding <Info>,
// <Content> (spritesheets, layers, nulls, events) and <Animations>, each
// <Animation> carrying a root timeline plus per-layer and per-null timelines
// and event triggers. Parsing is deliberately hand-rolled: the documents are
// machine-generated, there is no DTD, and pulling an XML library into the core
// for one dialect would be a much larger dependency than the grammar deserves.
//
// See docs/anm2-format.md for the observed schema and what this parser does not
// accept. Values are kept in the document's own units: positions and sizes in
// pixels, scales in percent (100 = unchanged), delays in frames at Info@Fps.

#ifndef ISAEC_DATA_ANM2_HPP
#define ISAEC_DATA_ANM2_HPP

#include <string>
#include <string_view>
#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::data {

// One keyframe. Root timelines use the pose fields; layer timelines add crop,
// pivot and size. Unused fields keep the defaults below.
struct Anm2Frame {
    float x = 0.0f;
    float y = 0.0f;
    float pivot_x = 0.0f;
    float pivot_y = 0.0f;
    float crop_x = 0.0f;
    float crop_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int delay = 1;
    float scale_x = 100.0f;
    float scale_y = 100.0f;
    float rotation = 0.0f;
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    u8 alpha = 255;
    i8 red_offset = 0;
    i8 green_offset = 0;
    i8 blue_offset = 0;
    bool visible = true;
    bool interpolated = false;
};

struct Anm2Spritesheet {
    int id = 0;
    std::string path;
};

struct Anm2Layer {
    int id = 0;
    std::string name;
    int spritesheet_id = 0;
};

struct Anm2Null {
    int id = 0;
    std::string name;
    bool show = true;
};

struct Anm2Event {
    int id = 0;
    std::string name;
};

struct Anm2LayerAnimation {
    int layer_id = 0;
    bool visible = true;
    std::vector<Anm2Frame> frames;
};

struct Anm2NullAnimation {
    int null_id = 0;
    std::vector<Anm2Frame> frames;
};

struct Anm2Trigger {
    int event_id = 0;
    int at_frame = 0;
};

struct Anm2Animation {
    std::string name;
    int frame_num = 1;
    bool loop = true;
    std::vector<Anm2Frame> root_frames;
    std::vector<Anm2LayerAnimation> layer_animations;
    std::vector<Anm2NullAnimation> null_animations;
    std::vector<Anm2Trigger> triggers;
};

struct Anm2 {
    int fps = 30;
    int version = 1;
    std::string default_animation;
    std::vector<Anm2Spritesheet> spritesheets;
    std::vector<Anm2Layer> layers;
    std::vector<Anm2Null> nulls;
    std::vector<Anm2Event> events;
    std::vector<Anm2Animation> animations;

    // Count of animation tracks whose layer/null/event id has no definition,
    // plus layers whose spritesheet is missing. Shipped documents contain such
    // dangling ids (older content carried into a newer archive), so this is
    // reported instead of rejected; a renderer skips the unknown track.
    usize dangling_references = 0;

    // Case-sensitive lookup, as the game uses the document's spelling. Returns
    // nullptr when the name is absent.
    const Anm2Animation* find(std::string_view name) const noexcept;
};

// Parses one document. Returns false and fills `error` with a message naming
// the offending construct; `out` is only meaningful on success. Structural
// errors are fatal. Dangling layer/null/event ids are counted in
// `dangling_references` rather than rejected, because shipped documents
// contain them and refusing the whole document would lose the rest of it.
bool parse_anm2(const u8* data, usize size, Anm2& out, std::string& error);

}  // namespace isaec::data

#endif  // ISAEC_DATA_ANM2_HPP
