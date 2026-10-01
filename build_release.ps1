# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$BuildDirectory = "$env:SystemDrive\b\millerscore-release",
    [string]$QtDirectory = $(if ($env:QTDIR) { $env:QTDIR } else { 'C:\Qt\6.10.2\msvc2022_64' }),
    [ValidateRange(1, 64)][int]$Jobs = 8,
    [switch]$UseWorkingFramework
)

$ErrorActionPreference = 'Stop'
$sourceDirectory = $PSScriptRoot
$buildPath = [IO.Path]::GetFullPath($BuildDirectory)
$installPath = Join-Path $buildPath 'install'
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio with the C++ workload is required.' }
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$developerShell = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
$deployQt = Join-Path $QtDirectory 'bin\windeployqt.exe'
foreach ($required in @($cmake, $developerShell, $deployQt)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required build tool is missing: $required" }
}

if (-not $UseWorkingFramework) {
    & (Join-Path $sourceDirectory 'tools\muse-patches\apply-muse-patches.ps1')
}
& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$env:QTDIR = $QtDirectory
$env:QT_DIR = $QtDirectory
$env:VSLANG = '1033'
$pathMaps = @(
    ('/pathmap:"{0}=MillerScore"' -f $sourceDirectory),
    ('/pathmap:"{0}=Build"' -f $buildPath),
    ('/pathmap:"{0}=Qt"' -f $QtDirectory)
) -join ' '
$releaseCFlags = "/DWIN32 /D_WINDOWS /experimental:deterministic $pathMaps"
$releaseCppFlags = "$releaseCFlags /EHsc"
$configure = @(
    '-S', $sourceDirectory, '-B', $buildPath, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DMUSE_APP_BUILD_MODE=release',
    "-DCMAKE_INSTALL_PREFIX=$installPath", "-DCMAKE_PREFIX_PATH=$QtDirectory",
    "-DCMAKE_C_FLAGS=$releaseCFlags", "-DCMAKE_CXX_FLAGS=$releaseCppFlags",
    '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO /DEBUG:FULL /PDBALTPATH:%_PDB% /MAP /OPT:REF /OPT:ICF',
    '-DMUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT=ON', '-DMUSE_MODULE_DIAGNOSTICS_CRASHREPORT_URL=',
    '-DMUE_RUN_WINDEPLOYQT=OFF', '-DCMAKE_NINJA_CMCLDEPS_RC=OFF',
    '-DMUSE_COMPILE_USE_PCH=ON', '-DMUSE_COMPILE_USE_UNITY=OFF',
    '-DMUSE_COMPILE_USE_COMPILER_CACHE=OFF', '-DMUSE_ENABLE_UNIT_TESTS=OFF',
    '-DMUSE_MODULE_AUDIO_ASIO=ON', '-DMUSE_MODULE_VST=ON',
    '-DMUSE_MODULE_RCONTROL=OFF', '-DMUE_BUILD_ENGRAVING_DEVTOOLS=ON',
    '-DBUILD_TESTING=OFF', '-DKDDockWidgets_EXAMPLES=OFF'
)
foreach ($module in @('BRAILLE', 'CONVERTER', 'ENGRAVING', 'IMPORTEXPORT', 'NOTATION', 'NOTATIONSCENE', 'PLAYBACK', 'PROJECT')) {
    $configure += "-DMUE_BUILD_${module}_TESTS=OFF"
}
& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw "Release configuration failed ($LASTEXITCODE)." }
& $cmake --build $buildPath --target MuseScoreStudio translations museupdater --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "Release build failed ($LASTEXITCODE)." }
& $cmake --install $buildPath
if ($LASTEXITCODE -ne 0) { throw "Release installation failed ($LASTEXITCODE)." }

$app = Join-Path $installPath 'bin\MillerScore.exe'
if (-not (Test-Path -LiteralPath $app)) { throw 'The release executable was not installed.' }
& $deployQt --dir $installPath --libdir (Join-Path $installPath 'bin') --plugindir (Join-Path $installPath 'plugins') `
    --qmldir (Join-Path $sourceDirectory 'buildscripts\packaging\Windows') --skip-plugin-types qmltooling --no-translations $app
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed ($LASTEXITCODE)." }

# Qt searches beside the executable; resources are one directory above bin.
$qtConfiguration = "[Paths]`nPrefix=..`nPlugins=plugins`nQmlImports=qml`nTranslations=locale`nLibraries=bin`nLibraryExecutables=bin`nData=.`n"
[IO.File]::WriteAllText((Join-Path $installPath 'bin\qt.conf'), $qtConfiguration, [Text.UTF8Encoding]::new($false))

$launcher = Join-Path $installPath 'MillerScore.exe'
$launcherObject = Join-Path $buildPath 'millerscore_launcher.obj'
$launcherVersion = Join-Path $buildPath 'millerscore_launcher_version.res'
& rc.exe /nologo "/fo$launcherVersion" (Join-Path $buildPath 'src\app\windows_version.rc')
if ($LASTEXITCODE -ne 0) { throw "Launcher version-resource compilation failed ($LASTEXITCODE)." }
& cl.exe /nologo /O2 /EHsc /DUNICODE /D_UNICODE "/Fo:$launcherObject" "/Fe:$launcher" `
    (Join-Path $sourceDirectory 'buildscripts\tools\millerscore_launcher.cpp') $launcherVersion user32.lib /link /SUBSYSTEM:WINDOWS
if ($LASTEXITCODE -ne 0) { throw "Portable launcher compilation failed ($LASTEXITCODE)." }
Write-Output "Release runtime installed: $installPath"
Write-Output 'Verify the packaged artifact before distribution.'
