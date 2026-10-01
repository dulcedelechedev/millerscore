# MillerScore DAW UI/UX architecture

## Purpose

This document defines the product and presentation architecture for MillerScore's
DAW workspace. It is intentionally based on the current production QML and C++
models, rather than on the visual showroom alone.

The immediate goal is trust: when a user sees a note, region, playhead, mute
state, instrument, or time position, the interface must be showing the real
project and playback state. Visual polish must reinforce that contract, not hide
missing behavior.

This specification does not introduce a second MIDI, transport, mixer, or plugin
system. The DAW UI remains a view and editing surface over score-linked notation,
performance overrides, and the existing MuseScore playback pipeline.

## Product principles

1. **One musical source of truth.** Score notes and their persistent identities
   remain authoritative. The piano roll presents notation and sparse performance
   overrides in explicit, distinguishable modes.
2. **One transport state.** Score, arranger, ruler, and piano roll display and
   seek the same playback position. No view simulates playback with a timer.
3. **One selection context.** Track, region, score note, piano-roll note, and
   inspector selection are coordinated through a shared selection model.
4. **Honest capabilities.** Unimplemented recording, automation, routing, or
   plugin functions are hidden or clearly disabled. Placeholder data must never
   look like project data.
5. **Progressive disclosure.** The default workspace exposes the common compose,
   edit, and playback path. Advanced routing and plugin state appear in the
   inspector when relevant.
6. **Gold is an accent.** Gold communicates focus, selection, playhead, and brand
   identity. It is not the default fill for every region or surface.
7. **Keyboard-first is a requirement.** Every editing action must be reachable
   and understandable without pixel-perfect pointer input.

## Current-state audit

### 1. Piano-roll notes can render outside the useful viewport

`PianoRoll.qml` currently uses a fixed pitch range of MIDI 36 through 84. Notes
above C6 can receive negative vertical coordinates and notes below C2 can be
clipped below the grid. This explains the observed high notes that appear outside
or detached from the editor.

The editor must instead derive its initial pitch viewport from the selected
track/region:

- calculate the lowest and highest visible note;
- add one octave of padding when possible;
- keep a minimum useful window of 36 semitones;
- clamp the result to MIDI 0 through 127;
- expose vertical zoom, scroll, and **Fit notes**;
- preserve manual scroll/zoom until the user requests auto-fit again.

The piano keyboard, pitch labels, note grid, hit testing, and note delegates must
all use one shared pitch-to-Y transform.

### 2. The editor is not scoped to the selected musical context

The current note refresh requests all projected score notes. A region selected on
one track can therefore be shown beside unrelated notes from other instruments,
staves, or voices.

The editor contract must contain:

- selected `InstrumentTrackId`;
- selected region or musical tick range;
- optional staff and voice filter;
- selected persistent note EIDs;
- edit domain: notation or performance.

If no region exists, the editor may follow the current score selection, but it
must say so in its breadcrumb. It must never silently fall back to displaying the
whole score.

Recommended header:

`Violin I  /  Region: Bars 9-12  /  Notation`

### 3. Arranger and piano roll do not share timeline geometry

The current presentation assumes 480 ticks per beat, a constant time signature,
and fixed content extents in several places. This fails with denominator changes,
time-signature changes, pickup measures, long scores, and regions outside the
hard-coded range.

Introduce a shared presentation-facing `TimelineGeometryModel`. It does not own
tempo or meter; it adapts the real score/project timing services and exposes:

- `tickToX(tick)` and `xToTick(x)`;
- measure and beat grid lines for the visible range;
- measure number, beat, and subdivision labels;
- current time-signature segments;
- snap targets and snap descriptions;
- content start/end ticks;
- loop range;
- exact playhead tick;
- visible tick range and horizontal zoom.

The arranger ruler, regions, piano-roll ruler, grid, playhead, and pointer tool
must consume the same geometry. This removes drift between what the user hears,
sees, and edits.

### 4. Playback feedback is approximate instead of authoritative

