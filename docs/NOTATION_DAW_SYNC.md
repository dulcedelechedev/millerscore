# Notation and DAW synchronization

This document defines the linked `NOTATION + PERFORMANCE` model for
MillerScore. The score remains the source of musical truth. DAW Mode projects
score notes into a piano roll and stores only performance information that does
not belong in conventional notation.

The design replaces neither the engraving model nor the playback engine. It
adds a versioned performance overlay between them.

## Core decision

```text
NOTATION (authoritative score)
  pitch, onset, notated duration, staff, voice, ties, articulations
                    |
                    | project by persistent Note EID
                    v
PERFORMANCE (sparse overrides)
  velocity, microtiming, playback duration, expression, CC, articulation override
                    |
                    | compose at playback-render boundary
                    v
muse::mpe::PlaybackData -> existing audio engine
```

A linked piano-roll note is not a second editable MIDI note. It is a projection
of `mu::engraving::Note` plus an optional `PerformanceNoteOverride`. There is
exactly one owner for every property:

| Property | Owner | Edit kind |
| --- | --- | --- |
| Written pitch/TPC, onset, notated duration, staff, voice, tie | Score | `NOTATION` |
| Printed articulation, dynamic, hairpin, playing technique | Score | `NOTATION` |
| Velocity override, microtiming, sounding duration | Performance overlay | `PERFORMANCE` |
| Per-note expression curve and supported controller events | Performance overlay | `PERFORMANCE` |
| Performance-only articulation add/remove/preference | Performance overlay | `PERFORMANCE` |
| Instrument/source, mixer state | Existing project audio settings / DAW track state | Project/mixer edit |

An operation must declare its edit kind before it starts. A drag must never
quietly change score rhythm and microtiming at the same time.

## Existing primitives to reuse

### Score note and musical position

`src/engraving/dom/note.h` defines `mu::engraving::Note`, an
`EngravingItem` owned by a `Chord`. Its score location is derived from the chord
and segment. `ChordRest::tick()`, `actualTicks()`, `track()`, `staffIdx()` and
`voice()` provide the musical placement used by notation. `Note::pitch()`, TPC
properties, `ppitch()` and `playTicksFraction()` provide written and playback
pitch/duration information.

The score already stores limited note-performance properties:

- `Note::userVelocity()`, `userVelocityFraction()` and `customizeVelocity()`;
- `Note::playEvents()` and `mu::engraving::NoteEvent` in
  `src/engraving/dom/noteevent.h`;
- `Pid::USER_VELOCITY`, `Pid::PLAY`, `Pid::TUNING` and `Pid::CENT_OFFSET`;
- `Chord::playEventType()` distinguishes automatic from user note events.

`NoteEvent` expresses relative pitch, onset and length in thousandths of the
nominal note length. It is serialized as `<Events>` by
`src/engraving/rw/write/twrite.cpp` and read by the versioned `tread.cpp`
implementations.

These facilities remain supported for score compatibility, but they are not a
sufficient DAW overlay: their timing unit is note-relative, the serialized
writer omits several runtime fields, they live inside notation, and changing
them changes the score document rather than a separately controllable
performance layer. The new overlay must compose with them, not overwrite them.

### Persistent identity

All engraving objects can have an `EID` through
`src/engraving/dom/engravingobject.{h,cpp}`. `EIDRegister` in
`src/engraving/infrastructure/eidregister.*` maps an EID to a live engraving
object. `TWrite::writeItemEid()` assigns a missing EID and writes `<eid>`;
`TRead::readItemEID()` restores it.

This is the canonical link for a performance override:

```text
PerformanceNoteOverride.sourceNoteEid -> engraving::Note
```

Never persist a pointer, QML row, note index, segment address, track array
offset, or `(tick, pitch)` tuple as identity. Tick/pitch/voice form a useful
migration fingerprint only.

Clipboard paste is significant: `TRead::readItemEID()` deliberately assigns new
EIDs in paste mode and records the old-to-new mapping with
`ReadContext::registerPastedEID()`. Performance copy/paste must consume an
equivalent mapping; reusing the source EID would make two notes share one
override.

### Playback and MPE

The current score playback route is:

```text
engraving::PlaybackModel
  -> PlaybackEventsRenderer
  -> NoteRenderer / articulation renderers
  -> muse::mpe::NoteEvent
  -> muse::mpe::PlaybackData
  -> muse::audio::IPlayback
```

Concrete seams:

- `src/engraving/playback/playbackmodel.cpp::processSegment()` chooses score
  items, instrument tracks and repeat offsets.
