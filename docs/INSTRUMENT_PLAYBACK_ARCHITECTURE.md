# Instrument and Playback Architecture

Status: design boundary for the MillerScore DAW vertical slice. This document describes the code that exists today and the smallest extensions needed for linked notation/performance editing. It is not a promise of unsupported plug-in features.

## Decision

MillerScore must keep one playback graph, one plug-in host, and one persisted instrument assignment per score instrument. A piano-roll edit is another view of the score/performance data; it must not create a second synthesizer graph or a parallel instrument registry.

The end-to-end path is:

```text
Score Note (stable EID) + persisted performance overrides
        |
        v
engraving::PlaybackModel / PlaybackEventsRenderer / NoteRenderer
        |
        v
muse::mpe::PlaybackData (events, dynamics, setup data)
        |
        v
playback::PlaybackController (InstrumentTrackId -> runtime audio TrackId)
        |
        v
audio::IPlayback / AudioContext / track chain / mixer
        |
        v
ISynthResolver -> MuseSampler | FluidSynth | VST3 instrument
```

The proposed `TrackInstrument` API is a control-plane facade over that path. It is not a DSP abstraction and does not host plug-ins itself.

## Existing chain

### Score and performance projection

`src/engraving/playback/playbackmodel.{h,cpp}` owns `engraving::PlaybackModel`. `processSegment()` walks score `Segment` objects, identifies `ChordRest` content, resolves an `InstrumentTrackId`, and delegates rendering. Repeat playback is already expanded by applying the repeat segment's unrolled-tick offset (`utick - tick`). This remains the source for score-derived playback.

`src/engraving/playback/playbackeventsrenderer.{h,cpp}` (`PlaybackEventsRenderer::renderNoteEvents`) and `src/engraving/playback/renderers/noterenderer.cpp` turn engraving notes and articulations into performance events. `src/engraving/playback/renderingcontext.h` constructs the note arrangement, pitch, and expression contexts.

`muse/framework/mpe/events.h` defines the transport-neutral payloads:

- `NoteEvent`, including arrangement, pitch, and expression contexts;
- `ControllerChangeEvent`;
- `PlaybackData`, including origin events, setup data, dynamics, main stream, and off-stream audition events.

`src/engraving/playback/playbacksetupdataresolver.*` derives the playback setup used to select an appropriate sound.

Classification: **REUSE** the renderer and MPE event contracts. **ADAPT** the render input so the persisted performance layer described in `docs/NOTATION_DAW_SYNC.md` can override velocity, microtiming, sounding duration, and supported expression without changing the notated note. Preserve Note EID provenance through the projection so selection and diagnostics can map rendered events back to notation.

### Audio-track creation and transport

`src/playback/internal/playbackcontroller.cpp` (`PlaybackController::addTrack`) receives `PlaybackData`, maps the score's `InstrumentTrackId` to a runtime `audio::TrackId`, and calls `audio::IPlayback::addTrack()`. Its existing source-selection and `setSourceParams()` paths are also used by the mixer.

`muse/framework/audio/main/iplayback.h` is the public audio control surface: track creation/removal, source parameters, output/effect parameters, and player access. `muse/framework/audio/main/internal/playback.cpp`, `muse/framework/audio/engine/internal/audiocontext.*`, and the engine track-chain/mixer nodes implement it.

Classification: **REUSE**. DAW transport must drive the existing player/transport and shared tick/tempo conversion; it must not schedule a second audio clock. A `DawTrackId` may identify UI/domain state, but it must resolve to the owning `InstrumentTrackId` and then to the runtime `audio::TrackId` held by `PlaybackController`.

### Synthesizer resolution

`muse/framework/audio/engine/isynthresolver.h` and `muse/framework/audio/engine/internal/synthesizers/synthresolver.cpp` select a synth implementation from `AudioInputParams`/`AudioSourceType`. `muse/framework/audio/common/audiotypes.h` defines `AudioResourceMeta`, `AudioInputParams`, `AudioUnitConfig`, `SoundPreset`, `AudioFxParams`, and the supported source kinds (`Fluid`, `MuseSampler`, `Vsti`).

