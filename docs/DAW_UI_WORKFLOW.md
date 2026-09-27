# DAW UI development workflow

The DAW interface is developed as reusable QML presentation components before
they are connected to live project state. This shortens visual iteration while
keeping notation, performance data, playback, and persistence authoritative in
the production models.

## Showroom

Open **DevTools > DAW Showroom** to inspect a representative dense project
without creating a score. The showroom covers transport, library, track
headers, MIDI and audio region appearances, playhead, inspector, piano roll,
and velocity lane.

Showroom data is illustrative only. It must never be serialized, routed to the
audio engine, or used as a second MIDI model. Production components receive
their state from `DawProjectModel` and the existing playback/project services.

## Visual contract

- Dark neutral surfaces establish hierarchy; gold is reserved for focus,
  selection, primary transport state, and other high-value accents.
- Track colors identify content without replacing text labels or selection
  outlines.
- Every icon has a tooltip or adjacent label when its meaning is not universal.
- Controls must remain legible at 100%, 125%, 150%, and 200% Windows scaling.
- Disabled controls describe unavailable engine capabilities; they must not
  imply that MIDI routing, VST automation, multi-output, or recording exists.
- Piano-roll notes remain linked to score notes. Notation and performance edit
  modes must always be visibly distinct.

## Promotion into production

1. Refine interaction and responsive layout in the showroom.
2. Extract the stable presentation element into a reusable component.
3. Give that component explicit properties and signals, without importing the
   project model directly.
4. Bind it from `DawWorkspace.qml` to the real project/playback adapter.
5. Validate empty, loading, unavailable, selected, disabled, and error states.
6. Run the main application incremental build in the background and smoke-test
   both Score and DAW modes.

This boundary allows fast mockup work without creating a mock architecture
that later competes with MillerScore's notation and playback systems.
