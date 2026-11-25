# Shared helpers for the SeaOfStar-ToD tooling scripts.
# Dot-source this file from the entry-point scripts.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RepoRoot = Split-Path -Parent $PSScriptRoot

function Get-RepoRoot {
    return $script:RepoRoot
}

# Where the runtime (the wii-recomp fork) is built. A development checkout keeps it in work/wii-recomp
# and syncs it into runtime/ with tools/sync-runtime.ps1; a plain clone only has runtime/.
function Get-RuntimeRoot {
    $dev = Join-Path $script:RepoRoot 'work/wii-recomp'
    if (Test-Path -LiteralPath (Join-Path $dev 'recomp/tools/add_game.py')) { return $dev }
    return (Join-Path $script:RepoRoot 'runtime')
}

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $script:RepoRoot $Path))
}

function Get-GameConfig {
    param([switch]$AllowExample)

    $path = Join-Path $script:RepoRoot 'config/game.json'
    if (-not (Test-Path -LiteralPath $path)) {
        if (-not $AllowExample) {
            throw 'config/game.json not found. Copy config/game.example.json to config/game.json and edit the paths.'
        }
        $path = Join-Path $script:RepoRoot 'config/game.example.json'
        Write-Warning 'config/game.json not found; falling back to config/game.example.json'
    }

    $cfg = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $cfg | Add-Member -NotePropertyName '_configPath' -NotePropertyValue $path -Force
    return $cfg
}

function Get-GamePaths {
    param([Parameter(Mandatory = $true)]$Config)

    return [pscustomobject]@{
        Disc      = Resolve-RepoPath $Config.game.disc
        Extracted = Resolve-RepoPath $Config.game.extracted
        MainDol   = Resolve-RepoPath $Config.recomp.mainDol
        RelDir    = Resolve-RepoPath $Config.recomp.relDir
        Output    = Resolve-RepoPath $Config.recomp.output
        Work      = Resolve-RepoPath $Config.paths.work
        Build     = Resolve-RepoPath $Config.paths.build
        Logs      = Resolve-RepoPath $Config.paths.logs
    }
}

function Get-CommandPath {
    param([Parameter(Mandatory = $true)][string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Write-Section {
    param([Parameter(Mandatory = $true)][string]$Title)
    Write-Host ''
    Write-Host "== $Title ==" -ForegroundColor Cyan
}

function Write-Item {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Value,
        [ValidateSet('ok', 'warn', 'fail', 'info')][string]$State = 'info'
    )

    $tag = switch ($State) {
        'ok'   { '[ ok ]' }
        'warn' { '[warn]' }
        'fail' { '[fail]' }
        default { '[info]' }
    }
    $color = switch ($State) {
        'ok'   { 'Green' }
        'warn' { 'Yellow' }
        'fail' { 'Red' }
        default { 'Gray' }
    }
    Write-Host ("{0} {1,-24} {2}" -f $tag, $Label, $Value) -ForegroundColor $color
}

# Runs a native command, mirrors its output, and throws on a non-zero exit code.
# Native tools write progress to stderr; temporarily relaxing the error preference
# keeps PowerShell 5.1 from turning that into a terminating error.
function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$LogPath
    )

    $display = "$FilePath $($Arguments -join ' ')"
    Write-Host "> $display" -ForegroundColor DarkGray

    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($LogPath) {
            $logDir = Split-Path -Parent $LogPath
            if ($logDir) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }
            & $FilePath @Arguments 2>&1 | Tee-Object -FilePath $LogPath
        }
        else {
            & $FilePath @Arguments 2>&1
        }
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    if ($code -ne 0) {
        throw "Command failed with exit code $code`: $display"
    }
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BeU32 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )
    return ([uint32]$Bytes[$Offset] -shl 24) -bor
           ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
           ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
           [uint32]$Bytes[$Offset + 3]
}

function Read-DolHeader {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x100) {
        throw "DOL is smaller than its 0x100-byte header: $Path"
    }

    $textOffsets = @(); $dataOffsets = @()
    $textAddresses = @(); $dataAddresses = @()
    $textSizes = @(); $dataSizes = @()

    for ($i = 0; $i -lt 7; $i++) { $textOffsets += Get-BeU32 $bytes ($i * 4) }
    for ($i = 0; $i -lt 11; $i++) { $dataOffsets += Get-BeU32 $bytes (28 + $i * 4) }
    for ($i = 0; $i -lt 7; $i++) { $textAddresses += Get-BeU32 $bytes (72 + $i * 4) }
    for ($i = 0; $i -lt 11; $i++) { $dataAddresses += Get-BeU32 $bytes (100 + $i * 4) }
    for ($i = 0; $i -lt 7; $i++) { $textSizes += Get-BeU32 $bytes (144 + $i * 4) }
    for ($i = 0; $i -lt 11; $i++) { $dataSizes += Get-BeU32 $bytes (172 + $i * 4) }

    $sections = @()
    for ($i = 0; $i -lt 7; $i++) {
        if ($textSizes[$i] -gt 0) {
            $sections += [pscustomobject]@{
                kind   = 'text'
                index  = $i
                offset = $textOffsets[$i]
                address = $textAddresses[$i]
                size   = $textSizes[$i]
            }
        }
    }
    for ($i = 0; $i -lt 11; $i++) {
        if ($dataSizes[$i] -gt 0) {
            $sections += [pscustomobject]@{
                kind   = 'data'
                index  = $i
                offset = $dataOffsets[$i]
                address = $dataAddresses[$i]
                size   = $dataSizes[$i]
            }
        }
    }

    return [pscustomobject]@{
        path       = $Path
        size       = $bytes.Length
        entryPoint = Get-BeU32 $bytes 224
        bssAddress = Get-BeU32 $bytes 216
        bssSize    = Get-BeU32 $bytes 220
        sections   = $sections
    }
}

function Find-MiddlewareMarkers {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = [Text.Encoding]::ASCII.GetString($bytes)

    $markers = [ordered]@{
        sofdec   = 'Sofdec'
        cri      = 'CRI'
        cpk      = 'CPK'
        adx      = 'ADX'
        hca      = 'HCA'
        ax       = 'AX'
        wpad     = 'WPAD'
        gx       = 'GX'
        ios      = 'IOS'
        nand     = 'NAND'
        dvd      = 'DVD'
        revolution = 'Revolution'
    }

    $found = [ordered]@{}
    foreach ($key in $markers.Keys) {
        $found[$key] = $text.IndexOf($markers[$key], [StringComparison]::OrdinalIgnoreCase) -ge 0
    }
    return $found
}
