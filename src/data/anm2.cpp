#include "isaec/data/anm2.hpp"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace isaec::data {
namespace {

struct Tag {
    std::string name;
    bool closing = false;
    bool self_closing = false;
    std::vector<std::pair<std::string, std::string>> attrs;
};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

std::string decode_entities(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (usize i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') {
            out.push_back(raw[i]);
            continue;
        }
        const usize semi = raw.find(';', i);
        if (semi == std::string_view::npos) {
            out.push_back(raw[i]);
            continue;
        }
        const std::string_view name = raw.substr(i + 1, semi - i - 1);
        if (name == "amp") out.push_back('&');
        else if (name == "lt") out.push_back('<');
        else if (name == "gt") out.push_back('>');
        else if (name == "quot") out.push_back('"');
        else if (name == "apos") out.push_back('\'');
        else out.append(raw.substr(i, semi - i + 1));
        i = semi;
    }
    return out;
}

// Pulls the next element tag out of `text`, skipping text, comments, the XML
// declaration and doctype-like declarations. Attribute values may use either
// quote character; ANM2 writers emit double quotes, but single quotes cost
// nothing to accept.
bool next_tag(std::string_view& text, Tag& tag, std::string& error) {
    tag = Tag{};
    while (!text.empty() && text.front() != '<') text.remove_prefix(1);
    if (text.empty()) return false;
    if (text.substr(0, 4) == "<!--") {
        const usize end = text.find("-->");
        if (end == std::string_view::npos) {
            error = "unterminated XML comment";
            return false;
        }
        text.remove_prefix(end + 3);
        return next_tag(text, tag, error);
    }
    if (text.substr(0, 2) == "<?") {
        const usize end = text.find("?>");
        if (end == std::string_view::npos) {
            error = "unterminated XML declaration";
            return false;
        }
        text.remove_prefix(end + 2);
        return next_tag(text, tag, error);
    }
    if (text.substr(0, 2) == "<!") {
        const usize end = text.find('>');
        if (end == std::string_view::npos) {
            error = "unterminated declaration";
            return false;
        }
        text.remove_prefix(end + 1);
        return next_tag(text, tag, error);
    }
    text.remove_prefix(1);
    if (!text.empty() && text.front() == '/') {
        tag.closing = true;
        text.remove_prefix(1);
    }
    while (!text.empty() && is_name_char(text.front())) {
        tag.name.push_back(text.front());
        text.remove_prefix(1);
    }
    if (tag.name.empty()) {
        error = "element with an empty name";
        return false;
    }
    while (true) {
        while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
        if (text.empty()) {
            error = "unterminated <" + tag.name + "> tag";
            return false;
        }
        if (text.front() == '>') {
            text.remove_prefix(1);
            return true;
        }
        if (text.front() == '/') {
            tag.self_closing = true;
            text.remove_prefix(1);
            continue;
        }
        std::string key;
        while (!text.empty() && text.front() != '=' && !is_space(text.front()) &&
               text.front() != '>' && text.front() != '/') {
            key.push_back(text.front());
            text.remove_prefix(1);
        }
        while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
        if (key.empty() || text.empty() || text.front() != '=') {
            error = "attribute without a value in <" + tag.name + ">";
            return false;
        }
        text.remove_prefix(1);
        while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
        if (text.empty() || (text.front() != '"' && text.front() != '\'')) {
            error = "unquoted attribute value in <" + tag.name + ">";
            return false;
        }
        const char quote = text.front();
        text.remove_prefix(1);
        const usize end = text.find(quote);
        if (end == std::string_view::npos) {
            error = "unterminated attribute value in <" + tag.name + ">";
            return false;
        }
        tag.attrs.emplace_back(key, decode_entities(text.substr(0, end)));
        text.remove_prefix(end + 1);
    }
}

const std::string* attr(const Tag& tag, const char* key) {
    for (const auto& [name, value] : tag.attrs) {
        if (name == key) return &value;
    }
    return nullptr;
}

std::string attr_string(const Tag& tag, const char* key, std::string fallback = {}) {
    const std::string* value = attr(tag, key);
    return value != nullptr ? *value : std::move(fallback);
}

int attr_int(const Tag& tag, const char* key, int fallback) {
    const std::string* value = attr(tag, key);
    if (value == nullptr || value->empty()) return fallback;
    return static_cast<int>(std::strtol(value->c_str(), nullptr, 10));
}

float attr_float(const Tag& tag, const char* key, float fallback) {
    const std::string* value = attr(tag, key);
    if (value == nullptr || value->empty()) return fallback;
    return std::strtof(value->c_str(), nullptr);
}

