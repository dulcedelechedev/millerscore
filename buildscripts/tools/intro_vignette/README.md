# Intro vignette

The MillerScore intro that plays at startup (`src/appshell/widgets/splashscreen/introvignette.cpp`):
gold light ribbons, the logo's M drawing itself on the jingle's arpeggio, and the wordmark landing
on the hit. It comes from the GameMaker Gold boot animation (gamemaker2 @ 86a529f,
`bin/resources/app/boot/`), with the wordmark changed to "MillerScore" and the spiral "G" mark
replaced by the M of the app icon (`boot/mark.js`).

The app does not run WebGL: the scene is rendered here, frame by frame, and the app plays the frames
in sync with the jingle.

- `boot/`: the scene (`scene.js`, WebGL2 in a worker), its controller (`boot.js`), the mark (`mark.js`)
  and the overlay styles (`boot.css`).
- `assets/boot.json`: cue points of the jingle (sparkles, boom, arpeggio notes, logo hit, fade out, end).
- `assets/jingle.mp3`: the jingle.
- `index.html`: loads the scene in a plain browser (stands in for Electron's `fs`/`path`).
- `capture.mjs`: drives headless Chrome over CDP and saves one PNG per frame, holding every part of
  the scene at the exact time with `GGBoot.freeze(t)`.
- `pack.py`: turns the PNGs into `share/intro/vignette.frames` (JPEG frames with a small index) and
  the jingle into `share/intro/jingle.wav`.

Both files are embedded in the executable as RCDATA by `share/icons/windows_icons.rc`.

## Regenerating

Needs Node 18+ (no npm packages), Chrome, Python 3 and ffmpeg.

```bash
node capture.mjs <framesDir> 30 960 540 1.5
python pack.py <framesDir> 4
```

`960 540` is the window size at 100% scaling, `1.5` the pixel density of the frames (1440x810);
`4` is the JPEG quality (ffmpeg `-q:v`, lower is better). To look at a few moments only:
`node capture.mjs <dir> 30 960 540 1.5 2.8,4.0,5.3`.

## In the app

- It plays in its own native window on its own thread (Windows only), so startup is never held up;
  the frames follow the sound card's position (`waveOutGetPosition`).
- Any click or key skips it.
- It does not play with `--no-intro` / `MILLERSCORE_NO_INTRO=1`, with `--autodrive` /
  `MILLERSCORE_AUTODRIVE=1`, or when Windows animations are turned off. The static splash shows
  instead.
- The log says whether it played: `intro vignette: playing` or `intro vignette: not played: <why>`.
