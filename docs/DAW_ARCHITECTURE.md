# MillerScore DAW architecture

This document defines the architecture for the MIDI-first DAW vertical slice.
It maps the capabilities already present in the MuseScore codebase, identifies
the boundaries MillerScore should add, and records which ideas are useful from
established DAW projects. It deliberately does not authorize audio recording,
a new plug-in host, a replacement mixer, or a replacement audio engine.

## Decision summary

| System | Decision now | Reason |
| --- | --- | --- |
| Audio engine and device drivers | **REUSE** | The existing graph, real-time worker, WASAPI/ASIO drivers, synthesis and effects already support the required MIDI playback path. |
| Playback event generation | **ADAPT** | Score playback produces `mpe::PlaybackData`; MIDI regions need a separate producer that emits the same engine-facing representation. |
| Transport and playhead | **ADAPT** | Keep one engine clock and one player, but expose it through a DAW-oriented adapter using musical positions. |
| Tempo and meter | **EXTEND** | Reuse the score maps for the current project, while introducing a DAW-facing `TempoMap` interface that can later own independent events. |
| MIDI input/output | **REUSE** | Device ports and MIDI event types already exist; region editing must remain separate from notation input. |
| Mixer, mute and solo | **ADAPT** | Reuse engine tracks and control parameters; map stable DAW track IDs to ephemeral audio `TrackId` values. |
| Instruments and effects | **REUSE** | FluidSynth, MuseSampler and VST3 paths already exist. No new hosting framework is needed for the milestone. |
| Undo/redo | **ADAPT** | Follow the existing transaction model, but do not force DAW objects into the engraving DOM. |
| Project container and save lifecycle | **EXTEND** | Store a versioned DAW payload inside MSCZ and integrate it with the existing dirty/save/autosave lifecycle. |
| Audio regions and recording | **REPLACE LATER** | A durable audio-source/clip subsystem is outside the MIDI milestone and should be designed only when concrete requirements exist. |

`REPLACE LATER` does not mean replacing the whole audio engine. It marks only
the temporary or absent DAW-specific subsystem, such as a future audio-region
source manager.

## Existing playback stack

The current path separates score event generation from engine playback:

```text
Score / engraving DOM
  -> mu::notation::INotationPlayback
  -> muse::mpe::PlaybackData
  -> mu::playback::PlaybackController
  -> muse::audio::IPlayback
  -> AudioContext / TrackChain / Mixer
  -> synthesizer and effects
  -> platform audio driver
```

Important code seams:

- `src/notation/inotationplayback.h` exposes track playback data, live note
  triggering, total duration, tick/second conversion, beat conversion and loop
  boundaries.
- `src/notation/internal/notationplayback.cpp` is the score-specific adapter;
  it converts engraving material and tempo information into playback data.
- `src/engraving/playback/playbackmodel.*`, `playbackcontext.*` and
  `playbackeventsrenderer.*` render notation into MPE events.
- `src/playback/internal/playbackcontroller.*` creates/removes engine tracks,
  owns the notation-to-audio track mapping, applies saved audio settings and
  coordinates the player.
- `muse/framework/audio/main/iplayback.h` is the public engine facade. It can
  add a track from `mpe::PlaybackData`, remove tracks, change source/control/FX
  and aux parameters, observe levels and obtain the shared player.
- `muse/framework/audio/engine/iengineplayer.h` supplies prepare, play, pause,
  resume, stop, seek, loop, duration and position notifications.

Classification: **ADAPT** at the event producer and controller boundary;
**REUSE** below `muse::audio::IPlayback`.

For MIDI regions, add a `DawPlaybackAdapter` that compiles enabled region notes
into `muse::mpe::PlaybackData` per DAW track. It must not manufacture notation
elements or mutate the score merely to make sound. The adapter maintains:

```text
DawTrackId -> muse::audio::TrackId
```

The left side is persisted and stable. The right side belongs to the active
audio context and is rebuilt after project load, engine reset or resource
change. Region edits should rebuild only the affected playback track, not all
score tracks.

## Audio engine

The framework already has the structure expected of a DAW engine:

- `muse/framework/audio/engine/internal/audioengine.*` and
  `audiocontext.*` own engine execution and a playback context.
- `muse/framework/audio/engine/internal/nodes/` contains audio source,
  event, track-chain, FX-chain, mixer and playhead nodes.
- `muse/framework/audio/engine/internal/mixer.*` manages track chains, aux
  sends and master processing.
- `muse/framework/audio/engine/platform/general/generalaudioworker.*` and the
  real-time utilities in `muse/framework/audio/common/` isolate engine work
  from the UI thread.
- `muse/framework/audio/driver/platform/win/` provides WASAPI and ASIO drivers;
  other platform drivers live beside them.
