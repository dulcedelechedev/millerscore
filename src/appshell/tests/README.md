# Focused DAW tests

This standalone target tests the controller registry, stable identities, read-only protections, gesture validation, automation data isolation, and serialization. It compiles the production catalog projection and executes the piano-roll selector functions with Qt's JavaScript engine. It does not exercise rendered mouse gestures, the full application undo stack, audio output, or the packaged application.

The missing-media tests compile unchanged audio-track storage, rendering, and synchronization methods against injected project, decoder, timeline, and engine services. They check reference retention, English placeholders, silent unavailable clips, and later recovery. Device decoding, asynchronous scheduling, and actual audio output need application integration tests.

The notation tests compile `NotationAutomation::editPoints`, `MasterScore::editAutomationPoints`, and the automation undo command unchanged. A deterministic non-repeating score and transaction service allow all 128 lane gestures, colliding moves, and duplicate writes to be undone and redone. Full score layout, repeat mapping, and the application's UndoStack still need integration tests.

The lane tests compare the unchanged QML interpolation functions with the native automation evaluator, including incoming and outgoing value jumps, saved bend times, and stepped endpoints. They also check Canvas drawing commands and dense selection identities. These source tests do not render Qt Quick delegates or measure the application's frame rate.

The performance edit methods run unchanged with an injected override sink to verify numeric bounds, malformed batch rejection, and the integer minimum overflow case. Score resolution, undo transactions, and playback of those edits need application integration tests.

The Note-reader tests compile the unchanged trailing property dispatch and spanner factory helper with recorded services. They verify that common identity and visibility properties avoid unknown element-type probes, note-anchored spanners still dispatch, other anchors reject, and unknown tags remain rejected. Earlier note-specific branches, the full XML parser, and the engraving object graph require integration tests.

Use a C++20 compiler and Qt 6 Core, Gui, and Qml. Configure from a Visual Studio developer shell on Windows. `MILLERSCORE_TEST_DEPS_ROOT` can point to an existing Muse `_deps` directory containing the GoogleTest, picojson, and utfcpp sources. An installed GoogleTest package and header search paths are also supported.

```shell
cmake -S src/appshell/tests -B build-daw-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt-prefix> -DMILLERSCORE_TEST_DEPS_ROOT=<dependency-source-directory>
cmake --build build-daw-tests
ctest --test-dir build-daw-tests --output-on-failure
```

The MIDI playback tests render actual MPE event maps with the production sampler and TempoTimeline. They check all108 safe controllers,20 protected exclusions, byte quantization, interpolation, pedal hold, part/layer isolation, tempo changes/pauses, expanded versus flattened repeat/volta/jump coordinates, dense curves, and replacement/deletion without removing notes. Repeat coordinates are deterministic fixtures; full Score/RepeatList generation, audio queues and synth output need integration tests.

Ensure the Qt runtime libraries are on the executable search path. The coverage test writes `midi-cc-coverage.json` in the test build directory. It records every registry entry and labels lifecycle checks as unrun; use the test results and application smoke tests to populate a separate verification report. Safe generic MIDI CC lanes are dispatched only to MS Basic/SoundFont; individual instruments may ignore them. Muse Sounds, VST3, external MIDI and protected controller sequences are unsupported.
