# MillerScore publication kit

This directory contains ready-to-paste copy and code-generated artwork for an
itch.io Early Access page. It deliberately describes the current product
honestly: MillerScore is a GPLv3 community fork, generic MIDI CC playback is
not complete, and ASIO recording still lacks input selection, monitoring and
latency compensation.

## Contents

- `listing-en.md`: primary English store page.
- `metadata.md`: every itch.io form field, theme value and upload order.
- `faq-and-support.md`: buyer-facing FAQ and support policy.
- `launch-devlog.md`: first itch.io devlog.
- `release-notes-v0.1.0.md`: release notes for the first binary and GitHub release.
- `social-and-video.md`: social posts, YouTube description and trailer script.
- `legal/`: GPL/source and independence notices to ship with the download.
- `assets/`: PNG artwork ready to upload.
- `assets/source/generate_assets.py`: reproducible Pillow generator.

Regenerate the artwork from the repository root:

```powershell
python marketing/itchio/assets/source/generate_assets.py
```

The feature images are illustrations generated from code, not application
screenshots. Add at least three real screenshots after the final release build
passes an attended UI check. Do not publish the itch.io page before completing
the blocking items in `publish-checklist.md`.