- `muse/framework/audio/engine/internal/synthesizers/` resolves instruments;
  the bundled FluidSynth implementation can render SoundFonts.
- `muse/framework/audio/engine/internal/fx/` and `nodes/fxchain.*` provide the
  existing effect path.

Classification: **REUSE**.

Do not introduce a second callback, device manager, render graph, or thread
pool. DAW model mutations occur on the UI/application thread and publish new
immutable playback data through the existing asynchronous engine API. No QML
object, project container, JSON document, mutex-taking UI service, or mutable
region collection may be accessed by the real-time callback.

## MIDI

There are three useful MIDI layers with different responsibilities:

- `muse/framework/midi/` owns device ports, configuration and common types.
  `miditypes.h` defines tick-based MIDI event structures, a default division of
  480, and a tempo map.
- `src/notationscene/internal/midiinputoutputcontroller.*` connects live MIDI
  input/output to notation workflows.
- `src/importexport/midi/` reads and writes Standard MIDI Files and contains
  import quantization utilities.

Playback-facing expressive events live in `muse/framework/mpe/`, while score
event production is in `src/engraving/playback/`.

Classification: device I/O and basic event types are **REUSE**; the
notation-specific input controller is not the DAW domain model. Region-to-MPE
projection is **ADAPT**. MIDI-file import/export and its quantization algorithms
are implementation references, not a model to couple directly to interactive
editing.

The piano roll edits `MidiNote` values owned by a `MidiRegion`. Notes remain
region-relative; projection adds the region start tick and applies mute/solo
before creating playback events. A note preview may use the existing live
trigger path, but timeline playback must use compiled track playback data.

## Mixer, mute, solo and arm

The engine facade supports per-track `AudioSourceParams`, `ControlParams`,
`AudioFxChain` and `AuxSendsParams`, plus equivalent master controls in
`muse/framework/audio/main/iplayback.h`. The graph implementation is in
`muse/framework/audio/engine/internal/mixer.*` and
`nodes/{trackchain,mixernode,fxchain}.*`.

The existing score UI and persistence path are:

- `src/playback/qml/MuseScore/Playback/mixerpanelmodel.*` and
  `mixerchannelitem.*`;
- `src/playback/qml/MuseScore/Playback/MixerPanel.qml` and its internal mixer
  sections;
- `src/project/iprojectaudiosettings.h` and
  `src/project/internal/projectaudiosettings.*` for source, output, FX, aux and
  solo/mute settings;
- `src/notation/inotationsolomutestate.h` for notation track state.

Classification: **ADAPT**.

`DawTrack` owns the persisted user intent (`mute`, `solo`, `armed`). A
`DawMixerAdapter` derives effective engine state:

- if no track is soloed, all unmuted tracks sound;
- if one or more tracks are soloed, only soloed and unmuted tracks sound;
- `armed` is persisted and visible but has no recording behavior in this
  milestone;
- engine control changes are applied by stable ID through the playback mapping.

The existing mixer remains the rendering backend. A complete replacement mixer
is explicitly out of scope.

## Instruments, plug-ins and effects

Existing source resolution includes FluidSynth and MuseSampler under
`muse/framework/audio/engine/internal/synthesizers/`. VST3 integration lives in
`muse/framework/vst/`: scanning, module repository, instance register, synth
sequencer, audio client, FX processor and editor UI are already implemented.
The playback module's `knownaudiopluginsconfigurator.*` bridges known plug-ins
into the audio path.

Classification: **REUSE**.

The first MIDI vertical slice should choose an existing valid source preset and
persist its existing `AudioInputParams`/resource identity. Do not add JUCE,
Tracktion Engine, Carla, a new VST host, or a second plug-in scanner. Plug-in
delay compensation, sandboxing and new formats may be evaluated only against a
later concrete requirement.

## Transport and playhead

There are three existing levels:

- `muse::audio::engine::IEnginePlayer` is the authoritative engine transport.
- `mu::playback::IPlaybackController` in
  `src/playback/iplaybackcontroller.h` exposes application operations and
  notifications: play/pause/stop/rewind, loop, position-related tempo and beat,
  track state and note audition.
- `src/context/iplaybackstate.h` publishes application-level playback state;
  QML examples include `playbacktoolbarmodel.*` and
  `src/notationscene/qml/MuseScore/NotationScene/playbackcursor.*`.

The lower engine also has `ITransportEventsDispatcher` and a playhead node in
`muse/framework/audio/engine/`, but DAW UI should not bypass the application
controller to manipulate those internals.

Classification: **ADAPT**.