The present QML infers a tick from normalized playback position and uses a short
timer as a proxy for whether playback is active. Clicking the ruler updates local
presentation state without guaranteeing a seek in the playback controller.

Replace these approximations with a single playback presentation adapter over the
existing controller:

- exact project tick and formatted bar/beat/subdivision;
- real `isPlaying`, `isPaused`, and `isSeeking` states;
- `seekToTick()` routed to the actual transport;
- loop, metronome, and count-in states from their existing services;
- throttled visual updates only at the UI boundary, never simulated movement.

The playhead must move simultaneously in the arranger and piano roll while audio
plays. Seeking in either ruler must update playback and every visible playhead.

### 5. Region previews do not yet communicate musical content

Production regions currently suppress their content preview. A MIDI region should
show a lightweight, clipped projection of its linked score notes. Preview notes
must be normalized to the region's time span and the track's useful pitch range;
full piano-roll coordinates must never be reused inside a small clip.

Region appearance states:

- normal: track color fill with readable title;
- selected: track fill plus gold focus/selection outline;
- muted: reduced chroma and opacity, with a mute glyph;
- unavailable instrument: warning indicator without hiding content;
- linked notation changed: preview refreshes without replacing the region;
- dragged/resized: translucent original position plus snapped destination.

### 6. Editing affordances promise more than they perform

The current notation mode looks like an editor while disabling note drag.
Performance mode allows limited horizontal movement, and visible resize handles do
not yet provide the complete interaction suggested by their appearance.

The UI must define an explicit editing state machine:

| Dimension | States |
| --- | --- |
| Tool | Pointer, Pencil, Eraser |
| Edit domain | Notation, Performance |
| Selection | None, Track, Region, Note, Multiple notes |
| Transport | Stopped, Playing, Paused, Seeking |
| Instrument | None, Scanning, Loading, Ready, Missing, Failed |

In **Notation** mode, pitch, rhythmic position, and written duration edits invoke
score commands and participate in normal score undo/redo.

In **Performance** mode, the original notation note remains visible as a subdued
anchor. The performed note shows timing offset, playback duration, and velocity.
Dragging or resizing changes only the sparse performance override. Reset removes
the override and visibly returns to the notation anchor.

Required direct-manipulation feedback:

- cursor reflects the active tool and resize edge;
- hover and focus states are distinct from selection;
- drag tooltip shows pitch, bar/beat, duration, offset, and velocity;
- snapped destination is previewed before commit;
- `Escape` cancels the gesture;
- `Delete` deletes in notation mode or resets the selected override in
  performance mode, with labels that make the distinction clear;
- multi-selection and undo/redo are supported consistently.

### 7. Selection state is duplicated between views

Track name, color, mute, solo, and arm values are currently copied into workspace
properties. The real instrument model can consequently point at a different track
than the arranger selection.

Create one `DawSelectionModel` containing stable identifiers, not copied display
values:

- selected DAW track ID;
- selected `InstrumentTrackId`;
- selected region ID;
- selected note EIDs;
- active edit domain;
- active inspector page.

Track headers, arranger, piano roll, score selection bridge, and inspector bind to
this model. Display values come from their authoritative models.

### 8. Empty, loading, and error states are not honest enough

An empty score currently receives a synthetic MIDI track and region. This makes
the workspace appear functional while obscuring the relationship with the score.

Required states:

- **Empty score:** “Add an instrument to start” with an action that invokes the
  existing score instrument flow.
- **Score has instruments, no region selected:** show derived tracks and explain
  how to select a range or create a linked region.
- **No notes in selected region:** show an empty piano-roll grid with the selected
  instrument and range, not fake notes.
- **Instrument scanning/loading:** progress or indeterminate status, cancellable
  only if the underlying service supports cancellation.
- **Missing VST/resource:** keep notation visible, show the missing resource and
  offer rescan, replace, or locate actions.
- **Plugin failed:** preserve the assigned resource and state, show a concise
  failure with expandable technical details.
