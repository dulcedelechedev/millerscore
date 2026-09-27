# Blocking publication checklist

Keep the itch.io page in **Draft** or **Restricted** until every blocking item
is checked. Preparing copy and artwork does not make the current binary a
commercial release by itself.

## Product and QA — blocking

- [x] Choose the semantic version: `0.1.0` / tag `v0.1.0`.
- [ ] Freeze the release commit and create tag `v0.1.0` from that exact commit.
- [ ] Produce a clean release build from that exact commit.
- [ ] Run malware scanning on the installer and portable archive.
- [ ] Test installation and uninstall on a clean Windows user or VM.
- [ ] Open, edit, save, close and reopen at least three representative MSCZ files.
- [ ] Validate Score → DAW, piano roll selection, undo/redo and playback.
- [ ] Validate SoundFont assignment and one redistributable/test VST3 workflow.
- [ ] Validate import of WAV, MP3 and FLAC.
- [ ] Validate an ASIO recording and confirm the resulting file location.
- [ ] Verify a failed/missing audio-file and missing-plugin scenario.
- [ ] Confirm no crash in startup, playback, save, project close and application exit.
- [ ] Capture at least three real screenshots from the exact release build.
- [ ] Confirm the system requirements on at least Windows 10 and Windows 11.

## Packaging and security — blocking

- [ ] Remove diagnostic, stale and private validation binaries from the package.
- [ ] Do not bundle third-party VSTs, Muse Sounds or sample libraries without explicit redistribution rights.
- [ ] Create an installer and a portable archive from a clean staging directory.
- [ ] Add `LICENSE.txt`, third-party notices and `SOURCE_AND_LICENSE_NOTICE.txt`.
- [ ] Confirm every version says `0.1.0`; replace the remaining store URL placeholder.
- [ ] Generate SHA-256 hashes and publish them with the release notes.
- [ ] Prefer Authenticode signing; if unsigned, disclose that Windows may show a warning.
- [ ] Verify that uninstall does not delete user scores, recordings, plugins or settings unexpectedly.

## GPL and asset audit — blocking

- [ ] Publish the complete corresponding source for the exact binary.
- [ ] Include build/install scripts and the exact Muse Framework/submodule source or fetchable revision.
- [ ] Ensure every required submodule/patch is obtainable by recipients.
- [ ] Audit fonts, icons, SoundFonts, splash audio/video and bundled assets.
- [ ] Preserve original copyright and license notices.
- [ ] Confirm the MillerScore name and logo do not imply official MuseScore affiliation.
- [ ] Obtain a short professional legal review before accepting significant sales.

## Store page — blocking

- [x] Set the planned source tag to `v0.1.0` in both descriptions.
- [ ] Upload the 630×500 cover, banner and four feature illustrations.
- [ ] Add three to five real screenshots after the illustrated panels.
- [ ] Add the final download size and verified requirements.
- [ ] Configure the page theme from `metadata.md`.
- [ ] Complete itch.io payment and tax onboarding; verify Brazilian payout and withholding.
- [ ] Review the page logged out and on a narrow/mobile browser.
- [ ] Keep “In development” and the Early Access limitations visible above purchase.
- [ ] Test the buyer download flow and source-code access with a restricted key.

## Recommended but not blocking

- [ ] Record the 60-second trailer.
- [ ] Create a support email separate from the developer's personal inbox.
- [ ] Add a privacy notice before collecting telemetry or account data.
- [ ] Prepare a crash-report template and first-week triage schedule.
- [ ] Schedule the launch devlog and social posts.
- [ ] Offer a small group of testers restricted download keys before going public.
