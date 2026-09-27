# MillerScore VST3 Compatibility

Status: implementation audit and validation contract for using the existing
MuseScore VST3 host from DAW Mode. This document deliberately distinguishes
code that exists from behavior that has been verified with a particular
plug-in.

## Boundary

MillerScore has one instrument path:

```text
Score notes + linked performance overrides
                |
                v
       mpe::PlaybackData
                |
                v
       PlaybackController
                |
                v
         audio::IPlayback
                |
                v
           VstiResolver
                |
                v
 VstSynthesiser / VstSequencer / VstAudioClient
                |
                v
        installed VST3 instrument
```

DAW Mode must select and control that existing source. It must not instantiate
a plug-in from QML, persist a second instrument assignment, or create another
MIDI/audio graph. Kontakt is treated as a generic VST3 instrument; Native
Access, NKS, Kontakt libraries and licensing remain responsibilities of the
installed Kontakt product.

## Audited capability matrix

| Capability | Current path | Status for DAW Mode | Notes |
|---|---|---|---|
| Default VST3 discovery | `VstPluginsScanner` and Steinberg module paths | **REUSE** | Uses architecture-appropriate system locations. |
| Custom scan folders | `IVstConfiguration::userVstDirectories()` | **REUSE** | Preferences already exposes folders and `Rescan VST3 plugins`. |
| Crash-isolated validation | `RegisterAudioPluginsScenario` | **REUSE** | Each candidate is launched with `--register-audio-plugin`; timeout is 15 seconds. Failed/crashed/missing states are retained in the registry. |
| Instrument enumeration | `VstModulesRepository::instrumentModulesMeta()` via `VstiResolver` | **REUSE** | Existing mixer resource menu groups VST3 resources by vendor. |
| Per-track instance | `VstInstancesRegister::makeAndRegisterInstrPlugin(resourceId, trackId)` | **REUSE** | Runtime `TrackId` and instance IDs are never project identities. |
| Source switching | `audio::IPlayback::setSourceParams()` | **REUSE / ADAPT** | DAW instrument selection must call the same path as the mixer. Preserve output parameters when switching. |
| Note input | `VstSequencer` | **REUSE** | Notes, velocity override, tuning, dynamics, pedal, sostenuto, modulation and pitch bend use the shared MPE stream. Event bus and channel are currently fixed to 0. |
| Native editor | VST action controller and `muse://vst/editor` | **REUSE** | Existing floating editor is addressed by runtime instance ID. DAW UI should expose `Open editor`, not embed or recreate the view. |
| Plug-in state | `VstPluginInstance` `componentState` + `controllerState` | **REUSE** | Opaque binary state is carried in `AudioUnitConfig` and persisted in `audiosettings.json`. |
| Missing plug-in recovery | known plug-in registry + retained resource metadata | **ADAPT** | Show unavailable/failed state and keep desired assignment. Never silently replace Kontakt with another instrument. |
| VST host presets | `VstiResolver::resolveSoundPresets()` | **GAP** | Returns an empty list. Presets chosen inside Kontakt can survive through opaque state, but there is no host preset browser. |
| Arbitrary parameter automation | controller parameter discovery exists | **GAP** | There is no project-level, scheduled, persisted VST parameter lane. Do not present decorative automation controls. |
| Independent plug-in outputs | `VstAudioClient` activates buses | **GAP** | Active output buses are summed into the owning track buffer. There is no persisted bus-to-mixer-channel route. This is not Kontakt multi-out support. |
| Plug-in latency compensation | no end-to-end graph path found | **GAP** | The host does not currently consume processor latency into track delay compensation. Do not claim sample-aligned latent plug-ins. |
| MIDI channel routing | `VstSequencer::buildEvent()` | **LIMITED** | Note events use event bus 0/channel 0 and MIDI mapping queries channel 0. A DAW channel selector would currently be misleading. |

## Existing scanning and loading lifecycle

1. Startup calls `RegisterAudioPluginsScenario::updatePluginsRegistry()`.
2. The VST scanner combines Steinberg default module paths with recursive
   custom-folder results and de-duplicates them.
3. Newly discovered or returned-missing modules receive a persisted
   `Discovered` placeholder.
4. Validation occurs out of process. Results become valid metadata or an
   explicit error state; removed paths become `Missing`.
5. Selecting a VST3 resource updates the track's `AudioInputParams` through
   `audio::IPlayback`.
6. `VstiResolver` checks the module repository, constructs one
   `VstSynthesiser` for the runtime audio track, and registers the plug-in
   instance.
7. Plug-in loading runs on the main thread because some vendors require it.
   Processing remains in the existing audio path.

DAW Mode should observe scan/load state instead of starting a second scan. A
manual rescan action should delegate to the existing registration scenario and
show its progress/result.

## Kontakt-compatible track UX

The production track inspector should expose only real capabilities:

- Instrument selector: `Muse Sounds`, `SoundFonts`, and installed `VST3`.
- Vendor/resource label: for example `Native Instruments / Kontakt` when that
  is the metadata returned by the installed plug-in.
- State: `Scanning`, `Loading`, `Ready`, `Unavailable`, or `Failed`.
- `Open editor`: enabled only after a native editor-capable instance is ready.
- `Rescan plug-ins`: routes to the existing global scan flow.
- Missing assignment: keeps the desired resource name and offers `Locate /
  rescan` or an explicit user-selected replacement.

Until the underlying routing changes, hide or disable:

- MIDI channel selection for VST3 instruments;
- Kontakt output/bus routing;
- host preset browsing;
- arbitrary plug-in automation lanes;
- a latency-compensation indicator that suggests compensation is active.