- **Saving/restoring:** avoid modal blocking; expose non-destructive status in the
  project header/status area.

Record-arm controls should remain hidden until a real recording path exists.

## Information architecture

```text
+-----------------------------------------------------------------------+
| Global app bar: transport | position | tempo | meter | loop | click   |
+-----------------------------------------------------------------------+
| DAW tools: pointer pencil eraser | snap | drag | zoom | fit | panels  |
+------------------+------------------------------------+---------------+
| Track list       | Arranger + shared ruler            | Inspector     |
| color/icon/name  | linked regions                     | selection-    |
| instrument       | playhead + loop range              | aware content |
| M / S / meter    |                                    |               |
+------------------+------------------------------------+---------------+
| Editor dock: Piano Roll | Performance | Automation                    |
| breadcrumb | shared ruler | note grid | lanes                         |
+-----------------------------------------------------------------------+
```

There should be one global transport. The DAW toolbar is contextual and should
not repeat global playback controls already visible in the application shell.

### Track list

- resizable width with a saved per-workspace preference;
- track color, semantic icon, editable name, and instrument subtitle;
- mute/solo using the existing mixer controls and accessibility behavior;
- compact activity meter fed by the real playback output;
- clear selected, muted, solo-isolated, missing-instrument, and loading states;
- no record arm until recording is implemented.

### Arranger

- ruler remains visible while vertically scrolling;
- regions are clipped to their lanes and never overlap track headers;
- double-click on empty space creates a linked region only when that operation has
  a defined score meaning; otherwise it opens an explicit creation menu;
- horizontal and vertical scrollbars appear when needed;
- empty canvas gives a short next action instead of remaining unexplained.

### Inspector

The inspector changes with selection:

- track: name, color, instrument, volume, pan, routing summary;
- region: linked score range, start/end, loop, mute, color inheritance;
- notation note: written pitch, position, duration, voice, articulation;
- performance note: notation anchor plus velocity, offset, playback duration, and
  reset action;
- multiple notes: shared editable values and mixed-value indicators.

At narrow widths it becomes a drawer instead of disappearing without an access
path.

### Editor dock

- vertically resizable and collapsible;
- remembers height per workspace;
- uses the same horizontal timeline/zoom as the arranger when link-scroll is on;
- contains a pitch keyboard and note grid with synchronized vertical scrolling;
- supports resizable lower lanes for velocity and later expression/CC data;
- clearly distinguishes Note, Performance, and Automation domains.

## Responsive behavior

Replace fixed layout disappearance with priorities and controlled overflow:

- **Wide (>= 1600 px):** track list, arranger, inspector, and editor all visible.
- **Desktop (1200-1599 px):** narrower inspector and compact toolbar labels.
- **Compact (900-1199 px):** inspector becomes a drawer; low-priority toolbar
  actions move to overflow.
- **Below 900 px:** supported for review and emergency use, not as the primary
  production layout; track list can collapse to icons and the editor occupies the
  available width.

Use splitters with minimum/maximum constraints instead of fixed 226/238/290 pixel
panels. Persist sizes as UI preferences, never in the score document.

## Visual system

### Semantic tokens

Add a QML theme extension such as `MillerScoreDawTokens.qml`, backed wherever
possible by the existing `ui.theme` semantic colors. The DAW components must not
contain scattered hexadecimal colors or compare color strings.

Required tokens:

- surfaces: workspace, panel, editor, raised, hover, pressed;
- text: primary, secondary, disabled, inverse;
- lines: separator, grid minor, grid beat, grid measure;
- state: focus, selection, playhead, success, warning, error;
- track palette: accessible categorical colors with dark/light variants;
- spacing: 4, 8, 12, 16, 24, 32;
- radii: small, medium, large;
- control heights: compact, normal, prominent;
- motion: instant, fast, normal, with reduced-motion variants.

