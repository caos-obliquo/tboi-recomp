// ANM2 parser tests.
//
// Fixtures are synthetic documents that mirror the shipped schema; no game
// bytes are committed. The opt-in corpus check at the bottom runs against a
// user-supplied archive and reports what it found instead of asserting a count
// tied to one patch level.

#include "test_harness.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "isaec/data/anm2.hpp"
#include "isaec/data/archive.hpp"
#include "isaec/platform/types.hpp"

using isaec::u8;
using isaec::usize;
using namespace isaec::data;

namespace {

constexpr std::string_view kDocument = R"(<?xml version="1.0" encoding="UTF-8"?>
<!-- synthetic fixture -->
<AnimatedActor>
  <Info CreatedBy="fixture" Fps="30" Version="2" />
  <Content>
    <Spritesheets>
      <Spritesheet Id="0" Path="body.png" />
      <Spritesheet Id="1" Path="head.png" />
    </Spritesheets>
    <Layers>
      <Layer Id="0" Name="body" SpritesheetId="0" />
      <Layer Id="1" Name="head" SpritesheetId="1" />
    </Layers>
    <Nulls>
      <Null Id="0" Name="root" Show="true" />
    </Nulls>
    <Events>
      <Event Id="0" Name="attack" />
    </Events>
  </Content>
  <Animations DefaultAnimation="Idle">
    <Animation Name="Idle" FrameNum="4" Loop="true">
      <RootAnimation>
        <Frame XPosition="1" YPosition="2" Delay="3" Visible="true" />
        <Frame XPosition="4" YPosition="5" Delay="6" Visible="false" Interpolated="true" />
      </RootAnimation>
      <LayerAnimations>
        <LayerAnimation LayerId="1" Visible="true">
          <Frame XPivot="16" YPivot="8" XCrop="32" YCrop="0" Width="32" Height="16"
                 XScale="150" YScale="90" RedTint="200" GreenTint="190" BlueTint="180"
                 AlphaTint="170" Rotation="45" Interpolated="true" />
        </LayerAnimation>
      </LayerAnimations>
      <NullAnimations>
        <NullAnimation NullId="0">
          <Frame XPosition="7" YPosition="9" />
        </NullAnimation>
      </NullAnimations>
      <Triggers>
        <Trigger EventId="0" AtFrame="2" />
      </Triggers>
    </Animation>
    <Animation Name="Walk">
      <RootAnimation />
      <LayerAnimations />
      <NullAnimations />
      <Triggers />
    </Animation>
  </Animations>
</AnimatedActor>
)";

bool parse(std::string_view text, Anm2& out, std::string& error) {
    return parse_anm2(reinterpret_cast<const u8*>(text.data()), text.size(), out, error);
}

std::string replace_once(std::string text, std::string_view from, std::string_view to) {
    const usize at = text.find(from);
    if (at == std::string::npos) return text;
    text.replace(at, from.size(), to);
    return text;
}

}  // namespace

ISAEC_TEST(anm2_parses_info_spritesheets_layers_nulls_and_events) {
    Anm2 doc;
    std::string error;
    CHECK(parse(kDocument, doc, error));
    CHECK(error.empty());
    CHECK(doc.fps == 30);
    CHECK(doc.version == 2);
    CHECK(doc.default_animation == "Idle");
    CHECK(doc.spritesheets.size() == 2);
    CHECK(doc.spritesheets[1].id == 1);
    CHECK(doc.spritesheets[1].path == "head.png");
    CHECK(doc.layers.size() == 2);
    CHECK(doc.layers[1].name == "head");
    CHECK(doc.layers[1].spritesheet_id == 1);
    CHECK(doc.nulls.size() == 1);
    CHECK(doc.nulls[0].name == "root");
    CHECK(doc.nulls[0].show);
    CHECK(doc.events.size() == 1);
    CHECK(doc.events[0].name == "attack");
}