`DawTransport` is a thin context-scoped facade, never a second clock. It exposes
play, pause, stop, seek-by-tick, loop and an observable playhead tick. It obtains
seconds from the shared player and converts through `TempoMap`. Playback state
must be observed rather than simulated by a QML timer. Seeking and region edits
while playing are serialized on the application side and passed to the engine
through its existing interfaces.

## Tempo, meter and tick conversion

The score currently owns authoritative musical maps:

- `src/engraving/dom/tempotimeline.*` maps raw/played ticks and time, including
  tempo behavior needed by score playback.
- the score's `TimeSigMap` supports time signatures and bar/beat-to-tick
  conversion; `src/notation/internal/notationplayback.cpp` calls it for
  `beatToRawTick`.
- `src/notation/inotationplayback.h` exposes `playedTickToSec`, `secToPlayedTick`,
  `secToTick`, tempo-at-tick, beat-at-tick and beat-to-tick operations.
- `src/engraving/types/constants.h` and `fraction.h` define the engraving tick
  basis; `muse/framework/midi/miditypes.h` defines the MIDI-file division.

Classification: **EXTEND** behind a DAW-facing interface.

Introduce `TempoMap` as a domain interface used by arranger, snap, persistence
and `DawTransport`:

```text
tickToSeconds(tick)
secondsToTick(seconds)
tempoAt(tick)
timeSignatureAt(tick)
barBeatToTick(bar, beat)
snapTick(tick, grid)
```

For the vertical slice its implementation delegates to the active master
score, so Score Mode and DAW Mode cannot disagree about BPM or meter. Do not
assume that the MIDI-file division and engraving division are interchangeable;
all imported events are normalized once at the boundary. A future standalone
DAW project may own tempo and meter events, but consumers must not change.

## Undo and redo

The engraving implementation is command/transaction based:

- `src/engraving/editing/transaction/undoablecommand.*`, `transaction.*` and
  `undostack.*` contain the underlying mechanism.
- `src/notation/inotationundostack.h` provides application operations for
  prepare, commit, rollback, undo, redo and transaction merging.
- `src/notation/internal/notationundostack.*` adapts that stack for notation.
- `src/notationscene/qml/MuseScore/NotationScene/internal/undohistorymodel.*`
  demonstrates history presentation.

Classification: **ADAPT**.

DAW mutations are command objects over `DawProject`, with before/after values
or reversible operations keyed by stable IDs. Commands include track creation,
region creation/removal/move/resize, note creation/removal/move/resize,
velocity, quantize and track state. Drag gestures may preview continuously but
commit one command on release. The DAW stack should integrate with the global
Undo/Redo actions, while its commands remain independent of `engraving::Score`
and `EngravingItem`.

If a single operation changes both score and DAW state, add an explicit
composite transaction coordinator. Do not silently nest unrelated undo stacks.

## Project serialization

`mu::project::INotationProject` in `src/project/inotationproject.h` owns file
lifecycle, dirty state, save/autosave, master notation and project audio
settings. `src/project/internal/notationproject.cpp` opens `MscReader`, loads the
engraving project and then reads audio, view and solo/mute state. Its write path
uses `MscWriter`, writes the engraving project, then writes audio and view state.
`src/engraving/infrastructure/mscreader.*` and `mscwriter.*` implement access to
the MSCZ container. The existing archive already demonstrates separate JSON
members such as `audiosettings.json` and `viewsettings.json`.

Classification: **EXTEND**.

Add one versioned container member, proposed as `daw.json`, owned by a
project-layer `IDawProjectSerializer`. The project load order is:

1. load score and construct the master notation;
2. load audio settings and existing view state;
3. read `daw.json`, validate schema and build an inert `DawProject`;
4. bind its `TempoMap` to the active score;
5. asynchronously project DAW tracks into the audio engine.

The save order takes an immutable DAW snapshot on the application thread, then
serializes it into the same MSCZ transaction. The payload contains a schema
version, stable IDs, ordered tracks, MIDI regions and notes, track state and
implemented project-level settings. Unknown fields are ignored; unsupported
future schema versions produce a clear error without corrupting score data.

DAW commands mark `INotationProject` dirty and participate in autosave. Audio
engine `TrackId` values, QML state, pixel coordinates, selections and cached
MPE events are never persisted. Tests must cover missing payload, malformed
payload, save/reopen equality, stable IDs, unknown fields and an unchanged
Score Mode round trip.

## Proposed MillerScore domain boundary

The domain is owned alongside the notation project, not inside QML and not
inside the engraving score:

```text
DawProject
  DawTransport                 adapter to shared playback/player
  TempoMap                     adapter to active score tempo and meter
  DawTrack[]
    MidiTrack
      DawRegion[]
        MidiRegion
          MidiNote[]
    AudioTrack                 reserved, not implemented now
      AudioRegion[]            reserved, not implemented now
```

Suggested module layout:

