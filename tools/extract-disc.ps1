[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$SkipHash
)

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
$config = Get-GameConfig
$paths = Get-GamePaths $config

Write-Section 'Inputs'

if (-not (Test-Path -LiteralPath $paths.Disc)) {
    throw "Disc image not found: $($paths.Disc). Edit config/game.json or put your dump there."
}
Write-Item 'disc' $paths.Disc 'ok'
Write-Item 'title ID' $config.game.titleId 'info'
Write-Item 'output' $paths.Extracted 'info'

if ((Test-Path -LiteralPath $paths.MainDol) -and -not $Force) {
    Write-Item 'extracted tree' 'already present (use -Force to re-extract)' 'ok'
}
else {
    if ($Force -and (Test-Path -LiteralPath $paths.Extracted)) {
        Write-Item 'extracted tree' 'removing existing tree' 'warn'
        Remove-Item -LiteralPath $paths.Extracted -Recurse -Force
    }

    $nodtool = Get-CommandPath 'nodtool'
    if (-not $nodtool) {
        $localNodtool = Join-Path $paths.Work 'tools/nodtool.exe'
        if (Test-Path -LiteralPath $localNodtool) { $nodtool = $localNodtool }
    }

    $wit = Get-CommandPath 'wit'

    if ($nodtool) {
        Write-Section 'Extract with nodtool'
        New-Item -ItemType Directory -Force -Path $paths.Extracted, $paths.Logs | Out-Null
        Invoke-Checked $nodtool @('extract', '-p', 'data', $paths.Disc, $paths.Extracted) (Join-Path $paths.Logs 'extract.log')
    }
    elseif ($wit) {
        Write-Section 'Extract with wit'
        New-Item -ItemType Directory -Force -Path $paths.Extracted, $paths.Logs | Out-Null
        Invoke-Checked $wit @('extract', $paths.Disc, $paths.Extracted) (Join-Path $paths.Logs 'extract.log')
    }
    else {
        throw 'No disc extraction tool found. Run tools/fetch-tools.ps1 (nodtool) or install Wiimms ISO Tools (wit).'
    }
}

if (-not (Test-Path -LiteralPath $paths.MainDol)) {
    throw "Extraction finished but main.dol is missing: $($paths.MainDol)"
}

Write-Section 'Analyze'

$dol = Read-DolHeader $paths.MainDol
Write-Item 'entry point' ('0x{0:X8}' -f $dol.entryPoint) 'ok'
foreach ($section in $dol.sections) {
    Write-Item ("{0}{1}" -f $section.kind, $section.index) (
        'addr=0x{0:X8} size=0x{1:X} offset=0x{2:X}' -f $section.address, $section.size, $section.offset
    ) 'info'
}

$relFiles = @(Get-ChildItem -LiteralPath $paths.Extracted -Recurse -Filter *.rel -File -ErrorAction SilentlyContinue)
$rsoFiles = @(Get-ChildItem -LiteralPath $paths.Extracted -Recurse -Filter *.rso -File -ErrorAction SilentlyContinue)
$selFiles = @(Get-ChildItem -LiteralPath $paths.Extracted -Recurse -Filter *.sel -File -ErrorAction SilentlyContinue)

Write-Item 'REL modules' $relFiles.Count 'info'
Write-Item 'RSO modules' $rsoFiles.Count 'info'
Write-Item 'SEL modules' $selFiles.Count 'info'
foreach ($file in $rsoFiles) {
    Write-Item '  RSO' ("{0} ({1:N1} KB)" -f $file.FullName.Substring($paths.Extracted.Length).TrimStart('\'), ($file.Length / 1KB)) 'info'
}

$markers = Find-MiddlewareMarkers $paths.MainDol
$markerNames = @($markers.Keys | Where-Object { $markers[$_] })
Write-Item 'main.dol markers' ($markerNames -join ', ') 'info'

$allFiles = @(Get-ChildItem -LiteralPath $paths.Extracted -Recurse -File)
$totalBytes = ($allFiles | Measure-Object -Property Length -Sum).Sum
$topLevel = @()
foreach ($dir in Get-ChildItem -LiteralPath $paths.Extracted -Directory) {
    $files = @(Get-ChildItem -LiteralPath $dir.FullName -Recurse -File)
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    $topLevel += [pscustomobject]@{
        name  = $dir.Name
        files = $files.Count
        bytes = [int64]$bytes
    }
}

Write-Item 'inventory' ("{0} files, {1:N2} GB" -f $allFiles.Count, ($totalBytes / 1GB)) 'ok'

$manifest = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    game        = [ordered]@{
        name     = $config.game.name
        titleId  = $config.game.titleId
        region   = $config.game.region
        disc     = $config.game.disc
    }
    disc        = [ordered]@{
        path   = $paths.Disc
        bytes  = (Get-Item -LiteralPath $paths.Disc).Length
        sha256 = if ($SkipHash) { $null } else { Get-Sha256 $paths.Disc }
    }
    mainDol     = [ordered]@{
        path       = $paths.MainDol
        bytes      = $dol.size
        sha256     = Get-Sha256 $paths.MainDol
        entryPoint = ('0x{0:X8}' -f $dol.entryPoint)
        bssAddress = ('0x{0:X8}' -f $dol.bssAddress)
        bssSize    = ('0x{0:X}' -f $dol.bssSize)
        sections   = $dol.sections
    }
    modules     = [ordered]@{
        rel = @($relFiles | ForEach-Object { $_.FullName.Substring($paths.Extracted.Length).TrimStart('\') })
        rso = @($rsoFiles | ForEach-Object { [ordered]@{ path = $_.FullName.Substring($paths.Extracted.Length).TrimStart('\'); bytes = $_.Length } })
        sel = @($selFiles | ForEach-Object { [ordered]@{ path = $_.FullName.Substring($paths.Extracted.Length).TrimStart('\'); bytes = $_.Length } })
    }
    middleware  = $markers
    inventory   = [ordered]@{
        files      = $allFiles.Count
        bytes      = [int64]$totalBytes
        topLevel   = $topLevel
    }
}

$analysisDir = Join-Path $root 'analysis'
New-Item -ItemType Directory -Force -Path $analysisDir | Out-Null
$manifestPath = Join-Path $analysisDir 'disc-manifest.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Section 'Result'
Write-Item 'manifest' $manifestPath 'ok'
Write-Host 'Extraction and baseline analysis complete.' -ForegroundColor Green
