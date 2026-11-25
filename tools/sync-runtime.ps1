<#
.SYNOPSIS
  Copies the runtime from the development clone into runtime/.

.DESCRIPTION
  runtime/ is a copy of the SeaOfStar-ToD fork of Zontex/wii-recomp (GPL-3.0-or-later), kept in
  this repository as plain files (no subtree, no upstream history). Day-to-day runtime
  development happens in the separate clone work/wii-recomp, on its branch seaofstar-tod. This
  script exports that branch with 'git archive', extracts it over runtime/ and deletes files in
  runtime/ that no longer exist in the branch. Review the result with 'git status' and commit it.

  Refuses to run if runtime/ has uncommitted changes in this repository. Nothing is committed
  or pushed.
#>
[CmdletBinding()]
param(
    # The development clone. Defaults to work/wii-recomp inside this repository.
    [string]$Source,
    [string]$Branch = 'seaofstar-tod'
)

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
if (-not $Source) { $Source = Join-Path $root 'work/wii-recomp' }
if (-not (Test-Path -LiteralPath (Join-Path $Source '.git'))) {
    throw "Not a git clone: $Source"
}
$runtime = Join-Path $root 'runtime'

$dirty = & git -C $root status --porcelain -- runtime
if ($dirty) { throw "runtime/ has uncommitted changes; commit or discard them first." }

$pending = & git -C $Source status --porcelain --untracked-files=no
if ($pending) { Write-Item 'source' 'has uncommitted changes; only the committed branch is synced' 'warn' }

Write-Section 'Sync runtime/'
Write-Item 'source' "$Source ($Branch)" 'info'

$wanted = @(& git -C $Source ls-tree -r --name-only $Branch)
if ($LASTEXITCODE -ne 0 -or $wanted.Count -eq 0) { throw "Branch '$Branch' not found or empty in $Source" }

$tar = Join-Path ([IO.Path]::GetTempPath()) ("runtime-sync-" + [guid]::NewGuid().ToString('N') + '.tar')
try {
    Invoke-Checked 'git' @('-C', $Source, 'archive', '--format=tar', "--output=$tar", $Branch)
    New-Item -ItemType Directory -Force -Path $runtime | Out-Null
    Invoke-Checked 'tar' @('-xf', $tar, '-C', $runtime)
} finally {
    Remove-Item -LiteralPath $tar -Force -ErrorAction SilentlyContinue
}

# Delete tracked files that the branch no longer has.
$keep = [Collections.Generic.HashSet[string]]::new([string[]]$wanted)
$removed = 0
foreach ($f in @(& git -C $root ls-files -- runtime)) {
    $rel = $f.Substring('runtime/'.Length)
    if (-not $keep.Contains($rel)) {
        Remove-Item -LiteralPath (Join-Path $root $f) -Force -ErrorAction SilentlyContinue
        $removed++
    }
}
Write-Item 'removed' "$removed file(s) no longer in the branch" 'info'
Write-Item 'runtime/' 'matches the development clone; review with git status and commit' 'ok'
