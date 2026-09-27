# MillerScore

**Music notation and production in one connected workflow.**

MillerScore is a Windows music notation and production workstation that keeps
the score, playback and performance editing in the same project. Write with a
full notation environment, switch to the DAW view, and work with a piano roll,
velocity, timing, virtual instruments and audio tracks without turning the
score into a separate file.

Developed by **DulceDeLecheDEV**.

> **Early Access:** this release is intended for musicians who want to support
> and follow active development. Please read the known limitations before buying.

## One project, two ways to work

- Switch between Score and DAW without duplicating the music.
- See score instruments as DAW tracks.
- Share transport, playhead and selection between both views.
- Edit velocity, timing and sounding duration without erasing the written score.
- Move notes in the piano roll and update the score through the same undo history.
- Use zoom, snap, multi-selection and overlaid track views.

## Instruments and audio

- Integrated SoundFont playback.
- Discovery and use of compatible VST3 instruments.
- Muse Sounds integration when its separately distributed component is installed.
- WAV, MP3 and FLAC audio tracks synchronized with the score.
- Up to two ASIO input channels recorded to 32-bit float WAV.
- Project-linked mixer, mute/solo and sound assignment.

Plug-ins, sample libraries and Muse Sounds are not necessarily included and
may be governed by their own licenses.

## MIDI performance

The control lane supports note velocity and exposes the complete CC0–CC127
catalog. Safe CC lanes can be selected, edited and saved; stateful or dangerous
controller sequences remain protected.

**Important:** generic CC0–CC127 lanes are not yet routed completely to every
playback engine. Velocity, volume, pan and pitch use established playback
paths. Do not buy this release expecting audible automation for every generic CC.

## Early Access status

Available now:

- The complete notation foundation inherited from MuseScore Studio.
- A score-linked DAW view and piano roll.
- Performance overrides and control lanes.
- SoundFont, VST3 and optional Muse Sounds support.
- WAV, MP3 and FLAC import.
- ASIO recording.
- Black & Gold theme.
- Built-in version checker backed by the public GitHub releases.

Still in development:

- Generic playback for every MIDI CC lane.
- Recording input selection, monitoring and latency compensation.
- Embedding audio media in the project; files are currently referenced externally.
- Complete Read/Touch/Latch/Write automation workflows.
- Wider validation across audio interfaces, plug-ins and display configurations.

## Provisional requirements

- 64-bit Windows 10 or 11.
- 4 GB RAM; 8 GB recommended for larger projects and plug-ins.
- About 2 GB free for the application, plus space for sounds and plug-ins.
- A compatible audio output; an ASIO driver is currently required for recording.

## Open-source license

Your purchase provides the official itch.io build and supports testing,
packaging, fixes and continued development. MillerScore is GPLv3 free software:
you may study, modify and redistribute it under the license.

Corresponding source: <https://github.com/dulcedelechedev/millerscore/releases/tag/v0.1.0>

Repository: <https://github.com/dulcedelechedev/millerscore>

## Independent community project

MillerScore is an independent community fork based on MuseScore Studio and
licensed under GNU GPLv3. It is not an official product and is not affiliated
with or endorsed by Muse Group or the MuseScore project. MuseScore and related
marks belong to their respective owners. Original authorship and third-party
license notices are preserved in the distribution.

Support and issue reports: <https://github.com/dulcedelechedev/millerscore/issues>
