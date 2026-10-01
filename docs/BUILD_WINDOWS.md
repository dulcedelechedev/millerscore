# Building MillerScore on Windows

Use the controlled Ninja release profile below to assemble a portable Windows
runtime. The later sections also record the upstream build driver and the local
toolchain used for development.

## Controlled Windows release build

Use the standalone release profile for new portable packages:

```powershell
.\build_release.ps1 -BuildDirectory C:\b\millerscore-release -Jobs 8
py -3.12 tools\package_millerscore.py --build C:\b\millerscore-release --output C:\b\millerscore-package
```

Choose an output directory that does not already exist. The packager refuses
to replace existing output. Python 3.11 or later is required for packaging.

This profile uses native Visual Studio CMake, Ninja, `CMAKE_BUILD_TYPE=Release`,
`MUSE_APP_BUILD_MODE=release`, the stable channel, and local Crashpad handling.
It requests English MSVC diagnostics where installed and lets CMake detect the
compiler's actual include prefix for Ninja. Qt can be selected with
`-QtDirectory` or `QTDIR`. The application and portable launcher both carry
MillerScore version and publisher metadata.

Both C and C++ compilation receive MSVC `/experimental:deterministic` and
`/pathmap` flags. The source, build and Qt roots map to `MillerScore`, `Build`
and `Qt`, respectively, so compiler-generated file references do not expose the
developer's source directory. Selecting `Release` and omitting PDB files alone
does not remove source paths embedded by assertions. Inspect the actual main
executable and archive for personal paths after building; prebuilt Qt DLLs can
retain their vendor's build provenance.

The controlled PowerShell profile also creates private linker PDB and MAP files
for crash diagnosis. `/PDBALTPATH:%_PDB%` keeps the embedded PDB reference to its
filename; `/INCREMENTAL:NO`, `/OPT:REF` and `/OPT:ICF` retain optimized linking.
Keep these symbols in the build directory. The runtime allowlist excludes PDB,
MAP and other debug artifacts from the portable package.

`build_millerscore.bat` uses the same Release application mode, compiler path
mapping and Qt deployment imports for its local `build` directory. It writes the
launcher to `build\millerscore.exe` and the application to
`build\bin\MillerScore.exe`. The controlled PowerShell profile installs into
`<BuildDirectory>\install`, which is the layout accepted by the packager.

Framework patches are checked and applied before building. Do not discard local
submodule changes if that check fails: inspect the differences and preserve them
as an incremental patch. `-UseWorkingFramework` compiles an intentionally edited
framework tree while those changes are being prepared; verify the complete patch
series before handing source to other developers.

The ZIP uses a controlled runtime allowlist, includes the user guide and license
notices, and excludes diagnostic executables, private reports, development
extensions, symbols, and crash dumps. Its external manifest records every file
and the archive SHA-256. Assembly alone does not establish release readiness:
extract to a new location, open the main window, create/save/reopen a score,
switch Score/DAW, test playback and unavailable Muse Sounds, check updates, and
verify normal shutdown without orphan helpers. Do not publish until those checks
and the remaining release gates pass.

## Portable Qt runtime

The release scripts run `windeployqt` with
`buildscripts/packaging/Windows/DeploymentImports.qml` as the QML import fixture.
That fixture covers the Qt imports used by the application and framework,
including Qt Quick Controls, Effects, layouts and Qt 5 Compatibility graphical
effects. `--skip-plugin-types qmltooling` excludes the QML debugger and profiler
plugins; `--no-translations` avoids copying unrelated Qt language binaries. The
controlled packager also filters installed application translations to English.

Keep the runtime's `bin`, `plugins` and `qml` directories together. Qt reads
`bin\qt.conf` beside the application executable; `Prefix=..` locates the runtime
root, while `Plugins=plugins`, `QmlImports=qml` and `Translations=locale` locate
the associated resources. Copying only `MillerScore.exe` is insufficient.

`qml/Qt5Compat/GraphicalEffects/private` contains Qt's required graphical effect
implementation, including its QML helpers and release plugin. Preserve those
vendor runtime files. This narrow module exception does not permit project
private documents, credentials or diagnostic files elsewhere in the archive;
the final privacy audit must still inspect the contents of permitted files.