u8 attr_u8(const Tag& tag, const char* key, u8 fallback) {
    return static_cast<u8>(attr_int(tag, key, fallback));
}

i8 attr_i8(const Tag& tag, const char* key, i8 fallback) {
    return static_cast<i8>(attr_int(tag, key, fallback));
}

bool attr_bool(const Tag& tag, const char* key, bool fallback) {
    const std::string* value = attr(tag, key);
    if (value == nullptr) return fallback;
    return *value == "true" || *value == "1";
}

Anm2Frame parse_frame(const Tag& tag) {
    Anm2Frame frame;
    frame.x = attr_float(tag, "XPosition", 0.0f);
    frame.y = attr_float(tag, "YPosition", 0.0f);
    frame.pivot_x = attr_float(tag, "XPivot", 0.0f);
    frame.pivot_y = attr_float(tag, "YPivot", 0.0f);
    frame.crop_x = attr_float(tag, "XCrop", 0.0f);
    frame.crop_y = attr_float(tag, "YCrop", 0.0f);
    frame.width = attr_float(tag, "Width", 0.0f);
    frame.height = attr_float(tag, "Height", 0.0f);
    frame.delay = attr_int(tag, "Delay", 1);
    frame.scale_x = attr_float(tag, "XScale", 100.0f);
    frame.scale_y = attr_float(tag, "YScale", 100.0f);
    frame.rotation = attr_float(tag, "Rotation", 0.0f);
    frame.red = attr_u8(tag, "RedTint", 255);
    frame.green = attr_u8(tag, "GreenTint", 255);
    frame.blue = attr_u8(tag, "BlueTint", 255);
    frame.alpha = attr_u8(tag, "AlphaTint", 255);
    frame.red_offset = attr_i8(tag, "RedOffset", 0);
    frame.green_offset = attr_i8(tag, "GreenOffset", 0);
    frame.blue_offset = attr_i8(tag, "BlueOffset", 0);
    frame.visible = attr_bool(tag, "Visible", true);
    frame.interpolated = attr_bool(tag, "Interpolated", false);
    return frame;
}

bool present(const std::vector<int>& ids, int id) {
    for (int candidate : ids) {
        if (candidate == id) return true;
    }
    return false;
}

template <typename Container>
std::vector<int> collect_ids(const Container& items) {
    std::vector<int> ids;
    for (const auto& item : items) ids.push_back(item.id);
    return ids;
}

// Counts ids an animation points at that the document does not define. This is
// data, not a syntax error: shipped archives contain references to layers from
// an older revision of the same document.
bool validate(Anm2& doc, std::string& error) {
    if (doc.animations.empty()) {
        error = "document has no <Animation>";
        return false;
    }
    const std::vector<int> sheets = collect_ids(doc.spritesheets);
    const std::vector<int> layers = collect_ids(doc.layers);
    const std::vector<int> nulls = collect_ids(doc.nulls);
    const std::vector<int> events = collect_ids(doc.events);
    for (const Anm2Layer& layer : doc.layers) {
        if (!present(sheets, layer.spritesheet_id)) ++doc.dangling_references;
    }
    for (const Anm2Animation& animation : doc.animations) {
        // Shipped documents carry blank placeholder animations (Name="",
        // FrameNum="0"). They are not playable and are kept only so the rest of
        // the document parses; see docs/anm2-format.md.
        if (animation.name.empty()) continue;
        if (animation.frame_num < 0) {
            error = "animation " + animation.name + " has FrameNum " +
                    std::to_string(animation.frame_num);
            return false;
        }
        for (const Anm2LayerAnimation& track : animation.layer_animations) {
            if (!present(layers, track.layer_id)) ++doc.dangling_references;
        }
        for (const Anm2NullAnimation& track : animation.null_animations) {
            if (!present(nulls, track.null_id)) ++doc.dangling_references;
        }
        for (const Anm2Trigger& trigger : animation.triggers) {
            if (!present(events, trigger.event_id)) ++doc.dangling_references;
        }
    }
    return true;
}

enum class Timeline { None, Root, Layer, Null, Triggers };

}  // namespace

const Anm2Animation* Anm2::find(std::string_view name) const noexcept {
    if (name.empty()) return nullptr;
    for (const Anm2Animation& animation : animations) {
        if (animation.name == name) return &animation;
    }
    return nullptr;
}

