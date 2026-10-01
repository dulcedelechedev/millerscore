# MillerScore privacy notice

MillerScore is a desktop application and does not add MillerScore-owned
analytics or advertising trackers.

## Update checks

When **Check for updates** is enabled, MillerScore contacts the public GitHub
Releases API for `dulcedelechedev/millerscore`. A manual check uses the same service.
GitHub receives ordinary network request information such as the IP address,
request time, application/network headers and other data covered by GitHub's
own privacy statement.

Automatic checks can be disabled in **Preferences > General > Updates**. The
MillerScore checker does not silently install an update. Background package
downloads are disabled by default; when a compatible package is unavailable,
the application offers the authenticated itch app flow, with the official
itch.io store page as a browser fallback when that app is unavailable.

## Crash reports and diagnostic files

The Windows release records crash reports locally. Automatic crash-report
uploads are disabled by default, and this release has no upload server
configured. Scores and imported audio are not sent with an update check.

Logs and crash reports may contain account names, filesystem paths, device
names, plugin details, or data from an open project. Review and redact them
before sharing. The default release log directory is
`%LOCALAPPDATA%\MillerScore\MillerScore\logs`; local Crashpad data is in its
`dumps` subfolder. Development profiles use a separate directory whose
application name ends with `Development`.

## External components and services

Third-party plugins, MuseHub, Muse Sounds, links opened in a browser and any
online MuseScore services are operated under their respective providers'
privacy terms. MillerScore does not control those services.

## Local files

Scores, imported audio references, recordings and preferences remain local
unless the user explicitly uses an external publishing, sharing or cloud
feature. Users should avoid submitting private scores, licensed sample content
or proprietary plugins to public issue reports.

Questions and issue reports: <https://github.com/dulcedelechedev/millerscore/issues>
