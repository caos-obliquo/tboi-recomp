#include "isaec/engine/animation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace isaec::engine {
namespace {

int frames_per_second(const data::Anm2* document) {
    return document != nullptr && document->fps > 0 ? document->fps : 30;
}

u8 multiply_tint(u8 a, u8 b) {
    return static_cast<u8>((static_cast<unsigned>(a) * b + 127u) / 255u);
}

}  // namespace

void AnimationPlayer::set_document(const data::Anm2* document) {
    document_ = document;
    animation_ = nullptr;
    reset();
}

bool AnimationPlayer::play(std::string_view name) {
    if (document_ == nullptr) return false;
    const data::Anm2Animation* found = document_->find(name);
    if (found == nullptr) return false;
    animation_ = found;
    prepare_triggers();
    reset();
    return true;
}

bool AnimationPlayer::play_default() {
    if (document_ == nullptr || document_->default_animation.empty()) return false;
    return play(document_->default_animation);
}

void AnimationPlayer::reset() {
    fractional_frames_ = 0.0;
    total_frames_ = 0;
    frame_ = 0;
    finished_ = false;
    started_ = false;
    pending_triggers_.clear();
}

void AnimationPlayer::prepare_triggers() {
    ordered_triggers_.clear();
    if (animation_ == nullptr) return;
    ordered_triggers_.reserve(animation_->triggers.size());
    for (const data::Anm2Trigger& trigger : animation_->triggers) {
        ordered_triggers_.push_back(&trigger);
    }
    std::stable_sort(ordered_triggers_.begin(), ordered_triggers_.end(),
                     [](const data::Anm2Trigger* a, const data::Anm2Trigger* b) {
                         return a->at_frame < b->at_frame;
                     });
}

usize AnimationPlayer::layer_count() const {
    return document_ == nullptr ? 0 : document_->layers.size();
}

void AnimationPlayer::update(f32 seconds) {
    if (animation_ == nullptr) return;
    if (!started_) {
        started_ = true;
        emit_triggers_at(0);
    }
    if (finished_ || !std::isfinite(seconds) || seconds <= 0.0f) return;

    const double advanced = static_cast<double>(seconds) * frames_per_second(document_);
    const double available = advanced + fractional_frames_;
    if (!std::isfinite(available) || available <= 0.0) return;
    const double rounded = std::floor(available + 1e-7);
    if (rounded < 1.0) {
        fractional_frames_ = available;
        return;
    }

    const u64 remaining_ticks = std::numeric_limits<u64>::max() - total_frames_;
    const double remaining_limit = static_cast<double>(remaining_ticks);
    const bool saturating = rounded >= remaining_limit;
    const u64 ticks = saturating ? remaining_ticks : static_cast<u64>(rounded);
    fractional_frames_ = saturating ? 0.0 : available - static_cast<double>(ticks);
    if (ticks == 0) return;

    const u64 old_total = total_frames_;
    const u64 new_total = old_total + ticks;
    const u64 duration = static_cast<u64>(std::max(1, animation_->frame_num));

    if (animation_->loop) {
        emit_looped_crossings(old_total + 1, new_total, duration);
        total_frames_ = new_total;
        frame_ = static_cast<int>(new_total % duration);
        return;
    }

    const u64 end = duration - 1;
    const u64 crossed_end = std::min(new_total, end);
    if (old_total < crossed_end) emit_trigger_range(static_cast<int>(old_total + 1),
                                                    static_cast<int>(crossed_end));
    total_frames_ = std::min(new_total, duration);
    frame_ = static_cast<int>(total_frames_);
    finished_ = total_frames_ >= duration;
}

void AnimationPlayer::emit_triggers_at(int frame) {
    if (frame < 0) return;
    auto it = std::lower_bound(ordered_triggers_.begin(), ordered_triggers_.end(), frame,
                               [](const data::Anm2Trigger* trigger, int value) {
                                   return trigger->at_frame < value;
                               });
    for (; it != ordered_triggers_.end() && (*it)->at_frame == frame; ++it) {
        pending_triggers_.push_back((*it)->event_id);
    }
}

void AnimationPlayer::emit_trigger_range(int first, int last) {
    if (first > last || ordered_triggers_.empty()) return;
    auto it = std::lower_bound(ordered_triggers_.begin(), ordered_triggers_.end(), first,
                               [](const data::Anm2Trigger* trigger, int value) {
                                   return trigger->at_frame < value;
                               });
    for (; it != ordered_triggers_.end() && (*it)->at_frame <= last; ++it) {
        if ((*it)->at_frame >= 0) pending_triggers_.push_back((*it)->event_id);
    }
}

