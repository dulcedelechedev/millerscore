# MillerScore v0.1.0 — Early Access

The planned first public MillerScore release, developed by
**DulceDeLecheDEV**. It brings music notation and a connected production
workflow together in one Windows application.

## Highlights

- Score and DAW views connected to the same project.
- Piano roll with selection, snap, zoom, and performance editing.
- SoundFonts, compatible VST3 instruments, and optional Muse Sounds integration.
- WAV, MP3, and FLAC audio-track import.
- Up to two ASIO inputs recorded to 32-bit float WAV.
- Mixer and effect processing with plug-in delay compensation.
- MIDI CC0–CC127 catalog with protection for dangerous state commands.
- Black & Gold theme.
- Manual and automatic version checks through public GitHub releases.

## Known limitations

- Audible generic CC playback does not yet cover every sound engine.
- Recording does not yet provide input selection, monitoring, or input-latency
  compensation. Effect delay compensation does not replace this.
- Imported audio files are externally referenced and must remain available at
  the same path.
- Some new surfaces may still contain untranslated upstream strings.
- Windows may show a warning if the first build is not yet code-signed.

## Updates

Use **Help > Check for updates**. MillerScore contacts only the repository’s
public GitHub releases. It does not silently install updates, and background
package downloads remain disabled by default.

## License and source

MillerScore is GPLv3 free software and an independent community fork based on
MuseScore Studio. The corresponding source for this build will be available at:

<https://github.com/dulcedelechedev/millerscore/releases/tag/v0.1.0>

MillerScore is not affiliated with or endorsed by Muse Group or the MuseScore project.
