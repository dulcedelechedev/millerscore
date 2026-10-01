# MillerScore in pictures

These real application captures show the Golden Hour demo in Score, DAW and
the mixer. Follow the [user guide](index.html) for the complete workflow, or
view this gallery in [Brazilian Portuguese](pt-BR/CAPTURAS.md).

## 1. Open the score

Start in **Score** to read and edit the notation for Piano, Strings and Bass.

![Golden Hour notation in Score, with Piano, Strings and Bass](../marketing/itchio-clean-v0.1.0/01-score-view-1600x1000.png)

## 2. Switch to DAW

Use **DAW** to inspect the same music in the piano roll and velocity lane.
Choose an **MS Basic/SoundFont** instrument in the sound inspector for generic
CC playback.

![DAW piano roll and velocity lane, with the MS Basic sound inspector](../marketing/itchio-clean-v0.1.0/02-piano-roll-1600x1000.png)

## 3. Add a CC7 curve

Select the Piano track and **CC7 — Channel Volume** to shape the receiving
SoundFont instrument's volume. The pictured curve has three points; its values
are MIDI integers from 0 to 127.

![Piano track with a three-point CC7 Channel Volume curve and MS Basic sound assignment](../marketing/itchio-clean-v0.1.0/03-midi-controller-lanes-1600x1000.png)

## 4. Check the mixer during playback

Open the **Mixer** to watch track and master meters and adjust the mix.
Generic CC7 changes the SoundFont instrument's volume; the mixer level is a
separate control.

![Mixer during playback, showing Piano, Strings, Bass and master channels with meters](../marketing/itchio-clean-v0.1.0/04-mixer-1600x1000.png)

## Controller playback scope

The 108 editable generic CC lanes send separate 7-bit values (0–127) only to
**MS Basic/SoundFont during main score playback**. The instrument can ignore
controllers it does not implement; paired lanes do not provide automatic
combined 14-bit control. The 20 protected controllers are read-only and never
sent. **Muse Sounds, VST3, external MIDI output and Standard MIDI File export
do not receive these generic DAW lanes.** See the
[controller guide](index.html#controllers) for the full catalog and safety rules.