void AnimationPlayer::emit_looped_crossings(u64 first, u64 last, u64 duration) {
    if (first > last || ordered_triggers_.empty()) return;
    while (first <= last) {
        const u64 local_first = first % duration;
        const u64 run = duration - local_first;
        const u64 remaining = last - first;
        const u64 run_last = first + std::min(run - 1, remaining);
        emit_trigger_range(static_cast<int>(local_first), static_cast<int>(run_last % duration));
        if (run_last == last) break;
        first = run_last + 1;
    }
}

void AnimationPlayer::take_triggers(std::vector<int>& out) {
    out.insert(out.end(), pending_triggers_.begin(), pending_triggers_.end());
    pending_triggers_.clear();
}

const data::Anm2Frame* AnimationPlayer::track_frame(
    const std::vector<data::Anm2Frame>& frames, int frame) const {
    if (frames.empty()) return nullptr;
    int remaining = std::max(0, frame);
    for (const data::Anm2Frame& key : frames) {
        const int delay = key.delay > 0 ? key.delay : 1;
        if (remaining < delay) return &key;
        remaining -= delay;
    }
    return &frames.back();
}

const data::Anm2Frame* AnimationPlayer::root_frame() const {
    if (animation_ == nullptr) return nullptr;
    return track_frame(animation_->root_frames, frame_);
}

void AnimationPlayer::compose_pose(const data::Anm2Frame& root, const data::Anm2Frame& pose,
                                   bool visible, LayerPose& out) const {
    out.visible = visible && root.visible && pose.visible;
    out.x = root.x + pose.x;
    out.y = root.y + pose.y;
    out.pivot_x = pose.pivot_x;
    out.pivot_y = pose.pivot_y;
    out.scale_x = root.scale_x * pose.scale_x / 100.0f;
    out.scale_y = root.scale_y * pose.scale_y / 100.0f;
    out.rotation = root.rotation + pose.rotation;
    out.red = multiply_tint(root.red, pose.red);
    out.green = multiply_tint(root.green, pose.green);
    out.blue = multiply_tint(root.blue, pose.blue);
    out.alpha = multiply_tint(root.alpha, pose.alpha);
}

bool AnimationPlayer::layer_pose(usize layer_index, LayerPose& out) const {
    if (document_ == nullptr || animation_ == nullptr || layer_index >= document_->layers.size()) {
        return false;
    }
    const data::Anm2Frame* root = root_frame();
    if (root == nullptr) return false;

    const data::Anm2Layer& layer = document_->layers[layer_index];
    for (const data::Anm2LayerAnimation& track : animation_->layer_animations) {
        if (track.layer_id != layer.id) continue;
        const data::Anm2Frame* pose = track_frame(track.frames, frame_);
        if (pose == nullptr) return false;
        out.layer_id = layer.id;
        out.crop_x = pose->crop_x;
        out.crop_y = pose->crop_y;
        out.crop_w = pose->width;
        out.crop_h = pose->height;
        compose_pose(*root, *pose, track.visible, out);
        return true;
    }
    return false;
}

bool AnimationPlayer::null_pose(usize null_index, NullPose& out) const {
    if (document_ == nullptr || animation_ == nullptr || null_index >= document_->nulls.size()) {
        return false;
    }
    const data::Anm2Frame* root = root_frame();
    if (root == nullptr) return false;

    const data::Anm2Null& null = document_->nulls[null_index];
    for (const data::Anm2NullAnimation& track : animation_->null_animations) {
        if (track.null_id != null.id) continue;
        const data::Anm2Frame* pose = track_frame(track.frames, frame_);
        if (pose == nullptr) return false;
        out.null_id = null.id;
        out.visible = null.show && root->visible && pose->visible;
        out.x = root->x + pose->x;
        out.y = root->y + pose->y;
        out.scale_x = root->scale_x * pose->scale_x / 100.0f;
        out.scale_y = root->scale_y * pose->scale_y / 100.0f;
        out.rotation = root->rotation + pose->rotation;
        out.red = multiply_tint(root->red, pose->red);
        out.green = multiply_tint(root->green, pose->green);
        out.blue = multiply_tint(root->blue, pose->blue);
        out.alpha = multiply_tint(root->alpha, pose->alpha);
        return true;
    }
    return false;
}

}  // namespace isaec::engine