- `src/engraving/playback/playbackeventsrenderer.cpp::renderNoteEvents()` builds
  score-derived events.
- `src/engraving/playback/renderers/noterenderer.cpp` resolves ties, swing and
  note articulations.
- `src/engraving/playback/renderingcontext.h::buildNoteEvent()` constructs the
  MPE event from nominal note context.
- `muse/framework/mpe/events.h` separates `ArrangementContext`
  (nominal/actual timestamp and duration), `PitchContext`, and
  `ExpressionContext` (articulations, dynamics, expression curve and velocity
  override). `PlaybackEvent` also supports `ControllerChangeEvent`.
- `src/engraving/playback/playbackmodel.cpp` publishes changes via
  `PlaybackData::mainStream`; `src/notation/inotationplayback.h` exposes the
  resulting per-instrument data.

This is already the right engine-facing representation. Performance data
should be applied after notation, ties, repeats, articulations and dynamics have
been resolved, but before the final `PlaybackData` snapshot is published.

### Undo and serialization

Score edits use transactions in
`src/engraving/editing/transaction/{transaction,undoablecommand,undostack}.*`,
adapted by `src/notation/inotationundostack.h` and
`src/notation/internal/notationundostack.*`. Project save/load is owned by
`src/project/internal/notationproject.cpp`; the MSCZ container is accessed via
`src/engraving/infrastructure/{mscreader,mscwriter}.*`.

The performance overlay belongs in the existing versioned `daw.json` member.
It must use the same dirty, autosave and atomic full-project save lifecycle as
the score.

## Domain model

Add a domain service below QML, scoped to the active master project:

```text
INotationPerformance
  PerformanceProject
    linkedTracks[]
    overridesByNoteEid
    controllerLanes[]
  ScoreNoteProjection
  PerformancePlaybackComposer
  PerformanceCommandStack / transaction bridge
  PerformanceSerializer
```

Recommended value types:

```text
ScoreNoteRef
  sourceNoteEid
  instrumentTrackId
  staffIndex
  voiceIndex
  scoreTick
  notatedDurationTicks
  writtenPitch
  playbackPitch
  tieRole: none | head | continuation

PerformanceNoteOverride
  sourceNoteEid
  velocity?                 # MIDI semantic 1..127
  startOffsetTicks?         # signed musical microtiming
  playbackDurationTicks?    # positive sounding duration
  expressionPoints[]?       # note-relative 0..1 position/value
  controllerEvents[]?       # note-relative position, CC, value
  articulationPatch?        # add/remove/preferred playback articulations
```

All fields except `sourceNoteEid` are optional. Default values are derived from
notation playback. Removing the final non-default field removes the override
object. The overlay therefore stays sparse and old scores sound exactly as they
do today.

Use integer or rational musical ticks in persisted timing, not pixels or wall
clock time. At render time convert the adjusted start and end independently
through the active tempo timeline. This preserves musical placement across BPM
changes. If sample-accurate offsets are required later, add an explicitly typed
unit rather than changing the meaning of this field.

## Score-note projection

`ScoreNoteProjection` traverses the active `MasterScore`, not an excerpt, and
emits immutable piano-roll rows. It groups rows by existing
`engraving::InstrumentTrackId`/part and exposes staff and voice as filters.
Linked staves and excerpts are views of the master material and must not create
duplicate performance records.

Projection rules:

1. A chord contributes one projected item per playable `Note`.
2. Written onset and length come from score ticks; pitch display chooses concert
   or written pitch according to the current UI setting, while edits use the
   score's existing transposition operations.
3. A normal tie chain is one sounding performance item anchored to the head
   note EID. Continuation notes remain addressable for notation selection but do
   not acquire independent sounding overrides.
4. Grace notes, ornaments, tremolos and arpeggios may render to multiple MPE
   events. Initially the piano roll shows the source note with a generated-event
   indicator; it does not pretend each generated sub-event is an editable score
   note.
5. Muted/non-playing notes remain visible but are marked inactive.
6. Selection identity is the canonical master-note EID.

## Editor integration

The first linked editor is a dockable lower panel beneath the score, not a
replacement document view. It follows the supplied score-plus-piano-roll
mockup while using MillerScore's modern theme tokens:

- tabs separate **Piano Roll** note geometry from **Performance** lanes;
- the bar ruler, playhead, zoom and visible time range stay synchronized with
  the score and playback transport;
- selecting a score note highlights the projected piano-roll note, and
  selecting a piano-roll note selects the same score `Note` through its EID;
