# FAQ and support policy

## Is MillerScore free software?

Yes. MillerScore is distributed under GNU GPLv3. Buying the official build
supports its development and does not remove your right to inspect, modify or
redistribute the covered software.

## Why pay if the source is available?

The paid release provides a prepared official build, release notes and a clear
update path while directly funding development, testing and packaging. You may
also compile the corresponding source yourself.

## Is this the official MuseScore application?

No. MillerScore is an independent community fork. It is not affiliated with or
endorsed by Muse Group or the MuseScore project.

## Will my MuseScore files open?

MillerScore is based on MuseScore Studio and uses the MSCZ project format. Keep
backups: Early Access changes and newer file formats may not behave identically
in older applications.

## Are VST3 plugins or Muse Sounds included?

Not necessarily. Compatible plugins, sample libraries, MuseHub and Muse Sounds
are obtained and licensed separately from their respective publishers.

## Do all MIDI controllers change the sound?

Not yet. CC0–CC127 lanes can be catalogued, edited and saved, but generic CC
playback is still being connected across backends. The application identifies
this limitation instead of silently claiming support.

## How does audio recording work?

Choose an ASIO driver, add and arm an audio track, then press Record. The
current implementation captures up to two input channels and creates a 32-bit
float WAV. Input selection, monitoring and latency compensation are planned.

## Where are imported and recorded audio files stored?

Audio clips currently reference external files. Recorded takes are stored next
to the saved score in a `<score> Audio` directory, or under
`Documents/MillerScore/Recordings` for an unsaved score. Moving a project
without its audio files can break those references.

## How do I report a problem?

Open an issue at <https://github.com/dulcedelechedev/millerscore/issues> and include:

- MillerScore version.
- Windows version.
- Exact steps to reproduce.
- Expected and observed behavior.
- Audio driver/interface and plugin names when relevant.
- A small non-confidential test project when possible.

Never publish private scores, commercial sample libraries or proprietary
plugins in a public issue.

## Support scope

Early Access support covers installation guidance and reproducible defects in
the official build. It does not guarantee compatibility with every third-party
plugin, driver or library, and it does not include custom music production or
project recovery services.

## Refunds

Refund requests are handled according to itch.io's policies and applicable
consumer law. Before buying, review the requirements and known limitations.
