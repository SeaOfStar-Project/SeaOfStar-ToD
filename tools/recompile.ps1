[CmdletBinding()]
param(
    [ValidateSet('c', 'llvm')][string]$Backend = 'c',
    [switch]$Force
)

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
$config = Get-GameConfig
$paths = Get-GamePaths $config

$dolrecomp = Join-Path $paths.Work 'DolRecomp/build/Release/dolrecomp.exe'
if (-not (Test-Path -LiteralPath $dolrecomp)) {
    $dolrecomp = Join-Path $paths.Work 'DolRecomp/build/Debug/dolrecomp.exe'
}
if (-not (Test-Path -LiteralPath $dolrecomp)) {
    throw 'dolrecomp.exe not found. Run tools/fetch-tools.ps1 first.'
}

if (-not (Test-Path -LiteralPath $paths.MainDol)) {
    throw "main.dol not found: $($paths.MainDol). Run tools/extract-disc.ps1 first."
}

New-Item -ItemType Directory -Force -Path $paths.Output, $paths.Logs | Out-Null

# DolRecomp only enables Wii/Broadway mode when database/titles.txt exists. Seed it
# with this project's title so a fresh checkout does not silently fall back to Gekko.
$titlesPath = Join-Path $root 'database/titles.txt'
if (-not (Test-Path -LiteralPath $titlesPath)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'database') | Out-Null
    @(
        "$($config.game.titleId) = $($config.game.name)",
        '# Local seed for DolRecomp. Replace with the full GameTDB titles.txt if available.'
    ) | Set-Content -LiteralPath $titlesPath -Encoding ASCII
    Write-Item 'titles database' 'seeded locally for Wii mode' 'warn'
}

if ($Force -and (Test-Path -LiteralPath $paths.Output)) {
    Write-Item 'generated output' 'removing existing output' 'warn'
    Remove-Item -LiteralPath $paths.Output -Recurse -Force
}

Write-Section 'Recompile main.dol'

$arguments = @()
if ($Backend -ne 'c') { $arguments += "--backend=$Backend" }
if ($config.recomp.map) { $arguments += @('--map', (Resolve-RepoPath $config.recomp.map)) }
if ($config.recomp.config) { $arguments += @('--config', (Resolve-RepoPath $config.recomp.config)) }
$arguments += @($paths.MainDol, $config.game.titleId, $paths.Output)

$logPath = Join-Path $paths.Logs 'recompile.log'
Push-Location $root
try {
    Invoke-Checked $dolrecomp $arguments $logPath
}
finally {
    Pop-Location
}

Write-Section 'Recompile REL modules'

$relFiles = @()
if (Test-Path -LiteralPath $paths.RelDir) {
    $relFiles = @(Get-ChildItem -LiteralPath $paths.RelDir -Recurse -Filter *.rel -File -ErrorAction SilentlyContinue)
}

if ($relFiles.Count -gt 0) {
    $relArguments = @()
    if ($Backend -ne 'c') { $relArguments += "--backend=$Backend" }
    if ($config.recomp.map) { $relArguments += @('--map', (Resolve-RepoPath $config.recomp.map)) }
    if ($config.recomp.config) { $relArguments += @('--config', (Resolve-RepoPath $config.recomp.config)) }
    $relArguments += @($paths.RelDir, $config.game.titleId, $paths.Output)
    Push-Location $root
    try {
        Invoke-Checked $dolrecomp $relArguments (Join-Path $paths.Logs 'recompile-rel.log')
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Item 'REL modules' 'none found; skipping folder recompilation' 'info'
}

Write-Section 'Stats'

$generatedFiles = @(Get-ChildItem -LiteralPath $paths.Output -Recurse -File -ErrorAction SilentlyContinue)
$generatedBytes = ($generatedFiles | Measure-Object -Property Length -Sum).Sum
$sourceFiles = @($generatedFiles | Where-Object { $_.Extension -in @('.c', '.cpp', '.h', '.hpp') })

Write-Item 'generated files' $generatedFiles.Count 'ok'
Write-Item 'source files' $sourceFiles.Count 'ok'
Write-Item 'generated size' ('{0:N1} MB' -f ($generatedBytes / 1MB)) 'ok'

$stats = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    backend     = $Backend
    titleId     = $config.game.titleId
    mainDol     = $paths.MainDol
    output      = $paths.Output
    files       = $generatedFiles.Count
    sourceFiles = $sourceFiles.Count
    bytes       = [int64]$generatedBytes
}

$analysisDir = Join-Path $root 'analysis'
New-Item -ItemType Directory -Force -Path $analysisDir | Out-Null
$stats | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $analysisDir 'recompile-stats.json') -Encoding UTF8

Write-Section 'Result'
Write-Host 'CPU recompilation complete.' -ForegroundColor Green
