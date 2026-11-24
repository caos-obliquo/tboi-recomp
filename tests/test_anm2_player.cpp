// ANM2 playback tests: timing, looping, timeline clamping, pose composition
// and trigger delivery. Documents are built in memory, so no game bytes are
// involved and nothing here depends on a particular patch level.

#include "isaec/engine/animation.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "test_harness.hpp"

using namespace isaec;
using namespace isaec::engine;

namespace {

bool near(f32 a, f32 b) { return std::fabs(a - b) < 0.01f; }

// Idle: fps 10, FrameNum 5. Root holds frame 0 for two ticks, frame 1 for
// three. Layer 0 has one frame that should clamp for the whole animation.
data::Anm2 make_document() {
    data::Anm2 doc;
    doc.fps = 10;
    doc.default_animation = "Idle";
    doc.layers.push_back({0, "body", 0});
    doc.layers.push_back({1, "ghost", 1});
    doc.nulls.push_back({0, "socket", true});

    data::Anm2Animation anim;
    anim.name = "Idle";
    anim.frame_num = 5;
    anim.loop = false;

    data::Anm2Frame slow;
    slow.delay = 2;
    data::Anm2Frame fast;
    fast.delay = 3;
    anim.root_frames = {slow, fast};

    data::Anm2LayerAnimation body;
    body.layer_id = 0;
    data::Anm2Frame body_frame;
    body_frame.crop_x = 32.0f;
    body_frame.crop_y = 16.0f;
    body_frame.width = 16.0f;
    body_frame.height = 16.0f;
    body_frame.pivot_x = 8.0f;
    body_frame.pivot_y = 16.0f;
    body.visible = true;
    body.frames = {body_frame};
    anim.layer_animations = {body};
    data::Anm2NullAnimation socket;
    socket.null_id = 0;
    data::Anm2Frame socket_frame;
    socket_frame.delay = 2;
    socket_frame.x = 3.0f;
    data::Anm2Frame socket_tail;
    socket_tail.delay = 3;
    socket_tail.x = 9.0f;
    socket.frames = {socket_frame, socket_tail};
    anim.null_animations = {socket};
    doc.animations.push_back(anim);
    return doc;
}

void add_trigger(data::Anm2& doc, int event_id, int at_frame) {
    doc.animations[0].triggers.push_back({event_id, at_frame});
}

}  // namespace

ISAEC_TEST(animation_advances_on_delays_at_the_document_fps) {
    data::Anm2 doc = make_document();
    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));

    player.update(0.1f);  // one frame at 10 fps
    CHECK(player.frame_index() == 1);
    player.update(0.2f);
    CHECK(player.frame_index() == 3);
    player.update(0.1f);
    CHECK(player.frame_index() == 4);
    CHECK(!player.finished());
    player.update(0.1f);
    CHECK(player.frame_index() == 5);
    CHECK(player.finished());
}

ISAEC_TEST(animation_loop_wraps_and_does_not_finish) {
    data::Anm2 doc = make_document();
    doc.animations[0].loop = true;
    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));

    player.update(0.5f);  // exactly one duration: back to frame 0
    CHECK(!player.finished());
    CHECK(player.frame_index() == 0);
    player.update(0.1f);  // one frame past the wrap
    CHECK(player.frame_index() == 1);
}

ISAEC_TEST(animation_delay_accumulation_is_required_for_timeline_sampling) {
    data::Anm2 delayed = make_document();
    data::Anm2 no_accumulation = delayed;
    auto& delayed_frames = delayed.animations[0].layer_animations[0].frames;
    delayed_frames[0].x = 7.0f;
    delayed_frames[0].delay = 2;
    data::Anm2Frame delayed_tail = delayed_frames[0];
    delayed_tail.x = 19.0f;
    delayed_tail.delay = 3;
    delayed_frames.push_back(delayed_tail);
    auto& immediate_frames = no_accumulation.animations[0].layer_animations[0].frames;
    immediate_frames = delayed_frames;
    immediate_frames[0].delay = 1;
    immediate_frames[1].delay = 1;

    AnimationPlayer delayed_player;
    AnimationPlayer no_accumulation_player;
    delayed_player.set_document(&delayed);
    no_accumulation_player.set_document(&no_accumulation);
    CHECK(delayed_player.play("Idle"));
    CHECK(no_accumulation_player.play("Idle"));
    delayed_player.update(0.1f);  // timeline position 1
    no_accumulation_player.update(0.1f);

    LayerPose delayed_pose;
    LayerPose no_accumulation_pose;
    CHECK(delayed_player.layer_pose(0, delayed_pose));
    CHECK(no_accumulation_player.layer_pose(0, no_accumulation_pose));
    CHECK(near(delayed_pose.x, 7.0f));
    CHECK(near(no_accumulation_pose.x, 19.0f));
}

ISAEC_TEST(animation_null_timeline_honors_delay_and_composition) {
    data::Anm2 doc = make_document();
    data::Anm2Frame root;
    root.delay = 1;
    root.x = 4.0f;
    root.rotation = 10.0f;
    doc.animations[0].root_frames = {root};

    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));
    NullPose pose;
    CHECK(player.null_pose(0, pose));
    CHECK(near(pose.x, 7.0f));
    CHECK(pose.visible);

    player.update(0.2f);  // frame 2: first null key's two-tick Delay elapsed
    CHECK(player.null_pose(0, pose));
    CHECK(near(pose.x, 13.0f));
    player.update(0.1f);
    CHECK(player.null_pose(0, pose));
    CHECK(near(pose.x, 13.0f));
    CHECK(near(pose.rotation, 10.0f));
    doc.nulls[0].show = false;
    CHECK(player.null_pose(0, pose));
    CHECK(!pose.visible);
}

