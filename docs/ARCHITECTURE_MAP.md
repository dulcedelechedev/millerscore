# Musescore Gold architecture map

This map describes the upstream baseline and the intended seams for adding the
DAW workspace without destabilizing score editing.

## Application composition

`src/app/appfactory.cpp` is the top-level composition root. It creates the
application and registers modules including `ProjectModule`, `NotationModule`,
and `PlaybackModule`. Each module follows the Muse modularity lifecycle:

1. register global exports;
2. resolve imports;
3. register resources and UI types;
4. create context-scoped services for the active project.

Dependencies are obtained through the modularity container. New DAW services
should follow this pattern rather than using process globals or reaching across
module internals.

## Project and document ownership

| Area | Primary entry points | Responsibility |
| --- | --- | --- |
| Project | `src/project/inotationproject.h`, `src/project/internal/notationproject.*` | File lifecycle, master notation, audio settings, save/load state |
| Global context | `src/context/iglobalcontext.h`, `src/context/internal/globalcontext.*` | Active project, active notation, playback state |
| Notation | `src/notation/inotation.h`, `src/notation/imasternotation.h` | Score-facing interfaces and part/excerpt ownership |
| Engraving | `src/engraving/` | Authoritative score DOM, layout, tempo map, serialization |

The notation project owns the master notation. `IGlobalContext` exposes the
currently active project and notation to context-aware modules. DAW project
state should be owned alongside the notation project, not by QML views.

## Playback path

The current playback path is:

```text
QML controls and commands
        |
        v
IPlaybackController / PlaybackController
        |
        +--> INotationPlayback (score events, tick/time conversion)
        |
        +--> muse::audio::IPlayback (engine tracks, transport, output)
        |
        v
IGlobalContext::playbackState (observable transport state)
```

Important seams:

- `src/playback/iplaybackcontroller.h` provides play, pause, stop, seek, loop,
  tempo, track mapping, and state notifications.
- `src/notation/inotationplayback.h` maps engraving ticks and elements to
  playback positions.
- `src/notation/internal/notationplayback.cpp` uses the engraving tempo
  timeline for tick/second conversion.
- `src/playback/qml/MuseScore/Playback/playbacktoolbarmodel.*` demonstrates how
  QML observes and changes transport state.

The first DAW increment should reuse this transport. A second independent clock
would cause playhead drift and conflicting play/stop state.

## Shell and workspace UI

`src/appshell/qml/MuseScore/AppShell/WindowContent.qml` loads application pages.
The score workspace is
`src/appshell/qml/MuseScore/AppShell/NotationPage/NotationPage.qml` at
`musescore://notation`. Its behavior is backed by
`src/appshell/qml/MuseScore/AppShell/notationpagemodel.*`.

The existing notation page already hosts dock panels, including the legacy
timeline. The legacy timeline is implemented by:

- `src/notationscene/qml/MuseScore/NotationScene/Timeline.qml`
- `src/notationscene/qml/MuseScore/NotationScene/timelineview.*`
- `src/notationscene/widgets/timeline.*`

That timeline is score-oriented and backed by a QWidget adapter. It is useful
as a behavior reference, but the DAW arranger should be a separate QML surface
with its own view model and data model.

## Proposed DAW module boundary

Add `src/daw/` as an application module with the same internal/public split as
the existing modules:

```text
src/daw/
  dawmodule.*
  idawproject.h
  idawtransport.h
  dawtypes.h
  internal/
    dawproject.*
    dawtransport.*
  qml/MuseScore/Daw/
    DawPage.qml
    ArrangerView.qml
    TimelineRuler.qml
    TrackList.qml
    models/
  tests/
```

Initial interface responsibilities:

- `IDawProject`: track and region commands, observable snapshots, and project
  dirty-state integration.
- `IDawTransport`: a thin adapter over `IPlaybackController` and the active
  notation tempo map.
- QML models: transform domain snapshots into roles suitable for delegates;
  they must not become the source of truth.

The DAW page can be introduced as a peer workspace route and surfaced by a
Score/DAW switch. Score mode stays unchanged while the DAW surface matures.

## Data and thread boundaries

- UI and edit commands run on the main thread.
- Audio rendering must not allocate, lock UI mutexes, or traverse QML models on
  the real-time thread.
- Models publish immutable or copy-on-write snapshots to readers.
- Track and region identifiers are stable values, never row indices.
- Timeline positions use musical ticks as the authoritative edit coordinate;
  seconds are derived through the active tempo map for playback and display.

## First vertical slice

The smallest safe slice is:

1. register the DAW module and page;
2. add a Score/DAW workspace switch;
3. render ruler, playhead, track list, and empty arranger lanes;
4. create one MIDI track and one MIDI region in memory;
5. move and resize the region with undoable commands;
6. drive the playhead from existing playback state;
7. keep score editing and legacy timeline behavior unchanged.

Audio clips, plug-in hosting, a replacement mixer, and advanced routing remain
outside this slice.
