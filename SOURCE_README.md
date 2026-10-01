# MillerScore 0.1.0 Windows test-build source snapshot

This source archive corresponds to the Build14 Windows x64 application and launcher listed in `SOURCE_MANIFEST.json`. It contains the current application sources, framework source inputs for the tested Release configuration, the dependency recipes, build and packaging scripts, and all 30 framework patches. The application worktree contains fixes after the recorded main-repository baseline commit; that commit alone does not reproduce this test build.

The archive is an editable source snapshot, without `.git` metadata or compiled application/dependency binaries. The hashes in `SOURCE_MANIFEST.json` identify the supplied files. Sources compiled into the tested application and launcher were copied without modification. Upstream copyright and license headers are retained. The public user guide is in `docs/index.html`, with a Brazilian Portuguese translation in `docs/pt-BR/index.html`. The two screenshot galleries and their final public captures are included; other marketing materials are excluded.

## Rebuild on Windows x64

Use Visual Studio 2022 with the Desktop development with C++ workload, MSVC 14.44 and Windows SDK 10.0.26100, PowerShell 7, Git, Python 3, native Visual Studio CMake, and Ninja. Install Qt 6.10.2 for `msvc2022_64` with the modules described in `docs/BUILD_WINDOWS.md` (including Qt 5 Compatibility, Network Authorization, Shader Tools, SVG, and Qt Quick/Widgets). Extract to a short local directory such as `C:\src\millerscore-source` and use a new build directory outside the source tree.

```powershell
pwsh -NoProfile -File .\SOURCE_REBUILD.ps1 -BuildDirectory C:\b\millerscore-source-release -QtDirectory C:\Qt\6.10.2\msvc2022_64 -Jobs 8
```

The wrapper first seeds `SOURCE_RELEASE_PROFILE.cmake`, then calls the supplied `build_release.ps1 -UseWorkingFramework`. This preserves the tested legacy KDDockWidgets configuration (`MUSE_MODULE_DOCKWINDOW_KDDOCKWIDGETS_V2=OFF`), Release mode, module selection, and crash-report defaults. Do not run the framework patch application script on this archive: the supplied `muse/` sources already contain patches 0001–0030. A fresh configuration still requires network access for dependencies pinned in `muse_deps/recipes` and `muse_deps/prebuilt.lock`. These external dependency caches, Microsoft/Qt SDKs, and compiler toolchains are not included. `EXTDEPS_OVERRIDE_ALL=REBUILD` selects dependency source rebuilds instead of prebuilt dependency delivery; see `muse_deps/README.md`.

The supplied `share/sound/MS Basic.sf3` is the exact licensed SoundFont used by this build (version 0.2.0). The profile disables automatic SoundFont updates to keep those bytes unchanged; the original tested cache had online version checking enabled. Fonts, icons, score fixtures, and the splash jingle are source resources, not compiled application binaries. Their source files and notices remain included. The video outro resource is the metadata-cleaned asset retained from Build13; its exact hash is recorded in the manifest. Optional proprietary MuseSampler/Muse Sounds libraries and content are not included and are not needed for MS Basic playback.

The build driver maps compiler-generated source/build/Qt paths to portable labels and produces private PDB/MAP files. Do not distribute those files or the build cache. The installed application is in the build directory's `install` folder. To assemble a runtime ZIP with the supplied allowlist:

```powershell
python .\tools\package_millerscore.py --build C:\b\millerscore-source-release --output C:\b\millerscore-source-package
```

A complete fresh rebuild of this source archive has not been rerun as part of snapshot assembly. The original worktree was built and tested; every included existing file is verified against its worktree hash, and generated snapshot helpers are syntax-checked. Different SDK versions or dependency delivery may change executable bytes; this is not a claim of byte-reproducible builds across machines.

## Upstream references and patches

- Application baseline: 247de3d39fbf9177fb6b56d576581aa4028be343, with worktree changes identified by the file manifest.
- Framework baseline: f5e1653413e827a17c4340544d1d0bce8a3fab8a from https://github.com/musescore/muse_framework.git.
- Dependency recipes: a9db17d2f0478859711873f2db156eedfc082040 from https://github.com/musescore/muse_deps.git.
- Framework patch series: `tools/muse-patches/0001-*.patch` through `0030-*.patch`, applied in numeric order against the framework baseline.

Both this archive and the corresponding test-source Git snapshot contain the full, already patched `muse/` and `muse_deps/` directories. Build either with `SOURCE_REBUILD.ps1`; do not initialize replacement submodules or reapply patches. If reconstructing the framework separately from its upstream baseline, check out the exact framework and recipes commits above, then apply the supplied patches once in numeric order. The old publicly available application baseline or a documentation-only commit must not be treated as the complete source for the binary attached with this snapshot.

## Licenses and contents

MillerScore is an independent community fork. See `LICENSE.txt` for GPLv3 and its font-embedding exception. Font and SoundFont notices are preserved under `fonts/`, `share/sound/`, and framework third-party directories. Additional notices from the tested dependency installation are in `SOURCE_NOTICES/dependencies/`. Original dependency recipes provide pinned upstream sources and their license-file lists. Qt and the Microsoft toolchain are obtained separately under their respective licenses. No optional proprietary sampler content is redistributed here.

Source selection uses versioned files in the current worktree plus a reviewed allowlist of new application/framework sources, tests, documentation, and build scripts. Deleted private audits/prompts, unfinished marketing drafts/artwork, encrypted CI credentials, precompiled CI symbol tools, build outputs, caches, recordings, fake runtime DLLs, and Git metadata are excluded. Seven ancillary legacy fixtures/utilities/documentation images containing historical absolute account paths are also excluded; they are not used by the Windows application build. Generic documentation placeholders and upstream copyright names are retained.