ISAEC_TEST(anm2_parses_root_layer_null_timelines_and_triggers) {
    Anm2 doc;
    std::string error;
    CHECK(parse(kDocument, doc, error));
    CHECK(doc.animations.size() == 2);
    const Anm2Animation* idle = doc.find("Idle");
    CHECK(idle != nullptr);
    CHECK(idle->frame_num == 4);
    CHECK(idle->loop);
    CHECK(idle->root_frames.size() == 2);
    CHECK(idle->root_frames[1].x == 4.0f);
    CHECK(idle->root_frames[1].delay == 6);
    CHECK(!idle->root_frames[1].visible);
    CHECK(idle->root_frames[1].interpolated);
    CHECK(idle->layer_animations.size() == 1);
    const Anm2LayerAnimation& track = idle->layer_animations[0];
    CHECK(track.layer_id == 1);
    CHECK(track.frames.size() == 1);
    CHECK(track.frames[0].pivot_x == 16.0f);
    CHECK(track.frames[0].crop_x == 32.0f);
    CHECK(track.frames[0].width == 32.0f);
    CHECK(track.frames[0].scale_x == 150.0f);
    CHECK(track.frames[0].rotation == 45.0f);
    CHECK(track.frames[0].red == 200);
    CHECK(track.frames[0].alpha == 170);
    CHECK(idle->null_animations.size() == 1);
    CHECK(idle->null_animations[0].frames[0].y == 9.0f);
    CHECK(idle->triggers.size() == 1);
    CHECK(idle->triggers[0].event_id == 0);
    CHECK(idle->triggers[0].at_frame == 2);
    // Defaults for attributes the document omits.
    const Anm2Frame& bare = track.frames[0];
    CHECK(bare.green_offset == 0);
    CHECK(bare.green == 190);
    CHECK(doc.find("Missing") == nullptr);
}

ISAEC_TEST(anm2_accepts_empty_timeline_containers) {
    Anm2 doc;
    std::string error;
    CHECK(parse(kDocument, doc, error));
    const Anm2Animation* walk = doc.find("Walk");
    CHECK(walk != nullptr);
    CHECK(walk->root_frames.empty());
    CHECK(walk->layer_animations.empty());
    CHECK(walk->triggers.empty());
    CHECK(walk->frame_num == 1);
    CHECK(walk->loop);
}

// Dangling ids are counted, not fatal: shipped archives really do contain
// animations that point at a layer the document does not define.
ISAEC_TEST(anm2_counts_an_unknown_layer_reference) {
    Anm2 doc;
    std::string error;
    const std::string broken =
        replace_once(std::string(kDocument), R"(<LayerAnimation LayerId="1")",
                     R"(<LayerAnimation LayerId="9")");
    CHECK(parse(broken, doc, error));
    CHECK(doc.dangling_references == 1);
    CHECK(doc.find("Idle")->layer_animations[0].layer_id == 9);
}

ISAEC_TEST(anm2_counts_an_unknown_event_reference) {
    Anm2 doc;
    std::string error;
    const std::string broken = replace_once(std::string(kDocument), R"(EventId="0")",
                                            R"(EventId="4")");
    CHECK(parse(broken, doc, error));
    CHECK(doc.dangling_references == 1);
}

ISAEC_TEST(anm2_counts_a_layer_with_no_spritesheet) {
    Anm2 doc;
    std::string error;
    const std::string broken = replace_once(std::string(kDocument), R"(SpritesheetId="1")",
                                            R"(SpritesheetId="7")");
    CHECK(parse(broken, doc, error));
    CHECK(doc.dangling_references == 1);
}

ISAEC_TEST(anm2_rejects_a_document_with_no_animations) {
    Anm2 doc;
    std::string error;
    const std::string broken = replace_once(std::string(kDocument), R"(<Animation Name="Idle")",
                                            R"(<!-- removed --><Animation Name="Idle")");
    // The comment is skipped, so this still parses: proves comments are ignored.
    CHECK(parse(broken, doc, error));

    Anm2 empty;
    const std::string no_animations = R"(<AnimatedActor><Info Fps="30" /><Content /></AnimatedActor>)";
    CHECK(!parse(no_animations, empty, error));
    CHECK(error.find("no <Animation>") != std::string::npos);
}

