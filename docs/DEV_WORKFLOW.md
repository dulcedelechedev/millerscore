# MillerScore development workflow

This workflow is optimized for the shortest safe edit-to-run cycle on Windows.
Builds run in the background by default so development work can continue while
the compiler is active.

## Why the original incremental build was slow

The initial Visual Studio build tree was generated before the MSVC toolchain was
upgraded from 14.38 to 14.44. Thousands of Qt AUTOMOC dependency records still
referenced the removed 14.38 headers. MSBuild therefore treated generated targets
as stale, emitted large numbers of `MSB8064` warnings, and repeatedly revisited
much of the project graph.

Additional costs came from building `ALL_BUILD`, which includes framework and
application test executables, and from CMake/Qt QML regeneration being spread
across many Visual Studio projects. The source checkout is also under OneDrive;
filesystem indexing, cloud synchronization, and placeholder metadata can add
latency when CMake and the compiler scan tens of thousands of files.

## Recommended source location

Keep the canonical checkout outside any synchronized folder. A short local path
such as `C:\src\millerscore` is recommended. Keep GitHub/OneDrive as a remote or
backup destination rather than the active compiler input tree.

One safe migration is:

```powershell
git clone https://github.com/dulcedelechedev/millerscore.git C:\src\millerscore
cd C:\src\millerscore
git submodule update --init --recursive --depth 1
```

The optimized build output is already outside OneDrive at `C:\b\musescore-gold`.

## Fast build and run

`build_dev.ps1` uses Ninja + MSVC, precompiled headers, and non-unity translation
units. It disables unit tests and example targets and builds only the CMake
application target `MuseScoreStudio`, whose output is `MillerScore5.exe`.
It pins the native CMake distributed with Visual Studio; the MSYS2 MinGW CMake
generates an invalid MSVC resource-compiler rule (`RC ... cl.exe rc.exe`) and
must not be used for this build tree.
The profile also disables Ninja's `cmcldeps` wrapper for `.rc` files because it
passes an invalid command line to this Windows SDK's resource compiler; direct
`rc.exe` compilation remains dependency-safe for the rarely changed app icon.

```powershell
# Starts an incremental build in the background and returns immediately.
.\build_dev.ps1

# Starts a background build and launches MillerScore as soon as it succeeds.
.\run_dev.ps1

# Explicit clean benchmark/recovery build. This removes only the dedicated
# C:\b\musescore-gold\dev-ninja-RelWithDebInfo directory.
.\build_dev.ps1 -Clean

# Optional experiment: no PCH and sccache in a separate build directory.
.\build_dev.ps1 -CompilerCache
```

The script configures CMake when the dedicated Ninja cache does not exist. It
also migrates an older cache once when `CMAKE_COMMAND` still points at a
different CMake installation; this prevents Ninja regeneration from silently
falling back to the MSYS2 MinGW CMake. After that, Ninja invokes CMake
automatically only when a build-system input actually changes. On the first
successful build, the runtime is installed and Qt is deployed once. Later
builds copy only the changed application executable into the existing runnable
image.

`run_dev.ps1` closes only the MillerScore process running from this profile's
development install path before refreshing the executable. Other installed or
release builds remain untouched; this avoids Windows executable-file locking
during the edit → build → relaunch loop.

Progress and timing are available without blocking the working shell:

```powershell
Get-Content C:\b\musescore-gold\logs\dev-RelWithDebInfo.status.json
Get-Content C:\b\musescore-gold\logs\metrics.jsonl
Get-Content C:\b\musescore-gold\logs\dev-RelWithDebInfo-*.log -Tail 30
```

Applications launched by `run_dev.ps1` also capture stdout and stderr as
`app-<configuration>-<timestamp>.log` and `.err.log` in the same directory.
The application's structured startup log remains under
`%LOCALAPPDATA%\MuseScore\MillerScore5Development\logs`.

Do not run `qmlformat --check` with the bundled Qt 6.10.2 tools. This version
does not support that option and opens one modal error dialog per invocation.
QML validity is checked by the generated QML cache during the normal
background application build. Use `git diff --check` only for whitespace.

## Build modes

- `RelWithDebInfo` is the default: optimized, with symbols suitable for normal
  debugging and profiling.
