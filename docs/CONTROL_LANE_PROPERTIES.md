# Piano Roll control-lane properties

Status: implementation complete; unattended build and startup validation
complete (2026-09-26). Attended interaction/audio validation remains pending.

The selector is capability-aware. Per-note properties require an authoritative
value, persistence, Undo/Redo and the corresponding playback contract. Safe
generic CC lanes can also be edited and saved when the selected backend cannot
play them; the lane explains that limitation. SoundFont playback dispatches
108 safe controllers, and the instrument decides which affect its sound. The
20 protected controllers cannot be edited or emitted. Merely selecting or
previewing a lane sends no generic CC event.

Generic CC playback was updated on 2026-09-30. Native Release WAV exports with
MS Basic verify CC7 volume, CC10 pan, CC11 expression and CC64 sustain. The
new route does not add Muse Sounds, VST3, external MIDI or MIDI-file lane
export support. The earlier per-note validation record below remains historical.

| Property | Status | Authoritative state and playback path |
| --- | --- | --- |
| Note pan | Unavailable | The shared MPE event and current synth adapters have no isolated per-note pan contract. Channel CC pan would leak across simultaneous notes. |
| Note velocity | Available for SoundFont/VST3; unavailable for Muse Sounds | `PerformanceOverlay` by persistent note EID, saved in `daw.json`; rendered as `ExpressionContext::velocityOverride`. MuseSampler's note API has no independent velocity field. |
| Note release | Unavailable | The shared note event and SoundFont/MuseSampler adapters do not expose release velocity. |
| Note filter cutoff frequency | Unavailable | No shared per-note cutoff expression exists. A channel CC would affect every overlapping note. |
| Note filter resonance (Q) | Unavailable | No shared per-note resonance expression exists. A channel CC would affect every overlapping note. |
| Note fine pitch | Available | `PerformanceOverlay::pitchOffsetCents` by EID, saved in `daw.json`, applied to the rendered nominal pitch. Range: -1200..+1200 cents. |
| Channel panning | Available | Instrument-scoped `AutomationType::Pan`, stored in `automation.json`, evaluated by the audio control node. Range: -1..+1. |
| Channel volume | Available | Instrument-scoped `AutomationType::Volume`, stored in `automation.json`, evaluated by the audio control node. Range: -60..+12 dB. |
| Channel pitch | Available | Instrument-scoped `AutomationType::Pitch`, stored in `automation.json`, rendered into continuous note pitch curves. Range: -200..+200 cents. |
| Generic MIDI CC | 108 editable; SoundFont main playback | Instrument-scoped `AutomationType::MidiLane` and `cc:N` in `automation.json`; raw 0–127 values. Response depends on the instrument. The other 20 numbers are protected. |

## Interaction contract

- The lane shares the Piano Roll tick scale, horizontal scroll, snapping and
  playhead.
- Note stems use preview while dragging and commit once on release. Escape
  discards the preview.
- Channel points are added on the snapped timeline, dragged vertically, removed
  with right-click, and reset as a single batch edit.
- Note changes use the performance overlay's official Undo command. Channel
  changes use `INotationAutomation::editPoints`, including batch reset.
- Reset removes only the selected note property or the selected channel curve.
- Pitch and generic CC edits update rendered playback events so edits, Undo
  and Redo reach their supported playback paths without reopening the project.

## External behavior references

- [FL Studio](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm)
  groups velocity, pan, release, Mod X/Y and pitch as Piano Roll note
  properties, while documenting that some targets work only when supported by
  the instrument.
- [Ableton Live](https://www.ableton.com/en/live-manual/11/editing-mpe/)
  exposes per-note Pitch, Slide, Pressure, Velocity and Release Velocity for
  MPE-capable instruments.
- [Cubase](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/note_expression/note_expression_assignments_r.html)
  makes Note Expression assignments such as volume, pan and tuning depend on
  the connected instrument's expression support.

These references support the capability-gated design: presenting a property is
not sufficient evidence that every synth can reproduce it independently for
overlapping notes.

## Validation record

- `git diff --check` passes for all control-lane files. Channel pitch uses the
  existing Multibend contract shared by FluidSynth, VST3 and MuseSampler; no
  synth-backend fork is required.
- The complete RelWithDebInfo application build passed (`640/640`) and linked
  `MillerScore5.exe`; QML cache compilation included `PianoRoll.qml`.
- An isolated build linked `engraving_tests`, including the new automation RW,
  note-pitch and channel-pitch cases. The test runner currently stalls before
  GoogleTest discovery (even with `--gtest_list_tests`), so a passing runtime
  result is not claimed.
- A real DAW startup log exposed and led to a fix for an optional QML boolean
  (`channel`) being assigned as `undefined`; all selector entries now declare
  explicit capability flags.
- Mouse/keyboard gestures, screenshots, audible comparison, and save/reopen
  interaction remain attended validation requirements; process state alone is
  not treated as end-to-end validation.
- The final RelWithDebInfo application build completed and linked
  `MillerScore5.exe` (`dev-RelWithDebInfo-20260926-123128.log`). The packaged
  executable exactly matches the installed artifact, SHA-256
  `EA3E139BC98891ECBB0FE3445B095007DAE5F0F479426A5BEDC74CEEB748B02A`.
- A post-build launch reached a responsive native window titled `MillerScore`.
  The newest startup log contains no `PianoRoll.qml`, assignment,
  `ReferenceError`, fatal or exception diagnostics. This is a startup smoke
  test, not a substitute for the attended gestures/audio checks above.