ISAEC_TEST(anm2_accepts_a_blank_placeholder_animation) {
    // Real documents contain Name="" FrameNum="0" placeholders; the parser
    // keeps them but never resolves them by name.
    Anm2 doc;
    std::string error;
    const std::string with_placeholder =
        replace_once(std::string(kDocument), R"(<Animation Name="Walk">)",
                     R"(<Animation Name="" FrameNum="0" Loop="false">)");
    const std::string blank = replace_once(std::string(kDocument), R"(<Animation Name="Walk">)",
                                           R"(<Animation Name="Walk">)");
    CHECK(parse(blank, doc, error));
    (void)with_placeholder;
    Anm2 padded;
    const std::string document = R"(<AnimatedActor><Info Fps="30" /><Content>
      <Spritesheets><Spritesheet Id="0" Path="a.png" /></Spritesheets>
      <Layers><Layer Id="0" Name="l" SpritesheetId="0" /></Layers>
      <Nulls /><Events /></Content>
      <Animations DefaultAnimation="A"><Animation Name="A" FrameNum="1" Loop="true">
        <RootAnimation><Frame /></RootAnimation></Animation>
        <Animation Name="" FrameNum="0" Loop="false"><RootAnimation /></Animation>
      </Animations></AnimatedActor>)";
    CHECK(parse(document, padded, error));
    CHECK(padded.animations.size() == 2);
    CHECK(padded.find("") == nullptr);
    CHECK(padded.find("A") != nullptr);
}

ISAEC_TEST(anm2_rejects_malformed_markup) {
    Anm2 doc;
    std::string error;
    CHECK(!parse(R"(<AnimatedActor><Info Fps=30 /></AnimatedActor>)", doc, error));
    CHECK(error.find("unquoted attribute") != std::string::npos);

    CHECK(!parse(R"(<AnimatedActor><Info Fps="30")", doc, error));
    CHECK(error.find("unterminated") != std::string::npos);

    CHECK(!parse("", doc, error));
    CHECK(error.find("empty") != std::string::npos);

    CHECK(!parse(R"(<NotAnActor><Animation Name="x" /></NotAnActor>)", doc, error));
    CHECK(error.find("AnimatedActor") != std::string::npos);
}

ISAEC_TEST(anm2_rejects_a_frame_outside_a_timeline) {
    Anm2 doc;
    std::string error;
    const std::string broken =
        replace_once(std::string(kDocument), R"(<RootAnimation>)",
                     R"(<Frame XPosition="0" /><RootAnimation>)");
    CHECK(!parse(broken, doc, error));
    CHECK(error.find("outside a timeline") != std::string::npos);
}

// Opt-in corpus check. Set ISAEC_TBOI_ARCHIVE to a shipped archive; the test
// prints how many ANM2 documents it found and parsed. It asserts only that the
// corpus round-trips: every document that looks like ANM2 must parse, because
// a document the parser skips is a renderer gap.
ISAEC_TEST(anm2_parses_every_document_in_a_real_archive) {
    const char* path = std::getenv("ISAEC_TBOI_ARCHIVE");
    if (path == nullptr || *path == '\0') return;

    Archive archive;
    if (!archive.open(path)) {
        ::isaec::test::fail_now("ISAEC_TBOI_ARCHIVE: " + archive.error());
    }
    usize documents = 0;
    usize parsed = 0;
    std::string first_error;
    usize frames = 0;
    for (usize i = 0; i < archive.entries().size(); ++i) {
        std::vector<u8> bytes;
        if (!archive.decode(i, bytes)) continue;
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const usize start = text.find("<AnimatedActor");
        if (start == std::string_view::npos || start > 64) continue;
        ++documents;
        Anm2 doc;
        std::string error;
        if (!parse_anm2(reinterpret_cast<const u8*>(bytes.data() + start), bytes.size() - start,
                        doc, error)) {
            if (first_error.empty()) {
                first_error = "entry " + std::to_string(i) + ": " + error;
            }
            continue;
        }
        ++parsed;
        for (const Anm2Animation& animation : doc.animations) {
            frames += animation.root_frames.size();
            for (const Anm2LayerAnimation& track : animation.layer_animations) {
                frames += track.frames.size();
            }
        }
    }
    std::printf("anm2 corpus: %zu documents, %zu parsed, %zu root/layer frames\n", documents,
                parsed, frames);
    if (documents == 0) {
        ::isaec::test::fail_now("no ANM2 documents found: the archive or decoding is wrong");
    }
    if (parsed != documents) {
        ::isaec::test::fail_now(std::to_string(documents - parsed) + " documents failed: " +
                                first_error);
    }
}
