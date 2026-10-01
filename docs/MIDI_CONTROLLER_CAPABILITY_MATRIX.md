# MIDI controller capability matrix

Status date: 2026-10-01. The generic SoundFont CC route is updated below; unrelated backend capabilities retain their earlier scope. “Preserve” means the new project model can retain the
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

| Message/lane | Project preserve | FluidSynth 2.3.3 adapter | VST3 adapter | MuseSampler >=0.101 | External MIDI |
| --- | --- | --- | --- | --- | --- |
| Note-on velocity | Yes, EID overlay | Yes | Yes | No in main note API | Notes only through FluidSynth path |
| Release velocity | Stable ID only | No route | No route | No API | No authoritative model |
| CC0–127 generic | Yes, `cc:N`; 108 editable, 20 protected | 108 safe CCs dispatched during main playback; instrument may ignore them | Generic lanes not dispatched | Generic lanes not dispatched | Generic lanes not forwarded |
| CC1 Modulation | Yes | Safe generic lane plus existing narrow MPE event | Generic lane unavailable; existing mapping is queried | Existing audition path only; generic lane unavailable | Existing narrow events only; generic lane not forwarded |
| CC7/10/11 | Yes; existing Volume/Pan separate | Generic volume/pan/expression verified with MS Basic; a CC11 lane overrides notation expression on its channels | Generic lanes unavailable | Dynamics uses dedicated API; generic lanes unavailable | Generic lanes not forwarded |
| CC64 Sustain | Yes | Generic sustain verified with MS Basic; lane overrides notation pedal on its channels | Generic lane unavailable; existing pedal mapping unchanged | Dedicated notation pedal API; generic lane unavailable | Existing notation events only; generic lane not forwarded |
| CC66 Sostenuto | Yes | Generic lane dispatched; overrides notation sostenuto on its channels; sound response depends on instrument | Generic lane unavailable | No documented generic lane API | Existing notation events only; generic lane not forwarded |
| CC120–127 | Existing data preserved; editing protected | Never emitted from generic lanes | Generic lanes not dispatched | Generic lanes not dispatched | Never forwarded from generic lanes |
| Pitch Bend 14-bit | Yes, `pitchBend` | Yes, but adapter assumes ±24 semitones | Mapping-dependent | Real per-note pitch path, not channel bend | MIDI1 down-conversion available |
| Channel Pressure | Yes, stable ID | Handler now calls FluidSynth API; shared route pending | Mapping-dependent, unqueried | No | Low-level model supports it |
| Poly Pressure | Stable ID (`polyPressure:*`) | Handler now calls key-pressure API; shared route pending | Event type exists in VST3, adapter missing | No | Low-level model supports it |
| Program + Bank | Stable IDs | Underlying APIs yes; route pending | Program parameter model, route pending | Resource selection is authoritative | 14-bit storage and CC0/CC32 down-conversion fixed and tested |
| RPN/NRPN | Stable semantic IDs; related CC editing protected | Ordered stateful sequences deliberately blocked in generic lanes | Generic lanes unavailable | No generic lane API | No generic lane forwarding |
| 14-bit CC pairs | Catalog pairs MSB/LSB; separate 7-bit curves | Safe bytes dispatched separately; no grouped or atomic 14-bit guarantee | Generic lanes unavailable | No generic lane API | Generic lanes not forwarded |
| Per-note pitch | EID pitch offset | Nominal pitch only; channel bend leaks | No Note Expression contract in adapter | Yes, real event-id pitch | MIDI2 conversion route absent |
| Per-note pressure/slide | Stable IDs only | No safe isolation | No Note Expression contract in adapter | No loaded API | MIDI2 low-level model only |
| Per-note pan/cutoff/resonance | Stable IDs only | No safe isolation | No Note Expression contract in adapter | No loaded API | Not claimed |

## UI capability rule

MuseSampler selects the required API exports by version, adapting 0.101–0.104
and retaining the modern 0.105 path. Optional features still depend on the
installed version. This compatibility correction does not add generic CC-lane
dispatch to Muse Sounds. Proprietary runtime, samples and account entitlements
remain separate from the tested API fixtures.

The generic lane selector distinguishes safe editing and persistence from
backend dispatch. SoundFont lanes explain that the selected instrument may
ignore a controller; Muse Sounds, VST3 and unresolved backends show an explicit
unavailable or unconfirmed reason. Merely selecting a lane sends no generic
controller event. Generic audition/preview, external MIDI forwarding and
MIDI-file lane export are unavailable.

The fuller lane contract must still report record/import/export capabilities,
effective resolution, channel/per-note scope and preview support separately.
Do not infer these capabilities from an editable or dispatched curve.

## Catalog rules

- CC0–31 are MSB entries paired with CC32–63 LSB entries.
- Values remain 0–127 per byte. A theoretical combined pair spans 0–16383, but generic lane playback sends separate safe bytes and does not promise atomic 14-bit behavior.
- CC64–69 are switches; CC96/97 are triggers; CC98–101 are parameter-state
  messages; CC120–127 are discrete channel-mode messages.
- Undefined controls retain numeric identity and are never assigned invented
  semantics.
- Same-tick ordering, MIDI channel and original resolution require the ordered
  event model planned for M2/M3; a curve map alone is insufficient.