Classification: **REUSE**. Instrument switching should update `AudioInputParams` through the existing playback service; it should not branch on backends inside DAW UI code.

## Backends

### MuseSampler and Muse Sounds

`muse/framework/musesampler/musesamplermodule.cpp` registers `MuseSamplerResolver` for `AudioSourceType::MuseSampler`. `muse/framework/musesampler/internal/musesamplerresolver.*` loads the MuseSampler library, enumerates instrument resources/presets, and creates the synth. `musesamplerwrapper.*` owns setup and rendering; `musesamplersequencer.*` converts MPE notes, dynamics, pedals, articulations, pitch/vibrato, text articulations, preset changes, and supported audition controllers into sampler events.

`src/musesounds/` is primarily the Muse Sounds catalog/install/update experience. It is separate from the runtime sampler resolver and must not become a second playback path.

Classification: **REUSE** sound discovery, loading, presets, MPE interpretation, and rendering. **ADAPT** only the upstream performance projection. Arbitrary DAW CC-lane coverage and sampler multi-output routing are not demonstrated by the current public path and are therefore a **GAP**.

### SoundFont and FluidSynth

`muse/framework/audio/engine/internal/synthesizers/fluidsynth/fluidresolver.*` registers and resolves FluidSynth resources. `soundfontrepository.*`, `fluidsoundfontparser.*`, and `muse/framework/audio/main/generalsoundfontinstallscenario.*` cover discovery, metadata, and installation. `fluidsynth.*` owns the synth and program setup; `fluidsequencer.*` maps MPE events to MIDI/channel events using `soundmapping.h`.

Classification: **REUSE** for the vertical slice. Persist the resource identity plus program/preset/configuration already represented by `AudioInputParams`; do not serialize a second SoundFont choice in DAW data. The existing sequencer now dispatches 108 safe generic channel CCs from persisted lanes during SoundFont playback. MS Basic native WAV tests verify volume, pan, expression and sustain; other controller responses depend on the instrument. Stateful/protected commands, generic audition, external MIDI forwarding and generic lane MIDI-file export remain unavailable.

### VST3 and Kontakt

The detailed implementation audit, Kontakt validation contract, and capability
matrix live in `docs/VST3_COMPATIBILITY.md`.

The generic scanner in `muse/framework/audioplugins/internal/registeraudiopluginsscenario.cpp` discovers candidates, validates them in a subprocess using `--register-audio-plugin`, and records results through `KnownAudioPluginsRegister`. VST-specific discovery is registered in `muse/framework/vst/vstmodule.cpp` and implemented by `vstpluginsscanner.*`, `vstpluginmetareader.*`, and `vstmodulesrepository.*`.

`muse/framework/vst/internal/vstinstancesregister.*` creates instrument instances by resource ID and runtime track ID. `muse/framework/vst/internal/synth/vstiresolver.*` creates `VstSynthesiser`; `vstsynthesiser.*`, `vstsequencer.*`, `vstaudioclient.*`, and `vstplugininstance.*` load, configure, feed, and process the plug-in.

The editor is already routed by `muse/framework/vst/internal/vstactionscontroller.cpp` to `muse://vst/editor?instanceId=...`, with the QML/view implementation in `muse/framework/vst/qml/Muse/Vst/VstEditor*.qml` and `muse/framework/vst/internal/vstview.*` (plus the legacy QWidget dialog).

Kontakt receives no special integration. It may work only when a compatible installed Kontakt VST3 is discovered and loads through this generic path. MillerScore must not claim support for Kontakt licensing, Native Access, NKS, Kontakt's internal library browser, standalone Kontakt, VST2, or Audio Units.