ISAEC_TEST(animation_clamps_a_timeline_shorter_than_the_animation) {
    data::Anm2 doc = make_document();
    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));
    player.update(0.4f);  // frame 4, layer timeline still has one frame
    CHECK(player.frame_index() == 4);

    LayerPose pose;
    CHECK(player.layer_pose(0, pose));
    CHECK(near(pose.crop_x, 32.0f));
    CHECK(near(pose.crop_w, 16.0f));
}

ISAEC_TEST(animation_composes_root_and_layer_pose) {
    data::Anm2 doc = make_document();
    data::Anm2Frame root;
    root.delay = 1;
    root.x = 10.0f;
    root.y = -4.0f;
    root.scale_x = 200.0f;
    root.scale_y = 50.0f;
    root.rotation = 10.0f;
    root.red = 200;
    root.alpha = 128;
    doc.animations[0].root_frames = {root};

    data::Anm2Frame layer;
    layer.delay = 1;
    layer.x = 5.0f;
    layer.y = 1.0f;
    layer.pivot_x = 8.0f;
    layer.pivot_y = 16.0f;
    layer.width = 16.0f;
    layer.height = 16.0f;
    layer.scale_x = 50.0f;
    layer.scale_y = 200.0f;
    layer.rotation = 5.0f;
    layer.red = 100;
    doc.animations[0].layer_animations[0].frames = {layer};

    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));

    LayerPose pose;
    CHECK(player.layer_pose(0, pose));
    CHECK(near(pose.x, 15.0f));
    CHECK(near(pose.y, -3.0f));
    CHECK(near(pose.scale_x, 100.0f));
    CHECK(near(pose.scale_y, 100.0f));
    CHECK(near(pose.rotation, 15.0f));
    CHECK(pose.red == 78);  // (200 * 100 + 127) / 255
    CHECK(pose.alpha == 128);
}

ISAEC_TEST(animation_triggers_fire_once_in_frame_order) {
    data::Anm2 doc = make_document();
    add_trigger(doc, 7, 3);
    add_trigger(doc, 5, 1);
    add_trigger(doc, 6, 1);
    add_trigger(doc, 4, 0);

    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));

    std::vector<int> fired;
    player.update(0.0f);
    player.take_triggers(fired);
    CHECK(fired == std::vector<int>({4}));

    fired.clear();
    player.update(0.2f);  // frames 1 and 2
    player.take_triggers(fired);
    CHECK(fired == std::vector<int>({5, 6}));

    fired.clear();
    player.update(0.1f);  // frame 3
    player.take_triggers(fired);
    CHECK(fired == std::vector<int>({7}));

    fired.clear();
    player.update(0.1f);  // frame 4, no further triggers
    player.update(0.1f);  // frame 5, animation ends
    player.take_triggers(fired);
    CHECK(fired.empty());
}

ISAEC_TEST(animation_triggers_rearm_on_loop_but_not_within_a_frame) {
    data::Anm2 doc = make_document();
    doc.animations[0].loop = true;
    add_trigger(doc, 1, 0);
    add_trigger(doc, 2, 2);

    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));

    std::vector<int> fired;
    player.update(0.0f);
    player.take_triggers(fired);
    CHECK(fired == std::vector<int>({1}));

    fired.clear();
    player.update(0.0f);  // still frame 0: no second delivery
    player.take_triggers(fired);
    CHECK(fired.empty());

    fired.clear();
    player.update(0.2f);  // frames 1, 2
    player.take_triggers(fired);
    CHECK(fired == std::vector<int>({2}));

    fired.clear();
    player.update(0.3f);  // frames 3, 4, wrap to 0
    player.take_triggers(fired);
    CHECK(fired == std::vector<int>({1}));

    fired.clear();
    player.update(10.0f);  // multiple complete loops are not dropped after a long pause
    player.take_triggers(fired);
    std::vector<int> expected;
    for (int loop = 0; loop < 20; ++loop) {
        expected.push_back(2);
        expected.push_back(1);
    }
    CHECK(fired == expected);
}

ISAEC_TEST(animation_unknown_animation_and_missing_track_are_reported) {
    data::Anm2 doc = make_document();
    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(!player.play("Walk"));
    CHECK(player.animation() == nullptr);

    CHECK(player.play("Idle"));
    LayerPose pose;
    CHECK(player.layer_pose(1, pose) == false);  // layer 1 has no track
    CHECK(player.layer_pose(9, pose) == false);
    CHECK(player.play_default());
    CHECK(player.layer_pose(0, pose));
}

ISAEC_TEST(animation_reset_restarts_without_pending_triggers) {
    data::Anm2 doc = make_document();
    add_trigger(doc, 1, 0);
    AnimationPlayer player;
    player.set_document(&doc);
    CHECK(player.play("Idle"));
    player.update(0.0f);

    player.reset();
    std::vector<int> fired;
    player.take_triggers(fired);
    CHECK(fired.empty());
    CHECK(player.frame_index() == 0);
    CHECK(!player.finished());

    player.update(0.0f);
    player.take_triggers(fired);
    CHECK(fired == std::vector<int>({1}));
}
