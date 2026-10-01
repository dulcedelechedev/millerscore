# MillerScore — simple store artwork

A light, restrained set built from real MillerScore content: a native export
of the Golden Hour demo score and five actual application captures. The gallery
shows that same demo in notation, the piano roll, CC7 lanes and the mixer.

| File | Size | Placement |
|---|---|---|
| `cover-630x500.png` | 630 × 500 | itch.io cover; real exported notation |
| `01-score-view-1600x1000.png` | 1600 × 1000 | Gallery: notation |
| `02-piano-roll-1600x1000.png` | 1600 × 1000 | Gallery: notes and velocity |
| `03-midi-controller-lanes-1600x1000.png` | 1600 × 1000 | Gallery: actual CC7 curve |
| `04-mixer-1600x1000.png` | 1600 × 1000 | Gallery: mixer during playback |
| `05-cc-pencil.png` | 1600 × 1000 | Gallery: a stepped Pencil stroke in CC7 |

White space, small English labels and clean typography frame the original
content. Application colors and controls are preserved. Safe crops remove the
window title; source images are contained without stretching or
enlargement. Each exported PNG was inspected at its full dimensions.

The Pencil image comes from the updated application. It keeps the drawing
cursor and shows a stepped stroke; Alt+drag draws a continuous curve. The four
earlier gallery captures predate the Pencil button.

The earlier artwork remains in `../itchio/assets`. Reproduction uses
[`build_clean_artwork.py`](../tools/build_clean_artwork.py) and the instructions
in [`CLEAN_ARTWORK.md`](../tools/CLEAN_ARTWORK.md), with separately retained
original sources and crop configuration for the original set. The new Pencil
panel is a crop of a current application capture. The source/build/crop audit is kept
separately; these PNGs contain no source paths or image metadata.

MillerScore is an independent GPLv3 community fork of MuseScore. These assets
do not imply official affiliation with Muse Group.
