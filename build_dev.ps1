param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Clean,
    [switch]$CompilerCache,
    [switch]$RunAfterBuild,
    [switch]$Worker
)

$ErrorActionPreference = 'Stop'

$sourceDir = (Resolve-Path $PSScriptRoot).Path
$buildRoot = 'C:\b\millerscore'
$profileSuffix = if ($CompilerCache) { '-cache' } else { '' }
$buildDir = Join-Path $buildRoot "dev-ninja-$Configuration$profileSuffix"
$installDir = Join-Path $buildDir 'install'
$logDir = Join-Path $buildRoot 'logs'
$pidFile = Join-Path $logDir "dev-$Configuration$profileSuffix.pid"
$statusFile = Join-Path $logDir "dev-$Configuration$profileSuffix.status.json"
$runRequestFile = Join-Path $logDir "dev-$Configuration$profileSuffix.run-requested"
$metricsFile = Join-Path $logDir 'metrics.jsonl'
$mutexName = "Global\MillerScoreDevBuild_$Configuration$profileSuffix"
$qtDir = if ($env:QTDIR) { $env:QTDIR } else { 'C:\Qt\6.10.2\msvc2022_64' }
$vsDevShell = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1'
$cmakeExe = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

function Find-Sccache {
    $command = Get-Command sccache.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    $file = Get-ChildItem $packages -Filter sccache.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    return $file.FullName
}

function Get-BuiltExecutable {
    $candidates = @(
        (Join-Path $buildDir 'MillerScore.exe'),
        (Join-Path $buildDir 'bin\MillerScore.exe'),
        (Join-Path $buildDir "$Configuration\MillerScore.exe")
    )
    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

function Write-Status([string]$State, [string]$Message, [hashtable]$Timing = @{}) {
    $status = [ordered]@{
        state = $State
        message = $Message
        configuration = $Configuration
        processId = $PID
        updatedAt = (Get-Date).ToString('o')
        timing = $Timing
    }
    $status | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statusFile -Encoding utf8
}

if (-not $Worker) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null

    if (Test-Path -LiteralPath $pidFile) {
        $activePid = Get-Content -LiteralPath $pidFile -ErrorAction SilentlyContinue
        if ($activePid -and (Get-Process -Id $activePid -ErrorAction SilentlyContinue)) {
            if ($RunAfterBuild) {
                New-Item -ItemType File -Path $runRequestFile -Force | Out-Null
                Write-Output 'Launch requested; MillerScore will open when the active build succeeds.'
            }
            Write-Output "A development build is already running (PID $activePid)."
            Write-Output "Status: $statusFile"
            exit 0
        }
    }

    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $stdoutLog = Join-Path $logDir "dev-$Configuration-$timestamp.log"
    $stderrLog = Join-Path $logDir "dev-$Configuration-$timestamp.err.log"
    $pwsh = (Get-Process -Id $PID).Path
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', "`"$PSCommandPath`"",
        '-Configuration', $Configuration,
        '-Jobs', $Jobs,
        '-Worker'
    )
    if ($Clean) { $arguments += '-Clean' }
    if ($CompilerCache) { $arguments += '-CompilerCache' }
    if ($RunAfterBuild) { $arguments += '-RunAfterBuild' }

    $process = Start-Process -FilePath $pwsh -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
    Set-Content -LiteralPath $pidFile -Value $process.Id -Encoding ascii

    Write-Output "Development build started in the background (PID $($process.Id))."
    Write-Output "Log: $stdoutLog"
    Write-Output "Status: $statusFile"
    exit 0
}

New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$buildMutex = [System.Threading.Mutex]::new($false, $mutexName)
if (-not $buildMutex.WaitOne(0)) {
    throw "Another build owns $buildDir. Builds sharing one directory must be serialized."
}
$totalWatch = [System.Diagnostics.Stopwatch]::StartNew()
$configureSeconds = 0.0
$buildSeconds = 0.0
$deploySeconds = 0.0
$result = 'failed'

