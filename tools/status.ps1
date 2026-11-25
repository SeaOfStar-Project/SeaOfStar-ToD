[CmdletBinding()]
param()

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
$config = Get-GameConfig -AllowExample
$paths = Get-GamePaths $config

function Row {
    param([string]$Label, [bool]$Ok, [string]$Detail)
    if ($Ok) { Write-Host ("[ ok ] {0,-22} {1}" -f $Label, $Detail) -ForegroundColor Green }
    else { Write-Host ("[ -- ] {0,-22} {1}" -f $Label, $Detail) -ForegroundColor DarkGray }
}

Write-Host 'SeaOfStar-ToD - project status' -ForegroundColor White

Write-Section 'Configuration'
Row 'config' (Test-Path -LiteralPath $config._configPath) $config._configPath
Row 'title ID' $true $config.game.titleId

Write-Section 'Game data'
Row 'disc image' (Test-Path -LiteralPath $paths.Disc) $paths.Disc
Row 'extracted tree' (Test-Path -LiteralPath $paths.MainDol) $paths.Extracted

if (Test-Path -LiteralPath $paths.Extracted) {
    $files = @(Get-ChildItem -LiteralPath $paths.Extracted -Recurse -File)
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    Write-Host ("       {0} files, {1:N2} GB" -f $files.Count, ($bytes / 1GB)) -ForegroundColor Gray
}

Write-Section 'Analysis'
$manifest = Join-Path $root 'analysis/disc-manifest.json'
$stats = Join-Path $root 'analysis/recompile-stats.json'
Row 'disc manifest' (Test-Path -LiteralPath $manifest) $manifest
Row 'recompile stats' (Test-Path -LiteralPath $stats) $stats

Write-Section 'Recompiled code'
$generatedDir = Join-Path $paths.Output "$($config.game.titleId)_generated"
Row 'generated output' (Test-Path -LiteralPath $generatedDir) $generatedDir

if (Test-Path -LiteralPath $generatedDir) {
    $files = @(Get-ChildItem -LiteralPath $generatedDir -Recurse -File)
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    Write-Host ("       {0} files, {1:N1} MB" -f $files.Count, ($bytes / 1MB)) -ForegroundColor Gray
    $smc = Join-Path $generatedDir "$($config.game.titleId)_smc.txt"
    if (Test-Path -LiteralPath $smc) {
        $count = @(Get-Content -LiteralPath $smc | Where-Object { $_ -match '^0x' }).Count
        Write-Host ("       {0} self-modifying-code candidates" -f $count) -ForegroundColor Yellow
    }
}

Write-Section 'Tools'
Row 'dolrecomp' (Test-Path -LiteralPath (Join-Path $paths.Work 'DolRecomp/build/Release/dolrecomp.exe')) (Join-Path $paths.Work 'DolRecomp/build/Release/dolrecomp.exe')
Row 'nodtool' (Test-Path -LiteralPath (Join-Path $paths.Work 'tools/nodtool.exe')) (Join-Path $paths.Work 'tools/nodtool.exe')
$runtimeRoot = Get-RuntimeRoot
Row 'runtime sources' (Test-Path -LiteralPath (Join-Path $runtimeRoot 'recomp/tools/add_game.py')) $runtimeRoot

Write-Section 'Runtime'
$console = Join-Path $runtimeRoot 'recomp/build/wii-recomp.exe'
$launcher = Join-Path $paths.Build 'launcher/SeaOfStar-ToD.exe'
Row 'wii-recomp.exe' (Test-Path -LiteralPath $console) $console
Row 'SeaOfStar-ToD.exe' (Test-Path -LiteralPath $launcher) $launcher

Write-Section 'Upscaling'
Write-Host '  FSR 1.0   EASU + RCAS in the runtime (WII_RECOMP_FSR): done and verified' -ForegroundColor Green
Write-Host '  DLSS      NGX starts, creates a feature and evaluates a synthetic frame,' -ForegroundColor Yellow
Write-Host '            but the game feeds it nothing yet (WII_RECOMP_NGX, ADR-012)' -ForegroundColor Yellow

Write-Section 'Baseline'
$titleDll = Join-Path $runtimeRoot 'recomp/build-SO3EE9/title.dll'
$baseline = Join-Path $root 'analysis/wii-recomp-baseline.md'
Row 'title.dll' (Test-Path -LiteralPath $titleDll) $titleDll
Row 'baseline report' (Test-Path -LiteralPath $baseline) $baseline

Write-Section 'Next step'
if (-not (Test-Path -LiteralPath $manifest)) {
    Write-Host '  tools/extract-disc.ps1'
}
elseif (-not (Test-Path -LiteralPath $generatedDir)) {
    Write-Host '  tools/recompile.ps1'
}
elseif (Test-Path -LiteralPath $baseline) {
    Write-Host '  Baseline, launcher and FSR are done. Save/load still needs a save point,'
    Write-Host '  and the M12 full playthrough has not been attempted. See docs/07-decisions.md.'
}
else {
    Write-Host '  tools/wii-recomp-boot.ps1 -Seconds 60'
    Write-Host '  See docs/02-milestones.md (B1) and docs/03-runtime-checklist.md.'
}
