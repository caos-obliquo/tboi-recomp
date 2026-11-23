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

## What is not implemented

- No renderer consumption yet. The loader produces data; texture selection,
  crop rectangles, pivots, interpolation and event dispatch are renderer and
  engine work, tracked by the roadmap's Phase 1/5 and issue #5.
- No `.png` lookup by spritesheet path. Entry names are hashes and the shipped
  manifest does not cover this install's names (see arch000-format.md §11), so a
  caller must supply the decoded sheet.
- No timing evaluation. `Delay`, `Loop` and triggers are parsed, not advanced;
  nothing here decides when the next frame is due.

## Provenance

No game bytes are committed. Fixtures in `tests/test_anm2.cpp` are synthetic and
written for this test; the corpus numbers above are reproduced by running the
suite with `ISAEC_TBOI_ARCHIVE` pointing at an archive from a user-owned install.