```text
src/daw/
  dawmodule.*
  idawproject.h
  idawtransport.h
  itempomap.h
  dawtypes.h
  internal/
    dawproject.*
    dawtransport.*
    scoretempomap.*
    dawplaybackadapter.*
    dawmixeradapter.*
    dawprojectserializer.*
    commands/
  qml/MillerScore/Daw/
    models/
    views/
  tests/
```

Ownership rules:

- `DawProject` owns tracks, regions and notes and enforces all invariants.
- A `DawTrack` owns its regions; a region ID never depends on vector position.
- `MidiRegion` owns region-relative notes and contains no QML or engine handle.
- `DawTransport` owns no musical data and delegates to the shared player.
- `TempoMap` is the only tick/time/meter conversion authority visible to DAW
  code.
- `DawPlaybackAdapter` and `DawMixerAdapter` are disposable runtime projections.
- QML models expose snapshots and command entry points; they are not storage.
- The serializer operates on versioned snapshots, not live QObjects.

This separation allows notation and DAW data to coexist while keeping a future
standalone DAW tempo map, audio track implementation or render strategy behind
stable interfaces.

## Vertical-slice data flow

```text
QML gesture
  -> DAW command (validate, snap, execute, one undo entry)
  -> DawProject immutable change notification
     -> QML model updates affected rows
     -> serializer marks project dirty
     -> DawPlaybackAdapter recompiles affected MIDI track
        -> muse::audio::IPlayback track data

shared IEnginePlayer position
  -> DawTransport
  -> TempoMap seconds-to-tick
  -> QML playhead
```

Mute and solo update only derived engine control state. Arm updates persisted
intent only in this milestone. BPM and time signature edits must use the score
editing transaction so both modes observe one map; DAW state listens for that
map change and recomputes ruler/snap/playback projection.

## External architecture references

These projects are references, not dependencies:

- [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) models an
  arrangement as an `Edit`, with tracks, clips, a tempo sequence and transport.
  Useful lesson: keep the persistent arrangement object model above the
  playback graph. Its [feature overview](https://github.com/Tracktion/tracktion_engine/blob/develop/FEATURES.md)
  also makes clear how much unrelated scope a complete engine would import.
- [Zrythm](https://github.com/zrythm/zrythm) separates a project, track list,
  transport/engine, tempo map and typed arranger objects. Useful lesson: regions
  and notes are domain objects selected and edited by arranger views, rather
  than UI delegate state. Its Qt/QML direction is relevant, but its engine and
  project model should not be transplanted.
- [Ardour](https://github.com/Ardour/ardour) centers ownership in `Session` and
  separates routes/tracks, playlists/regions, tempo mapping and real-time
  processing. Useful lesson: stable session ownership and strict separation of
  control-thread changes from process-cycle work. Ardour's
  [`Session` interface](https://github.com/Ardour/ardour/blob/master/libs/ardour/ardour/session.h)
  illustrates the scale that MillerScore should avoid absorbing prematurely.
- [JUCE `AudioProcessorGraph`](https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html)
  demonstrates processor-node graph composition, and
  [`ValueTree`](https://juce.com/tutorials/tutorial_value_tree/) demonstrates a
  hierarchical observable state model. Useful lesson: graph identity and
  serializable document identity are separate. MillerScore already has an audio
  graph and serialization stack, so adding JUCE would duplicate foundations.
- [LMMS](https://github.com/LMMS/lmms) demonstrates a pragmatic separation among
  Song Editor arrangement, Piano Roll note editing and mixer/instrument layers.
  Useful lesson: the same MIDI content can have focused editor and arrangement
  projections without either UI owning the content.

No large external dependency is recommended. The current engine already covers
the vertical slice's audio device, synthesis, graph, mixer, transport and VST3
requirements. External code should be considered only when a measured,
documented gap cannot be filled cleanly through existing interfaces, followed
by license, binary-size, build-time and maintenance review.

## Milestone implementation order

1. Move the current DAW mock state into `DawProject` with stable IDs and command
   tests.
2. Add `ScoreTempoMap` and `DawTransport`; replace QML timers and hardcoded BPM
   or meter with live project values.
3. Implement track/region selection, snapping and undoable region operations.
4. Implement piano-roll note and velocity commands plus basic quantize.
5. Project each MIDI track into existing `mpe::PlaybackData`; validate playhead,
   audition, mute and solo through the existing engine.
6. Add the versioned MSCZ payload, dirty/autosave integration and save/reopen
   tests.
7. Validate the complete three-track create, edit, play, save and reopen flow,
   plus regression-check Score Mode.

Audio recording, new plug-in hosting, advanced automation, Logic project import,
a new full mixer and an audio-engine rewrite remain outside this milestone.