- track/staff/voice filters determine which score notes are projected;
- grid and note-length controls apply only to notation edits, while velocity
  and later CC/expression lanes create performance edits;
- closing or resizing the panel never destroys model state.

The existing dock/panel framework and piano-keyboard placement are reused. A
standalone arranger may still exist for project-level navigation, but it must
open the same linked projection rather than owning duplicate MIDI notes.

Most notes do not receive an EID until one is needed. Projection may use a
session-local token for unedited rows. On the first performance edit, assign a
new EID to the master `Note` and replace the token atomically. That edit marks
the project dirty, ensuring the EID is written with the score and the matching
override with `daw.json` in the same save. Merely opening the piano roll should
not dirty the project.

The projection listens to the notation undo stack's `changesChannel()` and
invalidates only affected tick/track ranges. QML receives snapshots; it never
owns `Note*` pointers.

## Two explicit editing modes

### NOTATION edit

NOTATION mode changes what the score says. Piano-roll gestures dispatch
existing notation commands/transactions:

- horizontal move: change score onset/rhythm through score editing APIs;
- vertical move: use the existing pitch/transposition path;
- resize: change notated duration and let score code handle rests, ties,
  tuplets and measure boundaries;
- create/delete: create or delete engraving notes/chords;
- voice change: use `EditVoice` and the established track/voice rules.

The result must be visible in Score Mode and must pass normal score layout and
serialization. DAW code must not mutate `Note`, `Chord`, `Segment` or their
containers directly.

### PERFORMANCE edit

PERFORMANCE mode leaves pitch, score tick, notated duration and voice unchanged.
Gestures write sparse overrides:

- horizontal move changes `startOffsetTicks`;
- right-edge resize changes `playbackDurationTicks`;
- velocity lane changes `velocity`;
- expression/CC lanes update their own points;
- articulation controls create a performance articulation patch.

Performance edits are undoable commands over the overlay and trigger
recomposition only for the affected instrument track/tick range. The score
layout does not run.

The UI must label the active mode and render notation position separately from
performance position, for example a solid score note with a translucent offset
handle. Reset Performance removes overrides without touching notation.

## One user-visible undo history

Users must not have to guess which stack receives Ctrl+Z. Introduce a project
transaction coordinator with entries tagged `Notation`, `Performance` or
`Composite`:

- pure notation operations use the engraving transaction and existing undo
  command;
- pure performance operations use reversible overlay commands;
- operations that create/delete/remap a score note and its override commit a
  composite entry containing both sides;
- a drag creates one entry on release, not one per pointer event.

Undo order is global. Undoing a note deletion restores both the engraving note
with its EID and its performance override. Redo removes both. The coordinator
must expose the same action name and notifications consumed by the current
Undo/Redo UI. Do not nest two independent stacks and hope their indices remain
aligned.

## Playback composition

`PerformancePlaybackComposer` consumes score-rendered events and a read-only
overlay snapshot. It never reads QML or mutable domain collections from the
audio thread.

Composition order:

1. Render notation normally, including dynamics, tempo, repeats, ties, swing,
   ornaments and score articulations.
2. Retain an internal provenance association from each rendered event to its
   source note EID. This metadata is for composition and is not an audio-engine
   identity.
3. For each source note, apply its optional timing/duration patch to
   `ArrangementContext`.
4. Apply velocity to `ExpressionContext::velocityOverride`; merge expression
   points with the score-derived `expressionCurve` using a documented policy.
5. Apply articulation removal, addition and preferred articulation after the
   score parsers but before final actual timestamp/duration calculation where
   possible. Recalculate dependent curves once.
6. Emit supported CC values as MPE `ControllerChangeEvent` entries on the same
   instrument track.
7. Publish a new immutable `PlaybackData` map through the existing main-stream
   update path.

The first implementation should extend the render context or return a small
`RenderedNote { sourceEid, events }` value. Do not identify an event by
timestamp/pitch: repeats, chords, ornaments and unisons make that ambiguous.

The real-time engine receives only completed MPE data. JSON parsing, EID lookup,
curve merging, allocation and reconciliation occur on the application or
playback-preparation thread.

### Override merge semantics

- Velocity is absolute when present and wins over `Note::userVelocity`; absent
  preserves current notation behavior.
- Microtiming offsets the final attack after score-repeat expansion. It applies
  to every occurrence of that source note.
- Playback duration replaces the sounding duration of the resolved tie chain,
  then is clamped to a positive engine-safe minimum. It does not change written
  ties or rests.
- Expression points multiply or offset the notation-derived curve according to
  an explicit lane mode stored in the schema; the initial mode should be
  `multiply` to preserve written dynamics.
