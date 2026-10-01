# DAW data model

This document defines the first data contract for the Musescore Gold arranger.
It is intentionally small enough for the initial MIDI-only vertical slice while
leaving stable extension points for audio and automation.

## Core types

```text
DawProject
  projectId
  tracks[]
  markers[]
  loopRange?

Track
  id
  name
  kind: midi | audio | instrument | bus
  color
  mute
  solo
  armed
  regions[]

Region
  id
  trackId
  kind: midi | audio
  startTick
  lengthTicks
  sourceOffsetTicks
  name
  color?
  muted

MidiRegion extends Region
  notes[]

MidiNote
  id
  startTick       # relative to the region
  lengthTicks
  pitch           # MIDI note number, 0..127
  velocity        # 0..127
  channel         # 0..15

AudioRegion extends Region
  sourceId
  sourceOffsetFrames
  sourceLengthFrames
  gainDb
  fadeInFrames
  fadeOutFrames

AudioSource
  id
  path
  sampleRate
  channelCount
  frameCount

AutomationLane
  id
  ownerId         # track, region, plug-in, or project
  parameterId
  points[]

AutomationPoint
  id
  tick
  value
  curve
```

Only `midi` tracks, `MidiRegion`, and basic `MidiNote` data are required for the
first implementation. The remaining types reserve vocabulary and ownership
boundaries; they are not an instruction to build the full audio engine now.

## Identity and ordering

Every editable entity has an opaque, stable identifier. UI row positions and
vector offsets must never be persisted as identity. Track display order is the
order of IDs in the project's track list; regions are sorted by `startTick` for
display but retain independent IDs.

## Time model

Musical ticks are authoritative for MIDI regions, editing, snapping, measures,
and beats. The score's existing tempo and time-signature maps convert between
ticks and seconds.

Rules for the first slice:

- `startTick >= 0`
- `lengthTicks > 0`
- note positions are relative to their region
- `note.startTick >= 0`
- `note.startTick + note.lengthTicks <= region.lengthTicks`
- moving a region changes only `startTick`
- resizing a region changes `lengthTicks`; note trimming policy is explicit in
  the edit command

Audio data will additionally require frame-accurate source offsets. A future
audio region therefore keeps both musical placement and source-frame fields;
it must not overload MIDI tick fields with sample positions.

## Command boundary

All mutations pass through commands. The initial command set is:

- `AddTrack`
- `RemoveTrack`
- `RenameTrack`
- `SetTrackMute`
- `SetTrackSolo`
- `AddMidiRegion`
- `MoveRegion`
- `ResizeRegion`
- `RemoveRegion`

Each command validates IDs and ranges, participates in the application undo
stack, marks the project dirty, and emits a single coherent change notification.
Dragging may preview continuously, but commits one undoable command when the
gesture ends.

## UI projection

The domain model does not expose QML objects directly. A DAW page model publishes
read-only track and region roles plus invokable edit commands. The arranger
derives pixels from ticks using a viewport transform:

```text
x = (tick - viewportStartTick) * pixelsPerTick
tick = viewportStartTick + x / pixelsPerTick
```

This keeps zoom and scrolling out of persisted project data.

## Playback projection

The arranger playhead observes the existing global playback position. Conversion
between seconds and ticks uses the active notation tempo timeline. MIDI region
events can later be projected into the playback engine, but the region model
must not own audio-engine track handles; a playback adapter maintains that
mapping.

## Persistence strategy

During the in-memory prototype, opening or closing a project resets DAW data.
Persistence should then be added as a versioned project payload with these
properties:

- explicit schema version;
- unknown-field tolerance;
- stable IDs preserved across save/load;
- paths stored relative to the project when possible;
- migrations tested independently;
- no change to upstream score serialization until round-trip behavior is proven.

The first persisted schema should contain only implemented entities. Placeholder
audio and automation fields should not be written prematurely.

## Invariants to test

- IDs remain stable after reorder, undo/redo, and serialization.
- Regions cannot reference missing tracks.
- A MIDI region cannot be placed on an incompatible track kind.
- Move and resize operations round-trip through undo/redo.
- Tick/second/tick conversion stays within the defined rounding tolerance.
- Project close removes context-scoped DAW state and playback mappings.
