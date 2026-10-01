param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',
    [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
$buildScript = Join-Path $PSScriptRoot 'build_dev.ps1'
$devExecutable = "C:\b\millerscore\dev-ninja-$Configuration\install\bin\MillerScore.exe"

# Windows locks a running executable, so close only the development instance
# that this script owns before the background worker refreshes and relaunches it.
Get-Process MillerScore -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $devExecutable } |
    Stop-Process

# The build worker runs out of process and launches MillerScore after a successful
# no-op or incremental Ninja build. This command returns immediately.
& $buildScript -Configuration $Configuration -Jobs $Jobs -RunAfterBuild