Volume, pan, mute, solo, sends and insert effects continue to use the existing
track output/mixer settings. Arm may represent future recording intent, but it
must not imply that MIDI recording is already routed to Kontakt.

## State persistence contract

The authoritative assignment remains
`IProjectAudioSettings::trackInputParams(InstrumentTrackId)`. For VST3 it
contains:

- the VST3 resource metadata and stable resource ID;
- `AudioUnitConfig["componentState"]`;
- `AudioUnitConfig["controllerState"]`.

`ProjectAudioSettings` encodes this data in `audiosettings.json` inside MSCZ.
DAW persistence may store a link from `DawTrackId` to `InstrumentTrackId`, but
must not copy these buffers into `daw.json`.

Plug-in UI changes trigger a parameter rescan and publish a new configuration
through `VstSynthesiser::paramsChanges()`. A successful save/reopen test must
therefore verify the sound loaded inside Kontakt, not merely that the Kontakt
resource remains selected.

## Multi-output finding

`VstAudioClient::setUpProcessData()` discovers and activates default main audio
buses. `fillOutputBufferInstrument()` then iterates active output buses and adds
their samples into the single interleaved track output. There is no stable
model for `plug-in bus -> MillerScore mixer channel`, no per-bus fader state,
and no matching project serialization.

Required later design, in order:

1. expose stable bus descriptors from the loaded instance;
2. introduce persisted per-instrument bus routing;
3. allocate engine/mixer nodes for enabled routes;
4. preserve routes when the plug-in is missing;
5. validate Kontakt stereo pairs and offline rendering;
6. only then expose output selection in the track UI.

## Automation and latency findings

The existing notation automation handles track volume and pan. It is not a
generic VST3 parameter automation system. The VST client maps a small MIDI
controller set when the plug-in exposes `IMidiMapping`; this is event
translation, not arbitrary parameter-lane automation.

No call consuming `IAudioProcessor::getLatencySamples()` was found in the
current host path. Before delay compensation is designed, add read-only
diagnostics for reported plug-in latency and measure realtime and offline
rendering. Compensation later belongs in the shared audio graph, never as a
DAW-note timing offset.

## Validation matrix

Compatibility is earned per configuration. At minimum test on Windows x64:

| Case | Required result |
|---|---|
| Clean scan with Kontakt installed | Kontakt appears once under the VST3 vendor grouping; scan completes without blocking normal startup indefinitely. |
| Invalid or crashing VST3 | Validation process fails/times out; MillerScore remains usable and records a diagnosable error state. |
| Assign Kontakt to a score instrument | Existing score notes and piano-roll audition both sound through the same Kontakt instance. |
| Linked performance edits | Velocity, start offset and playback duration affect Kontakt playback without rewriting notation. |
| Native editor lifecycle | Editor opens, raises on a repeated request, closes safely, and project/app shutdown does not leave a live view. |
| Save/reopen | Kontakt remains assigned and restores its loaded instrument/settings from opaque state. |
| Missing after reopen | Assignment remains visible as unavailable; no silent fallback overwrites it. |
| Switch Kontakt to MuseSampler and back | Output/mixer parameters remain intact and no stale Kontakt editor survives. |
| Realtime/offline playback | Notes, pedals and pitch bend are consistent enough for the supported contract; unsupported mappings remain explicit. |
| High-latency plug-in | UI warns that compensation is unavailable; notation/performance data is not destructively shifted. |

Kontakt must be tested in at least one currently supported VST3 release. A
successful generic scanner result alone is not sufficient to advertise Kontakt
compatibility.

## Incremental implementation order

1. Add a playback-layer `TrackInstrument` facade over existing resource lists,
   `IPlayback::setSourceParams()`, project audio settings and VST editor actions.
2. Surface load/error/editor capability state to DAW track components.
3. Connect the DAW selector to the same authoritative `InstrumentTrackId` used
   by notation and mixer.
4. Validate Kontakt note playback and opaque-state round trip.
5. Add missing-plug-in recovery without automatic substitution.
6. Defer host presets, generic parameter automation, multiple outputs and delay
   compensation until their engine and persistence contracts exist.

### Implemented first slice

`DawInstrumentModel` is the first control-plane slice. It enumerates the
existing engine input resources, selects a real `InstrumentTrackId`, resolves
that identity to the runtime audio `TrackId`, switches instruments through
`IPlayback::setSourceParams()`, and opens the existing VST3 editor action. The
normal `PlaybackController::sourceParamsChanged()` subscription remains
responsible for persisting the resulting `AudioInputParams` in
`ProjectAudioSettings`.

Its `Available` state intentionally means that the assignment is present in the
current resource registry. The existing public API does not expose completion
or failure of a vendor plug-in's private initialization, so the model must not
label that state `Ready`. A future playback-layer service should add a genuine
instance load/error channel before DAW UI shows stronger status language.

## Primary implementation references

- `muse/framework/audioplugins/internal/registeraudiopluginsscenario.cpp`
- `muse/framework/vst/internal/vstpluginsscanner.cpp`
- `muse/framework/vst/internal/vstmodulesrepository.cpp`
- `muse/framework/vst/internal/vstinstancesregister.cpp`
- `muse/framework/vst/internal/synth/vstiresolver.cpp`
- `muse/framework/vst/internal/synth/vstsynthesiser.cpp`
- `muse/framework/vst/internal/synth/vstsequencer.cpp`
- `muse/framework/vst/internal/vstaudioclient.cpp`
- `muse/framework/vst/internal/vstplugininstance.cpp`
- `muse/framework/vst/internal/vstactionscontroller.cpp`
- `src/playback/internal/playbackcontroller.cpp`
- `src/playback/qml/MuseScore/Playback/inputresourceitem.cpp`
- `src/project/internal/projectaudiosettings.cpp`
