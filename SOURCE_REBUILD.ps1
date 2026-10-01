# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$BuildDirectory = "$env:SystemDrive\b\millerscore-source-release",
    [string]$QtDirectory = $(if ($env:QTDIR) { $env:QTDIR } else { 'C:\Qt\6.10.2\msvc2022_64' }),
    [ValidateRange(1, 64)][int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$sourceDirectory = $PSScriptRoot
$buildPath = [IO.Path]::GetFullPath($BuildDirectory)
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio with the C++ workload is required.' }
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$developerShell = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
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
$configure = @(
    '-S', $sourceDirectory, '-B', $buildPath, '-G', 'Ninja',
    '-C', (Join-Path $sourceDirectory 'SOURCE_RELEASE_PROFILE.cmake'),
    "-DCMAKE_PREFIX_PATH=$QtDirectory", "-DCMAKE_INSTALL_PREFIX=$buildPath\install",
    "-DCMAKE_C_FLAGS=$releaseCFlags", "-DCMAKE_CXX_FLAGS=$releaseCFlags /EHsc"
)
& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw "Source snapshot profile configuration failed ($LASTEXITCODE)." }
# Sources in muse/ are already patched, and the archive deliberately has no Git metadata.
& (Join-Path $sourceDirectory 'build_release.ps1') -BuildDirectory $buildPath -QtDirectory $QtDirectory -Jobs $Jobs -UseWorkingFramework