try {
    Write-Status 'running' 'Preparing MSVC, Ninja, and compiler cache.'

    if (-not (Test-Path -LiteralPath $vsDevShell)) {
        throw "Visual Studio developer shell not found: $vsDevShell"
    }
    if (-not (Test-Path -LiteralPath $qtDir)) {
        throw "Qt was not found at $qtDir. Set QTDIR to the Qt MSVC kit."
    }
    if (-not (Test-Path -LiteralPath $cmakeExe)) {
        throw "Native Visual Studio CMake was not found: $cmakeExe"
    }

    # Framework fixes live as patches because the muse submodule tracks
    # upstream; apply any pending ones before compiling (idempotent).
    Write-Status 'running' 'Applying MillerScore framework patches to muse.'
    & (Join-Path $sourceDir 'tools\muse-patches\apply-muse-patches.ps1')

    & $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $env:QTDIR = $qtDir
    $env:QT_DIR = $qtDir

    $sccachePath = if ($CompilerCache) { Find-Sccache } else { $null }
    if ($CompilerCache -and -not $sccachePath) {
        throw 'The compiler-cache profile was requested, but sccache.exe was not found.'
    }
    if ($sccachePath) {
        & $sccachePath --start-server 2>$null
    }

    if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
        $resolvedBuild = [System.IO.Path]::GetFullPath($buildDir)
        $resolvedRoot = [System.IO.Path]::GetFullPath($buildRoot)
        if (-not $resolvedBuild.StartsWith($resolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
                                           [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean a path outside $resolvedRoot"
        }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }

    $cacheFile = Join-Path $buildDir 'CMakeCache.txt'
    $normalizedCmakeExe = $cmakeExe.Replace('\', '/')
    $cacheUsesPinnedCmake = (Test-Path -LiteralPath $cacheFile) -and
        (Select-String -LiteralPath $cacheFile -SimpleMatch "CMAKE_COMMAND:INTERNAL=$normalizedCmakeExe" -Quiet)
    if (-not $cacheUsesPinnedCmake) {
        $configureMessage = if (Test-Path -LiteralPath $cacheFile) {
            'Migrating the Ninja graph to the pinned Visual Studio CMake.'
        } else {
            'Creating the optimized Ninja build tree.'
        }
        Write-Status 'configuring' $configureMessage
        $configureWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $configureArgs = @(
            '-S', $sourceDir,
            '-B', $buildDir,
            '-G', 'Ninja',
            "-DCMAKE_BUILD_TYPE=$Configuration",
            "-DCMAKE_INSTALL_PREFIX=$installDir",
            "-DCMAKE_PREFIX_PATH=$qtDir",
            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
            '-DMUE_RUN_WINDEPLOYQT=ON',
            "-DMUSE_COMPILE_USE_PCH=$(if ($CompilerCache) { 'OFF' } else { 'ON' })",
            '-DMUSE_COMPILE_USE_UNITY=OFF',
            "-DMUSE_COMPILE_USE_COMPILER_CACHE=$(if ($CompilerCache) { 'ON' } else { 'OFF' })",
            '-DMUSE_ENABLE_UNIT_TESTS=OFF',
            '-DMUSE_MODULE_AUDIO_ASIO=OFF',
            '-DMUSE_MODULE_VST=ON',
            '-DCMAKE_NINJA_CMCLDEPS_RC=OFF',
            '-DBUILD_TESTING=OFF',
            '-DKDDockWidgets_EXAMPLES=OFF',
            '-DMUE_BUILD_BRAILLE_TESTS=OFF',
            '-DMUE_BUILD_CONVERTER_TESTS=OFF',
            '-DMUE_BUILD_ENGRAVING_TESTS=OFF',
            '-DMUE_BUILD_IMPORTEXPORT_TESTS=OFF',
            '-DMUE_BUILD_NOTATION_TESTS=OFF',
            '-DMUE_BUILD_NOTATIONSCENE_TESTS=OFF',
            '-DMUE_BUILD_PLAYBACK_TESTS=OFF',
            '-DMUE_BUILD_PROJECT_TESTS=OFF'
        )
        if ($sccachePath) {
            $configureArgs += "-DCOMPILER_CACHE_PROGRAM=$sccachePath"
        }

        & $cmakeExe @configureArgs
        if ($LASTEXITCODE -ne 0) { throw "Configure failed with exit code $LASTEXITCODE" }
        $configureWatch.Stop()
        $configureSeconds = $configureWatch.Elapsed.TotalSeconds
    }

    Write-Status 'building' 'Building only the MillerScore application target.'
    $buildWatch = [System.Diagnostics.Stopwatch]::StartNew()
    & $cmakeExe --build $buildDir --target MuseScoreStudio --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
    $buildWatch.Stop()
    $buildSeconds = $buildWatch.Elapsed.TotalSeconds

    Write-Status 'deploying' 'Refreshing the runnable development image.'
    $deployWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $builtExecutable = Get-BuiltExecutable
    if (-not $builtExecutable) {
        throw 'The MillerScore executable was not produced in the expected Ninja output paths.'
    }

    $installedExecutable = Join-Path $installDir 'bin\MillerScore.exe'
    $runtimeMarker = Join-Path $installDir '.runtime-deployed'
    $qtCoreRuntime = Join-Path $installDir 'bin\Qt6Core.dll'
    if (-not (Test-Path -LiteralPath $runtimeMarker) -or -not (Test-Path -LiteralPath $qtCoreRuntime)) {
        # The install rules also copy artifacts that are not dependencies of the
        # application target: generated .qm translations and the updater helper.
        Write-Status 'deploying' 'Building install-only artifacts for the first deployment.'
        & $cmakeExe --build $buildDir --target translations museupdater --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw "Install-only artifacts failed with exit code $LASTEXITCODE" }
        & $cmakeExe --install $buildDir
        if (-not (Test-Path -LiteralPath $installedExecutable)) {
            throw "Initial resource deployment did not install $installedExecutable"
        }

        # The project's install-time QML scan can time out while walking a
        # OneDrive checkout. QML is linked into the app, so deploy the runtime
        # from the executable without rescanning the full source tree.
        $winDeployQt = Join-Path $qtDir 'bin\windeployqt.exe'
        $runtimeBinDir = Join-Path $installDir 'bin'
        $runtimePluginsDir = Join-Path $installDir 'plugins'
        & $winDeployQt --dir $installDir --libdir $runtimeBinDir --plugindir $runtimePluginsDir --no-translations $installedExecutable
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $qtCoreRuntime)) {
            throw "Qt runtime deployment failed with exit code $LASTEXITCODE"
        }
        New-Item -ItemType File -Path $runtimeMarker -Force | Out-Null
    } else {
        Copy-Item -LiteralPath $builtExecutable -Destination $installedExecutable -Force
    }
    $deployWatch.Stop()
    $deploySeconds = $deployWatch.Elapsed.TotalSeconds

    if ($RunAfterBuild -or (Test-Path -LiteralPath $runRequestFile)) {
        Remove-Item -LiteralPath $runRequestFile -Force -ErrorAction SilentlyContinue
        $runTimestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $appStdoutLog = Join-Path $logDir "app-$Configuration-$runTimestamp.log"
        $appStderrLog = Join-Path $logDir "app-$Configuration-$runTimestamp.err.log"
        Start-Process -FilePath $installedExecutable `
            -RedirectStandardOutput $appStdoutLog `
            -RedirectStandardError $appStderrLog
    }

    $result = 'success'
    $totalWatch.Stop()
    $timing = @{
        configureSeconds = [Math]::Round($configureSeconds, 3)
        buildSeconds = [Math]::Round($buildSeconds, 3)
        deploySeconds = [Math]::Round($deploySeconds, 3)
        totalSeconds = [Math]::Round($totalWatch.Elapsed.TotalSeconds, 3)
    }
    Write-Status 'complete' 'Development build completed.' $timing
    ([ordered]@{ timestamp = (Get-Date).ToString('o'); result = $result; clean = [bool]$Clean; configuration = $Configuration; timing = $timing } |
        ConvertTo-Json -Compress) | Add-Content -LiteralPath $metricsFile -Encoding utf8

    if ($sccachePath) { & $sccachePath --show-stats }
} catch {
    $totalWatch.Stop()
    $timing = @{ totalSeconds = [Math]::Round($totalWatch.Elapsed.TotalSeconds, 3) }
    Write-Status 'failed' $_.Exception.Message $timing
    ([ordered]@{ timestamp = (Get-Date).ToString('o'); result = $result; clean = [bool]$Clean; configuration = $Configuration; error = $_.Exception.Message; timing = $timing } |
        ConvertTo-Json -Compress) | Add-Content -LiteralPath $metricsFile -Encoding utf8
    throw
} finally {
    Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    $buildMutex.ReleaseMutex()
    $buildMutex.Dispose()
}