- Articulation patches refer to stable MPE articulation names/IDs, not localized
  labels. `remove` suppresses a score-derived playback articulation only; it
  does not remove the printed mark.
- Unsupported CC/articulation data is preserved on load and ignored with a
  diagnostic rather than deleted.

## Structural score-edit policies

EID stability is necessary but not sufficient: several editing operations
replace objects. Synchronization must happen at the command boundary where the
old-to-new relationship is known, never by guessing after the fact.

### Delete and undo

Deleting a source note removes its override in the same composite transaction.
The undo record retains the override and restores it with the note/EID. A load
that finds an unresolved EID keeps the record in an orphan table for diagnostics
and recovery but does not apply it.

### Copy and paste

Copying score notes includes an optional performance fragment keyed by source
EID. Paste uses the old-to-new EID map already created by
`ReadContext::registerPastedEID()` to clone overrides to the pasted notes. The
new note receives a new EID and an independent override. Paste from external
notation without performance metadata simply uses score defaults.

### Split

When one score note becomes several tied notation notes, the performance
identity remains at the attack-bearing first note. Its full sounding-duration
override continues to describe the tie chain. New continuation fragments get no
independent override. If the operation creates genuinely separate attacks, the
first keeps the original override and subsequent notes receive new EIDs with no
override unless the command explicitly offers duplication.

### Merge

When tied fragments merge, move the head-note override to the surviving note
EID. When independent attacks merge into a chord or single note, retain the
surviving/earliest attack's override; conflicting overrides require an explicit
policy or user choice and must not be silently averaged.

### Transposition

Ordinary transposition keeps the note EID, so velocity, timing, duration,
expression and relative pitch curves remain attached. Performance pitch bends
are interpreted relative to the transposed note. Any future absolute-pitch
event must declare that unit in the schema.

### Voice and staff changes

If the editing operation moves the same object and preserves EID, the override
follows automatically. `EditVoice::changeSelectedElementsVoice()` may create
copied notes through `Factory::copyNote`; such replacement commands must report
an old-to-new EID map and migrate the override in the same composite
transaction. Track routing is recomputed from the note's new part/staff/voice;
the override does not persist a stale engine track handle.

### Repeats

`PlaybackModel` expands `RepeatList` segments using
`tickPositionOffset = repeatSegment->utick - repeatSegment->tick`; notation
playback converts between raw and unrolled ticks through `NotationPlayback` and
the tempo timeline. A source EID can therefore create multiple playback
occurrences. Version 1 applies one override to every occurrence. Per-occurrence
editing is deferred; adding it later requires an explicit occurrence key such
as repeat traversal identity, never only an unrolled timestamp.

### Linked staves and excerpts

Canonical ownership is always the master score note. If an excerpt or linked
staff item is selected, resolve through the engraving link system to its master
counterpart before reading or writing the overlay. Only one record is persisted
for the musical note.

## Persistence schema

The linked design should increment the DAW schema and keep performance data in
its own top-level section. Example:

```json
{
  "schemaVersion": 2,
  "performance": {
    "version": 1,
    "noteOverrides": [
      {
        "sourceNoteEid": "base64Eid",
        "velocity": 104,
        "startOffsetTicks": -12,
        "playbackDurationTicks": 420,
        "expression": {
          "mode": "multiply",
          "points": [[0.0, 0.85], [1.0, 1.0]]
        },
        "controllers": [
          { "position": 0.25, "cc": 74, "value": 96 }
        ],
        "articulations": {
          "add": ["tenuto"],
          "remove": ["staccato"],
          "preferred": null
        }
      }
    ]
  }
}
```

Validation rules:

- EIDs must parse and be unique within `noteOverrides`.
- Velocity is `1..127`; CC number/value are `0..127`.
- Durations are positive and offsets have a documented safe bound.
- Curve positions and normalized values are finite and within `0..1`.
- Duplicate points have deterministic last-write or rejection behavior.
- Unknown object fields are preserved when practical; unknown enum values are
  ignored at runtime with diagnostics.
- Engine IDs, pointers, QML selection, expanded-repeat timestamps and cached
  MPE events are never serialized.

Load the score and register all EIDs before resolving the performance overlay.
Save takes an immutable overlay snapshot and writes it in the same MSCZ save as
the score. EID assignment plus first override creation is one dirty operation.
Autosave includes both. Range/selection export must omit unrelated performance
data unless it explicitly remaps and exports the selected note EIDs.

## Migration

Migration is versioned, deterministic and non-destructive:

