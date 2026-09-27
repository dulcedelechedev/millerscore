# Building Musescore Gold on Windows

This document records the verified local Windows build used as the project
baseline. It follows the upstream MuseScore build driver instead of duplicating
its CMake configuration.

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
day-to-day C++ and QML work. Keep the full Visual Studio build above for release
verification and the complete test suite.

## Updating the upstream base

The project keeps the original MuseScore repository as `upstream`:

```powershell
git fetch upstream main
git merge upstream/main
git submodule update --init --recursive
```

Resolve and test upstream updates locally before pushing them to `origin`.