bool parse_anm2(const u8* data, usize size, Anm2& out, std::string& error) {
    out = Anm2{};
    error.clear();
    if (data == nullptr || size == 0) {
        error = "empty ANM2 document";
        return false;
    }

    std::string_view text(reinterpret_cast<const char*>(data), size);
    Anm2Animation* animation = nullptr;
    Anm2LayerAnimation* layer_track = nullptr;
    Anm2NullAnimation* null_track = nullptr;
    Timeline timeline = Timeline::None;
    bool saw_root = false;

    Tag tag;
    while (next_tag(text, tag, error)) {
        if (tag.closing) {
            if (tag.name == "Animation") animation = nullptr;
            if (tag.name == "LayerAnimation") layer_track = nullptr;
            if (tag.name == "NullAnimation") null_track = nullptr;
            if (tag.name == "RootAnimation" || tag.name == "LayerAnimations" ||
                tag.name == "NullAnimations" || tag.name == "Triggers") {
                timeline = Timeline::None;
            }
            continue;
        }

        if (tag.name == "AnimatedActor") {
            saw_root = true;
        } else if (tag.name == "Info") {
            out.fps = attr_int(tag, "Fps", out.fps);
            out.version = attr_int(tag, "Version", out.version);
        } else if (tag.name == "Spritesheet") {
            out.spritesheets.push_back({attr_int(tag, "Id", 0), attr_string(tag, "Path")});
        } else if (tag.name == "Layer") {
            out.layers.push_back({attr_int(tag, "Id", 0), attr_string(tag, "Name"),
                                  attr_int(tag, "SpritesheetId", 0)});
        } else if (tag.name == "Null") {
            // <Nulls/> is a container; an element is <Null>.
            out.nulls.push_back(
                {attr_int(tag, "Id", 0), attr_string(tag, "Name"), attr_bool(tag, "Show", true)});
        } else if (tag.name == "Event") {
            out.events.push_back({attr_int(tag, "Id", 0), attr_string(tag, "Name")});
        } else if (tag.name == "Animations") {
            out.default_animation = attr_string(tag, "DefaultAnimation");
        } else if (tag.name == "Animation") {
            if (animation != nullptr) {
                error = "nested <Animation> inside " + animation->name;
                return false;
            }
            out.animations.push_back({});
            animation = &out.animations.back();
            animation->name = attr_string(tag, "Name");
            animation->frame_num = attr_int(tag, "FrameNum", 1);
            animation->loop = attr_bool(tag, "Loop", true);
        } else if (tag.name == "RootAnimation") {
            timeline = Timeline::Root;
        } else if (tag.name == "LayerAnimation") {
            if (animation == nullptr) {
                error = "<LayerAnimation> outside <Animation>";
                return false;
            }
            animation->layer_animations.push_back({});
            layer_track = &animation->layer_animations.back();
            layer_track->layer_id = attr_int(tag, "LayerId", 0);
            layer_track->visible = attr_bool(tag, "Visible", true);
            timeline = Timeline::Layer;
        } else if (tag.name == "NullAnimation") {
            if (animation == nullptr) {
                error = "<NullAnimation> outside <Animation>";
                return false;
            }
            animation->null_animations.push_back({});
            null_track = &animation->null_animations.back();
            null_track->null_id = attr_int(tag, "NullId", 0);
            timeline = Timeline::Null;
        } else if (tag.name == "Triggers") {
            timeline = Timeline::Triggers;
        } else if (tag.name == "Frame") {
            if (animation == nullptr) {
                error = "<Frame> outside <Animation>";
                return false;
            }
            Anm2Frame frame = parse_frame(tag);
            if (timeline == Timeline::Root) animation->root_frames.push_back(frame);
            else if (timeline == Timeline::Layer && layer_track != nullptr)
                layer_track->frames.push_back(frame);
            else if (timeline == Timeline::Null && null_track != nullptr)
                null_track->frames.push_back(frame);
            else {
                error = "<Frame> outside a timeline in animation " + animation->name;
                return false;
            }
        } else if (tag.name == "Trigger") {
            if (animation == nullptr) {
                error = "<Trigger> outside <Animation>";
                return false;
            }
            if (timeline != Timeline::Triggers) {
                error = "<Trigger> outside <Triggers> in animation " + animation->name;
                return false;
            }
            animation->triggers.push_back(
                {attr_int(tag, "EventId", 0), attr_int(tag, "AtFrame", 0)});
        }
    }
    if (!error.empty()) return false;
    if (!saw_root) {
        error = "missing <AnimatedActor> root";
        return false;
    }
    if (!validate(out, error)) return false;
    return true;
}

}  // namespace isaec::data