The semantic structure in the sibling GameMaker Gold project is a useful pattern:
labels, separators, window/content/workspace surfaces, control states, status
colors, metrics, and reduced motion are centralized. The concept should be ported
to QML; its platform-specific assets must not be copied.

### Color roles

- neutral dark surfaces establish hierarchy;
- gold marks the playhead, keyboard focus, selected outline, and primary brand
  accent;
- track colors identify instruments/regions and remain distinguishable in common
  color-vision deficiencies;
- error/warning/success retain their established semantic colors;
- selection must include outline/shape/state, never color alone;
- light and high-contrast themes must remain possible.

### Typography and control metrics

- body and control labels: 12-13 px equivalent at 100% scale;
- metadata only: 10-11 px equivalent;
- monospaced numerals only for time, bar/beat, tick, and parameter readouts;
- minimum desktop hit target: 28-32 px, with larger primary transport targets;
- text elides safely and exposes the full value in a tooltip;
- layouts must survive long translations and 100%, 125%, 150%, and 200% scaling.

Icons should use MillerScore/MuseScore's existing `IconCode` and owned or
compatible SVG assets. Do not bundle Apple SF Symbols or SF fonts for the
cross-platform application.

## Accessibility and keyboard navigation

The DAW workspace needs the same navigation framework used by the existing
MuseScore UI rather than standalone `MouseArea` controls.

Navigation order:

1. global transport;
2. contextual DAW toolbar;
3. track headers;
4. arranger regions;
5. inspector;
6. editor toolbar;
7. piano-roll notes and controller lanes.

Requirements:

- visible keyboard focus ring using the theme focus token;
- accessible name, role, state, and shortcut for each control;
- Mute and Solo announce both track name and current state;
- regions announce name, track, start, end, and selection state;
- notes announce pitch, musical position, duration, velocity, and edit domain;
- plugin states announce scanning, loading, ready, missing, and failed changes;
- pointer gestures have keyboard equivalents;
- reduced motion disables decorative transitions while preserving feedback;
- high-contrast mode does not depend on translucent overlays.

## Performance and rendering

Large orchestral scores cannot create one QML delegate for every grid line and
every note in the full project on each selection update.

Recommended boundary:

- C++ list models filter notes by selected track/range and visible tick/pitch
  viewport;
- grid rendering is separated from interactive note delegates;
- only visible notes receive QML interaction delegates;
- selection changes update roles/rows instead of re-projecting the entire score;
- region previews use cached lightweight geometry;
- expensive note projection never scans the score from QML;
- UI playhead updates are frame-throttled while retaining exact engine position.

A custom `QQuickItem` or scene-graph renderer can be introduced if profiling
shows that the virtualized list approach is insufficient. It is not required
before correctness and model scoping are fixed.

## Preview and approval workflow

The current DAW Showroom is valuable but can drift because it uses parallel
static components. Its next version should compose the production presentation
components with fixture model implementations.

Required fixture scenarios:

1. empty project;
2. orchestral score containing very high and low notes, long names, meter/tempo
   changes, muted/solo tracks, and dense regions;
3. instrument scanning, loading, missing, and failed states;
4. mixed notation/performance overrides;
5. large project stress fixture.

Review presets:

- 1024 x 768, 1366 x 768, 1920 x 1080, and ultrawide;
- dark, light, and high-contrast themes;
- 100%, 125%, 150%, and 200% scale;
- English and a long-string localization fixture;
- normal and reduced motion.

Add an optional UI review overlay showing component bounds, spacing, focus order,
hit-target dimensions, clipped text, and the active semantic color tokens.

Approval loop:

1. change a production presentation component;
2. open it through fixture-backed Showroom scenarios;
3. capture the defined viewport/theme presets;
4. review hierarchy, clipping, state accuracy, and accessibility;
5. approve the visual result;
6. connect or adjust production behavior using the same component.

This gives visual previews without allowing a separate mock UI to become the
product architecture.

## Implementation sequence

### P0 - Correctness and trust

