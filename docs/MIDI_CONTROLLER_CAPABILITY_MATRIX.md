# MIDI controller capability matrix

Status date: 2026-09-26. “Preserve” means the new project model can retain the
lane; it does not imply audible playback. Capabilities below describe the real
current adapters, not what the underlying SDK could theoretically do.

## Sources and design decisions

- [MIDI Association MIDI 1.0 Control Change table](https://midi.org/midi-1-0-control-change-messages): canonical CC/RPN names, ranges and channel-mode semantics.
- [MIDI Association MPE v1.1](https://midi.org/mpe-midi-polyphonic-expression): MPE uses MIDI 1.0 channels to vary pitch and timbre independently per sounding note; it is not equivalent to sending one channel CC to a chord.
- [MIDI 2.0 Core Specification Collection](https://midi.org/midi-2-0-core-specification-collection): authoritative entry point for UMP and MIDI 2.0 protocol/controller semantics.
- [VST3 About MIDI](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/About%2BMIDI/Index.html): CC, pressure and pitch map to parameters through `IMidiMapping`; MPE maps to Note Expression.
- [Ableton Live MPE editing](https://www.ableton.com/en/live-manual/11/editing-mpe/): separate per-note Pitch, Slide, Pressure, Velocity and Release Velocity lanes and explicit MPE capability.
- [FL Studio Event Editor](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/automation_eventeditor.htm): recorded/drawn controller events, selection and MIDI import behavior used only as an ergonomics reference.
- [Cubase Note Expression MIDI setup](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/note_expression/note_expression_midi_setup_dialog_r.html): explicit separation/conversion of CC, Pitch Bend, Aftertouch and Poly Pressure between controller lanes and note expression.
- [REAPER User Guide](https://www.reaper.fm/userguide.php): official guide source for multiple CC lanes, continuous versus discrete editing, pitch range and lane management ergonomics.

MIDI Association semantics are authoritative for protocol data. DAW manuals
inform interaction only. Backend support is determined from the exact bundled
APIs and adapters.

## Current backend matrix

| Message/lane | Project preserve | FluidSynth 2.3.3 adapter | VST3 adapter | MuseSampler >=0.105 | External MIDI |
| --- | --- | --- | --- | --- | --- |
| Note-on velocity | Yes, EID overlay | Yes | Yes | No in main note API | Notes only through FluidSynth path |
| Release velocity | Stable ID only | No route | No route | No API | No authoritative model |
| CC0–127 generic | Yes, `cc:N` | Underlying API yes; lane route pending | Only queried mappings; generic route pending | No main-stream generic CC | Low-level output can send MIDI1, route pending |
| CC1 Modulation | Yes | Yes via narrow MPE event | Mapping is now queried; generic lane route pending | Audition only | Via FluidSynth path |
| CC7/10/11 | Yes; existing Volume/Pan separate | CC11 generated for dynamics; generic route pending | Mapping-dependent | Dynamics uses dedicated API | Via FluidSynth path |
| CC64 Sustain | Yes | Yes | Mapping-dependent | Dedicated pedal API | Via FluidSynth path |
| CC66 Sostenuto | Yes | Yes | Mapping-dependent | No documented lane API | Via FluidSynth path |
| CC120–127 | Yes as risky discrete IDs | Handler can receive CC; no safe lane route | Mapping-dependent, unqueried | No | Must require explicit action |
| Pitch Bend 14-bit | Yes, `pitchBend` | Yes, but adapter assumes ±24 semitones | Mapping-dependent | Real per-note pitch path, not channel bend | MIDI1 down-conversion available |
| Channel Pressure | Yes, stable ID | Handler now calls FluidSynth API; shared route pending | Mapping-dependent, unqueried | No | Low-level model supports it |
| Poly Pressure | Stable ID (`polyPressure:*`) | Handler now calls key-pressure API; shared route pending | Event type exists in VST3, adapter missing | No | Low-level model supports it |
| Program + Bank | Stable IDs | Underlying APIs yes; route pending | Program parameter model, route pending | Resource selection is authoritative | 14-bit storage and CC0/CC32 down-conversion fixed and tested |
| RPN/NRPN | Stable semantic IDs | Generic CC route pending | Mapping-dependent | No | Legacy merge truncates IDs; unsafe currently |
| 14-bit CC pairs | Catalog pairs MSB/LSB | Generic route pending | Plugin mapping-dependent | No | Preserve/order route pending |
| Per-note pitch | EID pitch offset | Nominal pitch only; channel bend leaks | No Note Expression contract in adapter | Yes, real event-id pitch | MIDI2 conversion route absent |
| Per-note pressure/slide | Stable IDs only | No safe isolation | No Note Expression contract in adapter | No loaded API | MIDI2 low-level model only |
| Per-note pan/cutoff/resonance | Stable IDs only | No safe isolation | No Note Expression contract in adapter | No loaded API | Not claimed |

## UI capability rule

Every production lane descriptor must eventually report: editable, playable,
recordable, importable, exportable, effective resolution, channel/per-note
scope, preview support and a localized unavailable reason. Until that contract
is connected, a preserved generic MIDI lane must be shown as unavailable for
playback rather than silently advertised as working.

## Catalog rules

- CC0–31 are MSB entries paired with CC32–63 LSB entries.
- Values remain 0–127 per byte; combined values remain 0–16383.
- CC64–69 are switches; CC96/97 are triggers; CC98–101 are parameter-state
  messages; CC120–127 are discrete channel-mode messages.
- Undefined controls retain numeric identity and are never assigned invented
  semantics.
- Same-tick ordering, MIDI channel and original resolution require the ordered
  event model planned for M2/M3; a curve map alone is insufficient.