Classification: scanner, instance lifecycle, editor, event/audio client, and opaque state are **REUSE**. Friendly unavailable-plug-in recovery is **ADAPT**. Host preset enumeration, arbitrary parameter automation, multi-output routing, and plug-in delay compensation are **GAP** items; they are not prerequisites for the MIDI vertical slice.

## State and project persistence

`src/project/iprojectaudiosettings.h` associates track input and output settings with `engraving::InstrumentTrackId`. `src/project/internal/projectaudiosettings.cpp` serializes them to `audiosettings.json` through `MscReader`/`MscWriter`; `src/project/internal/notationproject.cpp` writes those settings into the MSCZ project.

For instruments, persisted input state includes `AudioResourceMeta` and `AudioUnitConfig`. For VST3, `muse/framework/vst/internal/vstplugininstance.cpp` stores and restores opaque `componentState` and `controllerState` buffers in that configuration. Output persistence already covers level/balance, effect chain, and auxiliary sends.

Rules:

1. `IProjectAudioSettings` remains authoritative for instrument/resource configuration. DAW JSON must store only the stable link to the score instrument, not a competing copy of the plug-in state.
2. A missing resource remains the desired assignment in persisted settings. Runtime UI reports `Unavailable` and may offer silence or an explicit fallback; it must not silently overwrite the desired resource.
3. A user-initiated switch is staged: resolve/load the candidate, atomically make it active, then mark project audio settings dirty. If loading fails, retain the previous active source and surface the error.
4. Switching input must preserve output/mixer/effect parameters.
5. Runtime instance IDs and `audio::TrackId` values are never persisted.

Classification: **REUSE** MSCZ/audio-settings storage. **ADAPT** load-error reporting and transactional switching. A second DAW instrument-state file is **REPLACE LATER** by removal, not expansion.

## Event and MIDI routing limits

MuseScore's internal event stream is MPE-domain data, not a raw MIDI clip bus. Backends translate it according to their capabilities. `muse/framework/vst/internal/synth/vstsequencer.cpp` emits notes and a limited controller subset; its generated note events currently use event bus 0 and channel 0. MuseSampler and FluidSynth have their own mappings.

Consequences:

- Linked score/performance notes should enter the established `PlaybackData` stream.
- New controller lanes require an explicit semantic MPE/controller contract and a backend capability check; raw UI CC values must not bypass the engine.
- Audition and timeline playback must remain distinct (off-stream versus main-stream events).
- Per-note expression/MPE support varies by backend. Preserve data even when the selected instrument cannot render it, and show capability state rather than discarding edits.

Classification: note routing is **REUSE**; performance override injection and capability reporting are **EXTEND**; unrestricted MIDI routing is a **GAP**.

## Presets, multiple outputs, automation, and latency

### Presets

MuseSampler and FluidSynth resolvers expose backend-specific preset/resource lists. `VstiResolver::resolveSoundPresets()` currently returns an empty list, so MillerScore cannot claim host-level VST instrument preset browsing. A VST plug-in may still manage presets inside its own editor and persist the resulting opaque state.