1. Schema without `performance`: create an empty overlay; existing score
   `USER_VELOCITY` and `<Events>` continue to render normally.
2. Current standalone DAW MIDI notes without `sourceNoteEid`: preserve them as
   legacy independent-region data. Do not silently bind them to score notes.
3. An optional explicit conversion may match by instrument/part, raw tick,
   voice, pitch and duration. It links only a unique exact match, assigns an EID
   through a transaction, shows a conversion report, and leaves ambiguous notes
   independent.
4. Older linked schema versions migrate in memory, retain a migration report,
   and are written as the newest schema only on the next successful save.
5. Unresolved EIDs remain preserved as `orphanOverrides` with their original
   payload. A cleanup command may remove them after confirmation.

Never fall back from a missing EID to the nearest note during ordinary load.
That can attach a loudness or articulation override to the wrong musical event.

## Required change notifications

The overlay needs precise reactions to score changes. Prefer adding a semantic
change stream alongside the existing broad `ScoreChanges` notification:

```text
NoteInserted(newEid, location)
NoteDeleted(oldEid)
NoteReplaced(oldEid, newEid, reason)
NotesPasted(oldToNewEids)
NoteMoved(eid, oldLocation, newLocation)
NotePitchChanged(eid, oldPitch, newPitch)
StructureChanged(affectedTickRange, tracks)
```

This stream is produced by notation commands while lineage is known. It drives
projection invalidation and composite override migration. Pointer-diffing the
score after every transaction is both slow and unable to distinguish copy from
replacement.

## Test matrix

### Identity and persistence

- First performance edit assigns and round-trips the source note EID.
- Save, close and reopen restores every override without changing notation.
- Unknown fields and orphan EIDs survive a round trip.
- Autosave and recovery include the same overlay snapshot as manual save.
- Selection/range export cannot leak unrelated performance data.

### Editing behavior

- NOTATION move/resize/pitch changes the score and keeps applicable overrides.
- PERFORMANCE move/resize changes only sounding position/duration.
- Delete/undo/redo restores note and override together.
- Copy/paste produces new EIDs and independent copied overrides.
- Split tie chain preserves one attack override; merge remaps it once.
- Voice/staff change routes playback correctly even if the note object is
  replaced.
- Transposition retains relative performance expression.

### Playback

- With an empty overlay, generated `PlaybackData` equals current score playback.
- Velocity, timing and duration affect the correct source note in unison chords.
- Overrides apply to every expanded repeat occurrence without duplicate events.
- Tied notes produce one adjusted sounding event.
- Articulation patches compose predictably with printed articulations.
- Tempo changes convert adjusted start/end ticks correctly.
- No overlay lookup, allocation, JSON work or EID registry access occurs in the
  real-time callback.

## Implementation sequence

1. Introduce value types, serializer and validation for the sparse overlay.
2. Add lazy canonical note-EID assignment and master-score projection.
3. Add semantic note-lineage notifications for delete, paste, split, merge and
   voice replacement.
4. Add performance commands and the unified transaction coordinator.
5. Add event provenance and the playback composer; prove empty-overlay parity.
6. Switch the piano roll to score projection and expose NOTATION/PERFORMANCE
   modes.
7. Add migration and the complete structural edit/playback test matrix.

Independent MIDI regions may remain as a separate track type, but they must not
be confused with linked score-note projection. The milestone is complete only
when linked notes survive score editing, undo/redo, repeats and save/reopen
without duplicate ownership or ambiguous identity.

## Implementation status (2026-09-26)

Implemented and runtime-validated:

- `PerformanceOverlay` (`src/engraving/dom/performanceoverlay.h`) owned by the
  `MasterScore`, keyed by note EID; it is the single owner of performance data.
  `daw.json` stores its saved form (`performance.noteOverrides`).
- `ChangePerformanceOverride` (`src/engraving/editing/editperformance.*`):
  performance edits are ordinary undoable commands on the score's undo stack,
  so notation and performance share one history. The command reports its tick
  and staff range, and `PlaybackModel` re-renders only that region.
- `NoteRenderer` composes start offset, sounding duration and velocity after
  ties and swing, converting ticks through the tempo map at the note's repeat
  offset. Every repeat occurrence of a note receives the same override (v1).
- Score projection by part (all staves and voices), tie chains projected once
  at the head note.
- Deleting a note keeps its override keyed by EID; undoing the deletion brings
  the note back with its performance.

Not implemented yet:

- expression curves, controller events and articulation patches;
- orphan cleanup UI and explicit migration reports;
- per-occurrence (repeat-specific) overrides;
- copy/paste of performance data between notes.

