<#
Applies MillerScore's framework patches to the `muse` submodule.

The submodule tracks upstream musescore/muse_framework, which this fork cannot
push to. Framework changes are therefore versioned here as ordered patches and
applied to the submodule working tree before a build.

The script is idempotent. Patches may touch the same lines (a later patch can
build on an earlier one), so a patch cannot be tested on its own: instead the
working tree is compared with "HEAD + the first k patches" for every k, the
longest match tells which patches are already applied, and only the rest are
applied. A working tree that matches no prefix of the series (a local edit, or
patches applied out of order) stops the build instead of compiling a mixed
tree.

Usage:
  tools\muse-patches\apply-muse-patches.ps1          # apply pending patches
  tools\muse-patches\apply-muse-patches.ps1 -Check   # report only
#>
param(
    [switch]$Check
)

# Native git writes diagnostics to stderr; never let that become a terminating
# PowerShell error in callers that use 'Stop'.
$ErrorActionPreference = 'Continue'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$museDir = Join-Path $repoRoot 'muse'
if (-not (Test-Path -LiteralPath (Join-Path $museDir 'framework'))) {
    throw "The muse submodule is not checked out at $museDir. Run: git submodule update --init muse"
}

$patches = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.patch' | Sort-Object Name)

# Trees are built in a throwaway index, so the submodule's own index is never touched.
$scratchIndex = Join-Path ([IO.Path]::GetTempPath()) ("muse-patches-" + [Guid]::NewGuid().ToString('N') + ".index")

function Invoke-WithScratchIndex([scriptblock]$Body) {
    $previous = $env:GIT_INDEX_FILE
    $env:GIT_INDEX_FILE = $scratchIndex
    try {
        & $Body
    } finally {
        $env:GIT_INDEX_FILE = $previous
        Remove-Item -LiteralPath $scratchIndex -ErrorAction SilentlyContinue
    }
}

function Get-WorkingTreeId {
    Invoke-WithScratchIndex {
        $null = & git -C $museDir read-tree HEAD 2>&1
        $null = & git -C $museDir add -A 2>&1
        (& git -C $museDir write-tree 2>$null | Select-Object -First 1)
    }
}

# The tree of HEAD with the first $Count patches applied, or $null if they do not apply
function Get-PatchedTreeId([int]$Count) {
    Invoke-WithScratchIndex {
        $null = & git -C $museDir read-tree HEAD 2>&1
        for ($index = 0; $index -lt $Count; $index++) {
            $null = & git -C $museDir apply --cached $patches[$index].FullName 2>&1
            if ($LASTEXITCODE -ne 0) {
                return $null
            }
        }
        (& git -C $museDir write-tree 2>$null | Select-Object -First 1)
    }
}

$workingTree = Get-WorkingTreeId
$applied = -1
for ($count = $patches.Count; $count -ge 0; $count--) {
    $tree = Get-PatchedTreeId $count
    if ($tree -and $tree -eq $workingTree) {
        $applied = $count
        break
    }
}

if ($applied -lt 0) {
    throw "The muse working tree is not HEAD plus a prefix of tools\muse-patches (a local edit, or patches " +
          "applied out of order). Inspect 'git -C muse status'; to start over: 'git -C muse checkout -f HEAD' " +
          "(this discards uncommitted muse changes) and run this script again."
}

for ($index = 0; $index -lt $applied; $index++) {
    Write-Output "muse patch already applied: $($patches[$index].Name)"
}

$pending = $patches.Count - $applied
for ($index = $applied; $index -lt $patches.Count; $index++) {
    $patch = $patches[$index]
    if ($Check) {
        Write-Output "muse patch pending: $($patch.Name)"
        continue
    }
    $null = & git -C $museDir apply $patch.FullName 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Applying muse patch $($patch.Name) failed. Inspect 'git -C muse apply --check $($patch.FullName)'."
    }
    Write-Output "muse patch applied: $($patch.Name)"
}

if ($Check -and $pending -gt 0) {
    exit 1
}
