// Playback of parsed ANM2 documents: which frame is showing now, and what a
// renderer should draw for each layer.
#ifndef ISAEC_ENGINE_ANIMATION_HPP
#define ISAEC_ENGINE_ANIMATION_HPP

#include <string_view>
#include <vector>

#include "isaec/data/anm2.hpp"
#include "isaec/platform/types.hpp"

namespace isaec::engine {

// One composed layer draw pose. Position and rotation include root transforms;
// the pivot and crop come from the layer timeline. Scales are percentages.
struct LayerPose {
    int layer_id = 0;
    bool visible = false;
    float crop_x = 0.0f;
    float crop_y = 0.0f;
    float crop_w = 0.0f;
    float crop_h = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float pivot_x = 0.0f;
    float pivot_y = 0.0f;
    float scale_x = 100.0f;
    float scale_y = 100.0f;
    float rotation = 0.0f;
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    u8 alpha = 255;
};

// Composed null pose. Nulls have no crop or pivot, but otherwise receive root
// and per-null timeline transforms like the shipped player's null cursor.
struct NullPose {
    int null_id = 0;
    bool visible = false;
    float x = 0.0f;
    float y = 0.0f;
    float scale_x = 100.0f;
    float scale_y = 100.0f;
    float rotation = 0.0f;
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    u8 alpha = 255;
};

class AnimationPlayer {
public:
    // The document must outlive the player, which only keeps a pointer.
    void set_document(const data::Anm2* document);
    bool play(std::string_view name);
    // Starts DefaultAnimation; false when it is absent.
    bool play_default();
    // Advances by wall-clock seconds. AtFrame 0 fires once on first update.
    void update(f32 seconds);
    void reset();

    const data::Anm2* document() const { return document_; }
    const data::Anm2Animation* animation() const { return animation_; }
    // Whole animation frames since start, in [0, FrameNum]. Looping runs expose
    // the wrapped in-range frame index; non-looping animations stop at FrameNum.
    int frame_index() const { return frame_; }
    bool finished() const { return finished_; }

    // Drains pending event IDs in AtFrame order; ties keep document order.
    void take_triggers(std::vector<int>& out);
    bool layer_pose(usize layer_index, LayerPose& out) const;
    bool null_pose(usize null_index, NullPose& out) const;
    usize layer_count() const;

private:
    const data::Anm2Frame* root_frame() const;
    const data::Anm2Frame* track_frame(const std::vector<data::Anm2Frame>& frames,
                                       int frame) const;
    void prepare_triggers();
    void emit_triggers_at(int frame);
    void emit_trigger_range(int first, int last);
    void emit_looped_crossings(u64 first, u64 last, u64 duration);
    void compose_pose(const data::Anm2Frame& root, const data::Anm2Frame& pose, bool visible,
                      LayerPose& out) const;

    const data::Anm2* document_ = nullptr;
    const data::Anm2Animation* animation_ = nullptr;
    double fractional_frames_ = 0.0;
    u64 total_frames_ = 0;
    int frame_ = 0;
    bool finished_ = false;
    bool started_ = false;
    std::vector<const data::Anm2Trigger*> ordered_triggers_;
    std::vector<int> pending_triggers_;
};

}  // namespace isaec::engine

#endif  // ISAEC_ENGINE_ANIMATION_HPP
