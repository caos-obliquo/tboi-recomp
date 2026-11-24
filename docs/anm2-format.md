# ANM2 animation documents

The game stores animations as XML documents inside the ARCH000 containers. This
file records the schema observed in the shipped archives and what
`isaec::data::parse_anm2` does with it. It is authoritative for the parser's
behaviour; addresses are not involved, so no evidence IDs appear here.

## Where the documents live

All containers under `resources/packed` that decode today carry ANM2 payloads.
Measured 2026-11-23 with the archive reader and the parser (entry count in
parentheses):

| Archive | ANM2 documents | Parsed | Root+layer frames |
|---|---|---|---|
| `repentance.a` (4180) | 833 | 833 | 88147 |
| `afterbirthp.a` (10367) | 2249 | 2249 | 193801 |
| `afterbirth.a` (2647) | 1022 | 1022 | 63549 |

The three archives decode 1533, 3472 and 1102 entries respectively through the
mode 0x02 arms described in [arch000-format.md](arch000-format.md). Documents
are recognised by a leading `<AnimatedActor` within the first 64 bytes; every
such payload parsed, so the parser has no observed document it cannot read.

## Schema

```xml
<AnimatedActor>
  <Info CreatedBy="..." CreatedOn="..." Fps="30" Version="2" />
  <Content>
    <Spritesheets><Spritesheet Id="0" Path="fist_tears.png" /></Spritesheets>
    <Layers><Layer Id="0" Name="body" SpritesheetId="0" /></Layers>
    <Nulls><Null Id="0" Name="root" Show="true" /></Nulls>
    <Events><Event Id="0" Name="attack" /></Events>
  </Content>
  <Animations DefaultAnimation="RegularTear1">
    <Animation Name="RegularTear1" FrameNum="15" Loop="false">
      <RootAnimation><Frame ... /></RootAnimation>
      <LayerAnimations>
        <LayerAnimation LayerId="0" Visible="true"><Frame ... /></LayerAnimation>
      </LayerAnimations>
      <NullAnimations>
        <NullAnimation NullId="0">...</NullAnimation>
      </NullAnimations>
      <Triggers><Trigger EventId="0" AtFrame="2" /></Triggers>
    </Animation>
  </Animations>
</AnimatedActor>
```

Frame attributes read by the parser: `XPosition`, `YPosition`, `XPivot`,
`YPivot`, `XCrop`, `YCrop`, `Width`, `Height`, `Delay`, `XScale`, `YScale`,
`Rotation`, `RedTint`, `GreenTint`, `BlueTint`, `AlphaTint`, `RedOffset`,
`GreenOffset`, `BlueOffset`, `Visible`, `Interpolated`. Missing attributes take
the defaults in `include/isaec/data/anm2.hpp`: zero position/crop/pivot,
scale 100, tint 255, delay 1, visible true, interpolated false. Scales are
percentages, not fractions.

`Info@Fps` is the document's timing base; frame `Delay` is counted in those
frames. It is read into `Anm2::fps` and not otherwise applied.

## What the parser tolerates, and why

Shipped documents are not clean, and rejecting them would lose assets that do
render:

- **Blank placeholder animations.** `<Animation Name="" FrameNum="0">` occurs in
  the corpus. They are kept in `animations` but never resolved: `find("")`
  returns `nullptr`.
- **`FrameNum="0"` on named animations.** Present in the shipped data; only a
  negative `FrameNum` is an error.
- **Dangling ids.** Some documents reference layers that they do not define
  (for example, `HeadDown` referencing layer 6 in older archives carried into a
  newer container). The reference is counted in `Anm2::dangling_references`;
  the animation is kept and a renderer skips the unknown track.

Structural damage is still fatal: unquoted or unterminated attributes, an
unterminated comment or declaration, a `<Frame>` outside any timeline, a
`<LayerAnimation>` outside `<Animation>`, a document without
`<AnimatedActor>`, or one without any `<Animation>`. The error names the
construct.

XML comments, `<?xml ...?>`, and `<!...>` declarations are skipped; the five
standard entities (`&amp; &lt; &gt; &quot; &apos;`) are decoded in attribute
values.

## Playback and drawing

`isaec::engine::AnimationPlayer` advances a parsed document. It converts
wall-clock seconds to frames with the document's `Info@Fps`, accumulates the
fractional remainder, and crosses whole frames monotonically. A layer or null
timeline is sampled by walking its keys and subtracting each `Delay` (minimum 1)
until the current frame falls inside a key; past the last key the last key is
held. Root and track transforms compose by addition for position and rotation,
per-mille-style multiplication for the percent scales, per-channel
multiplication for tint, and `visible` is the AND of root, track and the null's
`Show`. A non-looping animation clamps at `FrameNum` and reports `finished()`; a
looping one wraps at `FrameNum`.

Triggers are stable-sorted by `AtFrame`, delivered once when playback crosses
that frame, and drained with `take_triggers`. A looping animation re-arms its
triggers on each wrap, including when a single update spans several whole loops.

`DrawCall` carries rotation (degrees, clockwise), a pivot in source texels, and
an RGB tint. `SdlRenderer` applies them through `SDL_RenderCopyExF` and
`SDL_SetTextureColorMod`; the pivot is the destination of `x`/`y`.

## Runner

```sh
isaec_window anm2 <archive.a> <anm2-entry> <png-entry> [animation|-] \
  [frames] [width] [height] [sheet-archive.a]
```

`-` selects `DefaultAnimation`; `frames 0` runs until the window closes. The
optional last argument reads the PNG from another archive when the document's
sheet is not in the same container.

Measured on a user-owned install (2026-11-24), 90 fixed-budget frames under
`xvfb-run`:

| field | value |
|---|---|
| document | `repentance.a` entry 20, `Idle`, fps 30, `FrameNum` 8, loop, 1 layer |
| sheet | `afterbirthp.a` entry 7205, `resources/gfx/eraser_tears.png` |
| frames / draws / presented | 90 / 90 / 90 |
| animation frame | 4 |
| triggers | 0 |

## What this does not establish

- **No interpolation.** Keys marked `Interpolated` are held, not blended.
  Shipped documents contain interpolation weights; the player ignores them.
- **FPS conversion is not runtime-verified.** The monotonic frame crossing and
  per-key `Delay` sampling were compared against decompiled game code, but the
  exact seconds-to-frames conversion, and therefore the wall-clock speed, is not
  proven against the shipped executable.
- **Sheet lookup is by basename only.** `repentance.a` resolves 0 of 4180 entry
  names, so a path cannot be turned into an entry index there. The runner
  matches the PNG's basename against `Spritesheet@Path` and needs a second
  archive for this install: no document in `repentance.a` has a sheet entry that
  the container can decode in place.
- **Nothing composites to a framebuffer.** The runner draws layers in submission
  order; blend rules, palettes and shaders are not modelled.
- **Archive open is slow on large containers** (issue #130): a 633 MB archive
  takes ~37 s to read, so a two-archive run spends about a minute before the
  first frame.

## Provenance

No game bytes are committed. Fixtures in `tests/test_anm2.cpp` are synthetic and
written for this test; the corpus numbers above are reproduced by running the
suite with `ISAEC_TBOI_ARCHIVE` pointing at an archive from a user-owned install.