Classification: built-in backend presets **REUSE**; VST3 host preset browsing **GAP**/**EXTEND** later.

### Multiple outputs

`vstaudioclient.cpp` inspects and activates audio buses and processes active output buses, but the current project/track routing model has no end-to-end persisted route from an individual plug-in output bus to independent MillerScore mixer channels. `vstsequencer.cpp` also targets event bus 0. MuseSampler and FluidSynth follow the normal track output path.

Classification: **GAP**, with routing model work **REPLACE LATER** only after the MIDI slice is solid. Do not expose a multi-output switch that merely folds buses into one track and suggests independent routing.

### Automation

The engine has automation-related primitives, and the VST client can discover parameter information, but there is no proven project-level DAW lane that persists and schedules arbitrary VST3 parameters. The current VST sequencer maps only a small known controller subset.

Classification: velocity and the explicitly supported expression/controller mappings can be **EXTEND**ed. Generic plug-in parameter automation is a **GAP** and is out of scope.

### Latency

The audio engine handles device buffering, but the current VST path does not expose a verified project-wide use of VST3-reported processing latency or a per-track delay-compensation graph. No end-to-end plug-in delay compensation should be claimed.

Classification: **GAP**. Record and measure backend latency first; add graph-level compensation only in a later audio milestone. Until then, warn when a latent plug-in makes live or rendered timing inaccurate.

## Common `TrackInstrument` control plane

The facade belongs above `audio::IPlayback` and project audio settings, ideally in the playback/application layer:

```cpp
enum class InstrumentBackend { MuseSampler, SoundFont, Vst3 };
enum class InstrumentLoadState { Unloaded, Loading, Ready, Unavailable, Failed };

struct TrackInstrumentDescriptor {
    DawTrackId dawTrackId;
    engraving::InstrumentTrackId instrumentTrackId;
    audio::AudioResourceMeta resource;
    audio::SoundPreset preset;
    audio::AudioUnitConfig configuration;
    InstrumentBackend backend;
    InstrumentLoadState loadState;
    InstrumentCapabilities capabilities;
};

class ITrackInstrumentService {
public:
    virtual InstrumentList availableInstruments() const = 0;
    virtual TrackInstrumentDescriptor current(DawTrackId) const = 0;
    virtual async::Promise<void> setInstrument(DawTrackId, InstrumentSelection) = 0;
    virtual audio::SoundPresetList presets(DawTrackId) const = 0;
    virtual async::Promise<void> setPreset(DawTrackId, audio::SoundPreset) = 0;
    virtual Ret openEditor(DawTrackId) = 0;
    virtual async::Channel<DawTrackId, TrackInstrumentDescriptor> changed() const = 0;
};
```

`InstrumentCapabilities` should report only verified abilities such as editor availability, host presets, per-note expression, supported CCs, multiple outputs, parameter automation, and latency reporting. It must be derived from backend/instance metadata, not hard-coded in QML.

Implementation mapping:

- identity: `DawTrackId -> InstrumentTrackId`; runtime resolution remains owned by `PlaybackController`;
- enumeration: existing synth resolver/resource registries and known plug-in register;
- activation: `audio::IPlayback::setSourceParams()`;
- persistence: `IProjectAudioSettings::setTrackInputParams()`;
- VST editor: existing VST action/controller route;
- load status and errors: new facade state, never a second engine;
- output/mixer state: untouched during instrument changes.

## Docked Performance/Piano Roll UX

The notation score remains the primary upper surface. Piano Roll/Performance is a docked lower panel, occupying the current Piano Keyboard area or sharing that lower dock when both tools are enabled. It is not a separate DAW window and does not own transport state.

Required interaction contract:

- The bar/beat ruler reads the same tempo map and tick conversion as notation playback.
- The score, ruler, piano roll, and existing transport display one shared playhead.
- Selection is bidirectional through stable Note EID: score selection highlights and reveals the corresponding roll note; roll selection selects/reveals the notation note. A repeated playback occurrence may highlight the source note, but must not create a second editable note identity.
- The compact toolbar exposes `Grid`, `Note length`, and `Velocity`. Values are editing defaults or performance edits as defined in `docs/NOTATION_DAW_SYNC.md`, not local QML-only state.
- The lower lane initially edits velocity. Future lanes may expose supported CC/expression data through the capability-aware event contract above.
- The panel header may display the current `TrackInstrument`, load state, preset, and an `Open editor` action when supported. It must not instantiate the synth.

The expanded MillerScore mockup further fixes the first production layout:

- the upper notation canvas remains visible and owns the shared vertical
  playback cursor;
- lower tabs are `Piano Roll`, `Envelope`, `Controllers`, `Articulations`, and
  `Mixer`, introduced incrementally as their backend contracts become real;
- a compact inspector at the left of the roll shows the linked score
  instrument, mute/solo/arm intent, volume, pan, selected instrument backend,
  and MIDI channel where the backend exposes one;
- the note grid and velocity lane share bar numbers and the same cursor as the
  score, so measure 10 above is measure 10 below;
- the instrument selector edits `IProjectAudioSettings` through
  `ITrackInstrumentService`; it never creates a QML-owned synth instance;
- status text may report the selected note's pitch, notated duration, effective
  velocity, measure, and beat from the linked projection.

Tabs or controls whose engine support is still a documented `GAP` remain
disabled/hidden rather than storing decorative state that playback ignores.

Do not reproduce an older DAW mockup literally. Use centralized MillerScore design tokens: dark/high-tech neutral surfaces, gold as a restrained accent for focus/playhead/active controls, clear contrast, and consistent states. Avoid hard-coded colors in panel components.

Classification: docking, existing transport, and score selection infrastructure are **REUSE**; EID selection mediation, ruler synchronization, performance toolbar/lane models, and instrument capability display are **ADAPT/EXTEND**. A second transport, clock, piano-roll document, or synth selector is prohibited.

## Classification summary

| System | Classification | Vertical-slice action |
|---|---|---|
| Engraving playback renderer | REUSE / ADAPT | Inject linked performance overrides while retaining score-note provenance |
| MPE `PlaybackData` | REUSE / EXTEND | Use one event stream; add only explicit performance/controller semantics |
| PlaybackController and audio tracks | REUSE | Preserve InstrumentTrackId-to-runtime-track ownership |
| Audio engine, mixer, transport | REUSE | No second graph or clock |
| Synth resolver/source parameters | REUSE | Backend-neutral activation |
| Project audio settings in MSCZ | REUSE / ADAPT | Single authoritative persisted instrument assignment; recover missing resources |
| MuseSampler/Muse Sounds | REUSE | Existing discovery, presets, MPE rendering |
| FluidSynth/SoundFont | REUSE | Existing resources, programs, MIDI translation |
| Generic VST3 scan/host/state/editor | REUSE / ADAPT | Expose through `TrackInstrument`; improve status/error handling |
| Kontakt | GAP | Generic compatible VST3 only; no product-specific promises |
| VST host preset browser | GAP / EXTEND | Later, after a real resolver/persistence design |
| Generic plug-in automation | GAP | Out of scope |
| Independent plug-in output routing | GAP / REPLACE LATER | Requires mixer/routing model work |
| Plug-in delay compensation | GAP / REPLACE LATER | Measure, model, then implement graph compensation |
| Docked Performance panel | ADAPT / EXTEND | Shared EID selection, tempo ruler, playhead, velocity lane |

## Vertical-slice sequence

1. Establish the stable `DawTrackId -> InstrumentTrackId` link and Note EID projection.
2. Feed linked notation plus performance overrides through `PlaybackModel` into the existing `PlaybackData` path.
3. Expose current resource/load/capability state through `ITrackInstrumentService` without changing DSP ownership.
4. Build the docked Performance panel with shared ruler, playhead, selection, and velocity editing.
5. Persist overrides in the linked-performance schema and instrument assignment in existing project audio settings.
6. Validate project close/reopen with MuseSampler and FluidSynth; validate a generic VST3 with state restoration and missing-plug-in recovery.
7. Keep new plug-in hosting, plug-in parameter automation, multi-output routing and latency compensation outside this slice. Generic channel CC playback is implemented for SoundFont; the recording workflow retains its documented limits.

## Acceptance boundaries

For this milestone, success means a score-linked note can be edited in the docked Performance panel, rendered by the existing playback chain, heard through the selected existing backend, and restored with the project. Instrument switching must survive reopen and must fail safely when a resource is absent.

It does **not** mean that every VST3 or Kontakt configuration works, that arbitrary automation is available, that multiple plug-in outputs are routable, or that latent plug-ins are compensated. Those remain explicit gaps rather than implicit promises.