1. Replace the inferred transport/playhead state with the real playback adapter.
2. Scope the piano roll to the selected instrument track and region/range.
3. Replace the fixed pitch window with dynamic viewport and **Fit notes**.
4. Connect arranger selection to the authoritative instrument track selection.
5. Remove synthetic tracks/regions and hide controls for unavailable features.
6. Add honest empty, loading, missing-resource, and error states.

### P1 - Core interaction

1. Introduce the shared selection and timeline presentation models.
2. Complete Pointer, Pencil, and Eraser behaviors.
3. Implement notation-aware create/move/resize/delete commands.
4. Implement performance move/resize/velocity/reset with visible notation anchors.
5. Add score-derived, clipped MIDI previews to regions.
6. Add resizable track, inspector, and editor panels with persisted UI state.

### P2 - Design system and accessibility

1. Centralize DAW semantic tokens and remove hard-coded colors.
2. Reuse existing accessible MuseScore controls for mixer and transport actions.
3. Implement navigation panels, shortcuts, focus states, and screen-reader labels.
4. Validate responsive layout, localization, high contrast, and display scaling.

### P3 - Preview fidelity and scale

1. Rebuild Showroom from production components and fixture models.
2. Add visual review presets and overlay diagnostics.
3. Virtualize grid/note rendering and cache region previews.
4. Profile dense orchestral projects and set frame/update budgets.

## Acceptance criteria for the next UI milestone

- A playing score shows one synchronized moving playhead in arranger and piano
  roll, and either ruler can seek the real transport.
- Selecting a region shows only the linked instrument and musical range.
- Notes from MIDI 0 through 127 remain reachable; **Fit notes** frames the selected
  content without clipping.
- Notation and Performance modes are visually distinct and explain the effect of
  every edit.
- Track, region, and note selection cannot disagree with the inspector.
- No fake track, region, record state, or plugin success state is displayed.
- Missing/loading/failed instruments produce actionable non-destructive feedback.
- Controls remain legible and usable at 1024 x 768 and at 200% Windows scaling.
- All primary actions work by keyboard and expose accessible names/states.
- Dark, light, and high-contrast presentations use semantic tokens without
  hard-coded-color regressions.
- Showroom screenshots exercise the same presentation components used by the live
  workspace.

## Explicit non-goals for this stage

- audio recording;
- a new mixer or plugin host;
- advanced automation editing;
- a `.logicx` importer;
- replacing the existing audio engine;
- copying the complete interaction model or visual language of another DAW.

The UI may reserve clear extension points for these features, but it must not show
controls that imply they already work.

## Implementation status (2026-09-26)

Resolved from the audit above (runtime-validated):

| Audit item | Resolution |
| --- | --- |
| 1. Notes outside the viewport | Dynamic pitch range with **Fit**; one pitch transform for keys, grid and notes |
| 2. Editor not scoped | Piano roll shows exactly the selected part (every staff and voice) |
| 3. No shared geometry | `DawTransportModel.measures` feeds one `DawTimeGrid` used by both rulers and grids; arranger and roll bars align vertically |
| 4. Approximate playback | Exact tick from the shared player; rulers seek the real transport; no timers |
| 5. Region previews | Clips are the bars where a part has notes, with a preview of the real notes |
| 6. Misleading affordances | Notes/Performance and Select/Draw are explicit; Draw explains refusals; Performance edits are heard and undoable |
| 7. Duplicated selection | `DawTracksModel` is the only track selection; inspector and roll follow it |
| 8. Dishonest empty states | No fabricated lanes; "Add instruments" when a score has none |

Also delivered: one transport (the application's), the Score/DAW switch with
standard segmented controls, notation-only panels closed in the DAW view, the
Mixer's own searchable sound picker in the inspector, and semantic tokens in
`DawTheme.qml` (Okabe-Ito based track palette, gold reserved for the playhead).

Still open: resizable/persisted panel sizes, full keyboard traversal audit,
high-contrast and light-theme review, a Showroom rebuilt from production
components, and velocity/controller lanes.

