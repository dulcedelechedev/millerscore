# MillerScore

**MuseScore, but with a built-in DAW.**

MillerScore is a Windows music notation and production workstation that keeps the score, piano roll, performance controls, virtual instruments, audio tracks, transport, and mixer connected in one project.

> **Early Access — v0.1.0**  
> MillerScore is under active development. Review the limitations below before relying on it for production work.

## Highlights

- Switch between Score and DAW views without duplicating the music.
- See score instruments as DAW tracks.
- Edit pitch, velocity, timing, and sounding duration in the piano roll.
- Share transport, playhead, selection, and undo history across both views.
- Use SoundFont playback and compatible VST3 instruments.
- Use Muse Sounds when its separately distributed component is installed.
- Synchronize WAV, MP3, and FLAC audio tracks with the score.
- Record up to two ASIO input channels to 32-bit float WAV.
- Work with a project-linked mixer, mute/solo, and sound assignment.
- Check for new MillerScore releases from inside the application.

## MIDI controllers

The control lane exposes note velocity and the complete CC0–CC127 catalog. Safe controller lanes can be selected, edited, and saved; stateful or dangerous controller sequences remain protected.

The current test build dispatches the 108 editable generic CC lanes during main playback with MS Basic/SoundFont; the 20 protected numbers are never sent. Native WAV-export checks with MS Basic measured CC7 (volume), CC10 (pan), CC11 (expression), and CC64 (sustain). Other safe controllers depend on the selected instrument and may have no audible effect.

Earlier builds may preserve these lanes without this SoundFont playback route. Check the release notes for the ZIP you install. Generic lanes remain unsupported by Muse Sounds, VST3, external MIDI output, and MIDI-file lane export. These checks do not validate every CC or interactive editing workflow.

## Current limitations

- Generic CC playback is limited to MS Basic/SoundFont; instrument responses and interactive workflows need wider validation.
- Recording input selection, monitoring, and latency compensation are incomplete.
- Audio media is referenced externally instead of embedded in the project.
- Full Read/Touch/Latch/Write automation workflows are not complete.
- Hardware, plug-in, and display compatibility still needs wider validation.

## Requirements

- 64-bit Windows 10 or 11
- 4 GB RAM; 8 GB recommended for larger projects and plug-ins
- Approximately 2 GB free for the application, plus sound and plug-in storage
- Compatible audio output; an ASIO driver is currently required for recording

## User guides

Read the [English user guide](docs/index.html) or [Brazilian Portuguese guide](docs/pt-BR/index.html) for installation, audio setup, linked editing, controller limits, troubleshooting, and privacy. The guides describe the current test build; check release notes when using an earlier download.

## Getting the source

Clone the repository with its submodules:

```sh
git clone --recurse-submodules https://github.com/dulcedelechedev/millerscore.git
cd millerscore
```

Build instructions are available in [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md). The project also retains the upstream MuseScore build system and documentation where applicable.

## License and attribution

MillerScore is free software licensed under the GNU General Public License version 3. See [LICENSE.txt](LICENSE.txt) for the complete terms, copyright notice, and font exception.

This is an independent community fork based on MuseScore Studio. It is not an official product and is not affiliated with or endorsed by Muse Group or the MuseScore project. MuseScore and related marks belong to their respective owners. Original authorship and third-party license notices are preserved.

## Links

- [Releases](https://github.com/dulcedelechedev/millerscore/releases)
- [Issue tracker](https://github.com/dulcedelechedev/millerscore/issues)
- [itch.io page](https://dulcedelechedev.itch.io/millerscore)