Use a fresh build/install directory for release validation. Deployment skips
do not remove files left by an earlier configuration, and the local batch helper
can reuse an existing development directory. A controlled package's allowlist
and its final audit remain necessary even after deployment succeeds.

## Verified toolchain

- Windows 11 x64
- Visual Studio 2022 Community with the Desktop development with C++ workload
- Visual Studio 2022 17.14 with MSVC 14.44
- Windows SDK 10.0.26100
- CMake 4.4.3
- Qt 6.10.2, `win64_msvc2022_64`
- Qt modules: Qt 5 Core Compatibility, Network Authorization, Shader Tools,
  and WebSockets
- Python 3.12 with `aqtinstall`
- Git submodules initialized recursively

Qt is installed at `C:\Qt\6.10.2\msvc2022_64` on the reference machine.

## Initial setup

From a PowerShell prompt in the repository root:

```powershell
git submodule update --init --recursive --depth 1
py -3.12 -m pip install --user aqtinstall
py -3.12 -m aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 `
    -m qt5compat qtnetworkauth qtshadertools qtwebsockets `
    -O C:\Qt
```

The Qt version and modules intentionally match `.github/workflows/build_windows.yml`.
An older Visual Studio 17.8/MSVC 14.38 toolset is not sufficient for all current
prebuilt dependencies: the MNX test target fails to link against newer C++
standard-library symbols.

## Keep the build path short

Some generated QML object paths exceed the path accepted by the MSVC toolchain
when the repository is inside a long OneDrive directory. Create the following
ignored, machine-local `build_overrides.cmake` in the repository root:

```cmake
# Keep generated MSVC paths below the legacy compiler path limit.
set(ALL_BUILDS_PATH "C:/b/musescore-gold")
```

Without this override, compilation can fail with `fatal error C1083: Cannot
open compiler generated file: '': Invalid argument`, even when Windows long
paths are enabled.

## Configure, build, and install

```powershell
$env:QTDIR = 'C:\Qt\6.10.2\msvc2022_64'
cmake -P build.cmake configure -DCMAKE_BUILD_TYPE=Release -DMUE_RUN_WINDEPLOYQT=ON
cmake -P build.cmake build -DCMAKE_BUILD_TYPE=Release -DMUE_RUN_WINDEPLOYQT=ON
cmake -P build.cmake install -DCMAKE_BUILD_TYPE=Release -DMUE_RUN_WINDEPLOYQT=ON
```

The build driver selects Visual Studio 17 2022 and x64 automatically. With the
short-path override, the resulting locations are:

- Build tree: `C:\b\musescore-gold\Win-Qt6.10.2-msvc2022_64-VS17-Release`
- Installed application: `C:\b\musescore-gold\Win-Qt6.10.2-msvc2022_64-VS17-Release\install\bin\MuseScoreStudio5.exe`

`MUE_RUN_WINDEPLOYQT=ON` deploys the Qt runtime, platform plugins, and QML
modules beside the executable. Without it, the executable builds but a clean
machine-local install may exit immediately because required Qt DLLs are absent.
For subsequent builds, the same `build` and `install` commands are incremental.

## Run

```powershell
$env:QTDIR = 'C:\Qt\6.10.2\msvc2022_64'
cmake -P build.cmake run -DCMAKE_BUILD_TYPE=Release
```

Alternatively, start the executable from the install path above.

## Fast development loop

Use the background Ninja workflow in [DEV_WORKFLOW.md](DEV_WORKFLOW.md) for
day-to-day C++ and QML work. The upstream Visual Studio build above remains useful
for the complete test suite. Its default application mode is development even
when the compiler configuration is named Release; use the controlled profile
above for distributable builds.

## Updating the upstream base

The project keeps the original MuseScore repository as `upstream`:

```powershell
git fetch upstream main
git merge upstream/main
git submodule update --init --recursive
```

Resolve and test upstream updates locally before pushing them to `origin`.