- `Debug` is reserved for problems that disappear under optimization.
- The existing Visual Studio Release build remains the release/package
  verification path, not the inner development loop.

## QML and C++ iteration

Unity compilation is deliberately disabled in this edit-loop profile: it can
improve a cold build, but a one-line C++ change otherwise recompiles its entire
unity batch and produces coarser cache entries.

QML files are compiled into Qt resources, so a QML edit rebuilds the AppShell
QML resource and relinks the main executable. The dedicated Ninja graph avoids
building unrelated test binaries during that relink. Small C++ changes benefit
from PCH and fine-grained non-unity translation units; header changes can still
invalidate their normal dependency fan-out.

The default profile deliberately uses PCH without `sccache`. On this MSVC
toolchain, sccache 0.17 reports PCH compiler calls as non-cacheable because of
`/Fp` and `/Yc`; enabling both adds process and dependency-output overhead with
no cache hits. `-CompilerCache` is therefore an opt-in experiment that disables
PCH and uses a separate `-cache` build directory, leaving the fast default
profile unchanged.

Avoid touching global CMake files, version metadata, or central QML module lists
during ordinary UI iteration. Those files legitimately trigger CMake or QML type
regeneration.

## Full verification

Before a release or after changes to shared framework code, use the full build
documented in [BUILD_WINDOWS.md](BUILD_WINDOWS.md). The fast loop intentionally
does not replace the complete tests and packaging checks.

## Measurements

Each background build appends machine-readable timing data to
`C:\b\musescore-gold\logs\metrics.jsonl`. Record at least:

1. one `-Clean` build;
2. one no-change incremental build;
3. one representative small C++ edit;
4. one representative QML edit.

This separates compiler time from first-time configuration and Qt deployment.

Measurements on the current Windows workstation (16 parallel jobs):

| Profile | Result | Time |
| --- | --- | ---: |
| Previous Unity + PCH + sccache cold tree | Reached 2406/2412, then failed in the malformed RC rule | 1,101.280 s |
| Non-unity + PCH migration build | Completed across preserved-object restarts after removing stale DAW and ASIO graph entries and repairing RC generation | about 51 min wall-clock |
| Optimized no-change incremental | `ninja: no work to do` | 0.900 s build; 5.287 s process total |
| Optimized no-change build + launch | MillerScore window responsive | 0.846 s build; 5.557 s process total |

The migration build is not a controlled clean benchmark because source and
CMake inputs changed while it was running; it is recorded as an upper-bound
observation, not a like-for-like speedup claim. A future stable-HEAD clean run
will provide the repeatable post-optimization cold-build number.

## Remaining application-target breadth

The initial non-unity configured Ninja tree containing `MuseScoreStudio` has 9,139 build
edges and 2,928 object-compilation edges. The largest source groups by object
edge count are engraving (465), UI components (264), properties panel (243),
import/export (205), notation scene (196), project (145), audio (109),
preferences (100), dock window (99), and app shell (93). Building only the app
target removes standalone tests and unrelated executables, but the app itself
still links a broad set of optional production modules.

Candidates for a later lean development profile, subject to configure and
startup validation, are:

- `MUE_BUILD_BRAILLE_MODULE=OFF`, `MUE_BUILD_CONVERTER_MODULE=OFF`, and
  `MUE_BUILD_ENGRAVING_DEVTOOLS=OFF`;
- legacy import/export modules BB, BWW, Capella, MNX, MuseData, OVE, Guitar Pro,
  MEI, video, TablEdit, and lyrics;
- MuseSounds and cloud/account UI when work does not exercise those paths.

MIDI and MusicXML import/export, playback, project, notation, engraving,
audio, and image/audio export remain in the default Score + DAW profile.
Optional modules should only move to the default OFF set after a clean
configure, successful link, and Score/DAW startup smoke test.

The lean Windows development profile keeps the WASAPI driver and disables ASIO
with `MUSE_MODULE_AUDIO_ASIO=OFF`. The bundled ASIO SDK does not compile against
the shared MSVC PCH (`iasiodrv.h` lacks the required COM declarations in that
include order). ASIO remains enabled in the full release/package profile.
The existing Muse Framework VST3 module remains enabled so DAW instrument
discovery uses the application's standard scanner and validation flow.
