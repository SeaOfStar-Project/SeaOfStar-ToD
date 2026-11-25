# Imports a Wii save for Rune Factory: Tides of Destiny (SO3EE9) into the
# recomp's NAND folder.
#
# Accepts any of these, because save sites hand out all three:
#   - the zip GameFAQs serves (it contains one data.bin)
#   - the data.bin itself
#   - an already unpacked folder: banner.bin plus RFF2_00..RFF2_15
#
# data.bin is decrypted with tools/wii-save-extract.py (needs Python 3 and
# pycryptodome) and needs the SD key and IV from your own console, supplied via
# config\sd-keys.txt or WII_SD_KEY / WII_SD_IV (see config\README.md). Everything in the destination is backed up first, so
# -Restore puts the previous slots back.
#
#   tools\import-save.ps1 -Zip "$HOME\Downloads\SO3E.zip"
#   tools\import-save.ps1 -Bin "$HOME\Downloads\SO3E.bin" -Check
#   tools\import-save.ps1 -Source "C:\saves\SO3E"
#   tools\import-save.ps1 -Restore
#   tools\import-save.ps1 -RestorePristine
#
# After a successful import a pristine copy (with manifest.sha256) is written
# to saves\pristine\<stamp>, because the game rewrites all 16 files on every
# boot. -RestorePristine puts the newest pristine copy back (the current
# folder is kept in saves\backup\before-restore-* first) and checks hashes.
param(
    [string]$Zip,
    [string]$Bin,
    [string]$Source,
    [string]$SaveDir,
    [string]$BackupRoot,
    [string]$PristineRoot,
    [switch]$Restore,
    [switch]$RestorePristine,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
if (-not $SaveDir) {
    $SaveDir = Join-Path $root 'work\wii-recomp\recomp\build\saves\title\00010000\534f3345\data'
}
$backupRoot = if ($BackupRoot) { $BackupRoot } else { Join-Path $root 'work\wii-recomp\recomp\build\saves\backup' }

$pristineRoot = if ($PristineRoot) { $PristineRoot } else { Join-Path $root 'work\wii-recomp\recomp\build\saves\pristine' }

$wanted = @('banner.bin') + (0..15 | ForEach-Object { 'RFF2_{0:d2}' -f $_ })
$expected = @{ 'banner.bin' = 43168 }
0..15 | ForEach-Object { $expected['RFF2_{0:d2}' -f $_] = 47936 }

function Copy-Tree([string]$from, [string]$to) {
    if (-not (Test-Path -LiteralPath $to)) { New-Item -ItemType Directory -Path $to -Force | Out-Null }
    Get-ChildItem -LiteralPath $from -Force | Copy-Item -Destination $to -Recurse -Force
}

function Find-SaveFiles([string]$where) {
    $map = @{}
    if (-not (Test-Path -LiteralPath $where)) { return $map }
    foreach ($name in $wanted) {
        $file = Get-ChildItem -LiteralPath $where -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
            Sort-Object FullName | Select-Object -First 1
        if ($file) { $map[$name] = $file }
    }
    $map
}

function New-DefaultLayout([string]$where) {
    New-Item -ItemType Directory -Path $where -Force | Out-Null
    foreach ($name in $wanted) {
        $path = Join-Path $where $name
        if (-not (Test-Path -LiteralPath $path)) {
            [IO.File]::WriteAllBytes($path, (New-Object byte[] $expected[$name]))
        }
    }
}

function Expand-DataBin([string]$file, [string]$target) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        throw 'python is not on PATH; data.bin needs Python 3 with pycryptodome'
    }

    $tempLayout = $null
    if (Test-Path -LiteralPath $SaveDir) {
        $layout = $SaveDir
    } else {
        # The packed container does not store file names. Any folder with the
        # expected names and sizes is enough to map the payload.
        $tempLayout = Join-Path ([IO.Path]::GetTempPath()) ('seaofstar-layout-' + [guid]::NewGuid().ToString('N'))
        New-DefaultLayout $tempLayout
        $layout = $tempLayout
    }

    try {
        $extractor = Join-Path $PSScriptRoot 'wii-save-extract.py'
        $pyArgs = @($extractor, $file, $target, '--layout', $layout)
        & python @pyArgs
        if ($LASTEXITCODE -ne 0) { throw "could not unpack $file (exit $LASTEXITCODE)" }
    } finally {
        if ($tempLayout -and (Test-Path -LiteralPath $tempLayout)) {
            Remove-Item -LiteralPath $tempLayout -Recurse -Force
        }
    }
}

function Get-Sha256([string]$path) {
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Verifies a folder against a manifest.sha256 ("<hash>  <name>" per line).
# Returns the number of mismatching or missing files.
function Test-Manifest([string]$folder, [string]$manifest) {
    $bad = 0
    foreach ($line in Get-Content -LiteralPath $manifest) {
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { continue }
        $path = Join-Path $folder $Matches[2]
        if (-not (Test-Path -LiteralPath $path) -or (Get-Sha256 $path) -ne $Matches[1]) {
            Write-Host "[fail] hash mismatch or missing: $($Matches[2])"
            $bad++
        }
    }
    $bad
}

function New-PristineCopy([string]$from) {
    $dest = Join-Path $pristineRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    $lines = foreach ($name in $wanted) {
        Copy-Item -LiteralPath (Join-Path $from $name) -Destination (Join-Path $dest $name) -Force
        '{0}  {1}' -f (Get-Sha256 (Join-Path $dest $name)), $name
    }
    [IO.File]::WriteAllText((Join-Path $dest 'manifest.sha256'), (($lines -join "`n") + "`n"))
    if (Test-Manifest $dest (Join-Path $dest 'manifest.sha256')) { throw "pristine copy at $dest failed verification" }
    $dest
}

if ($RestorePristine) {
    $newest = $null
    if (Test-Path -LiteralPath $pristineRoot) {
        $newest = Get-ChildItem -LiteralPath $pristineRoot -Directory |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'manifest.sha256') } |
            Sort-Object Name -Descending | Select-Object -First 1
    }
    if (-not $newest) { throw "no pristine copy in $pristineRoot" }
    $manifest = Join-Path $newest.FullName 'manifest.sha256'
    if (Test-Manifest $newest.FullName $manifest) { throw "pristine copy $($newest.Name) is damaged; nothing was changed" }
    if (Test-Path -LiteralPath $SaveDir) {
        $keep = Join-Path $backupRoot ('before-restore-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Path $keep -Force | Out-Null
        Copy-Tree $SaveDir $keep
        Write-Host "[ ok ] kept the replaced slots in $keep"
    }
    New-Item -ItemType Directory -Path $SaveDir -Force | Out-Null
    foreach ($name in $wanted) {
        Copy-Item -LiteralPath (Join-Path $newest.FullName $name) -Destination (Join-Path $SaveDir $name) -Force
    }
    if (Test-Manifest $SaveDir $manifest) { throw "restored files do not match the manifest of $($newest.Name)" }
    Write-Host "[ ok ] restored pristine $($newest.Name) into $SaveDir; $($wanted.Count) hashes verified"
    return
}

if ($Restore) {
    if (-not (Test-Path -LiteralPath $backupRoot)) { throw "no backups in $backupRoot" }
    $newest = Get-ChildItem -LiteralPath $backupRoot -Directory |
        Where-Object { $_.Name -notlike 'before-restore-*' } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $newest) { throw "no backups in $backupRoot" }
    if (Test-Path -LiteralPath $SaveDir) {
        $keep = Join-Path $backupRoot ('before-restore-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Path $keep -Force | Out-Null
        Copy-Tree $SaveDir $keep
        Write-Host "[ ok ] kept the replaced slots in $keep"
    }
    Copy-Tree $newest.FullName $SaveDir
    Write-Host "[ ok ] restored $($newest.Name) into $SaveDir"
    return
}

if (-not $Zip -and -not $Bin -and -not $Source) {
    throw 'give -Zip (a save archive), -Bin (a data.bin) or -Source (an unpacked folder)'
}

$staging = $null
$binFile = $null

if ($Zip) {
    if (-not (Test-Path -LiteralPath $Zip)) { throw "no such file: $Zip" }
    if ([IO.Path]::GetExtension($Zip) -ieq '.bin') {
        $binFile = Get-Item -LiteralPath $Zip
    } else {
        $staging = Join-Path ([IO.Path]::GetTempPath()) ('seaofstar-save-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $staging | Out-Null
        Write-Host "unpacking $Zip"
        Expand-Archive -LiteralPath $Zip -DestinationPath $staging -Force
        $Source = $staging
    }
}

if ($Bin) {
    if (-not (Test-Path -LiteralPath $Bin)) { throw "no such file: $Bin" }
    $binFile = Get-Item -LiteralPath $Bin
}

try {
    $found = @{}
    if ($Source) {
        if (Test-Path -LiteralPath $Source -PathType Leaf) {
            if ([IO.Path]::GetExtension($Source) -ieq '.bin') { $binFile = Get-Item -LiteralPath $Source }
            else { throw "not a folder or a .bin: $Source" }
        } else {
            $found = Find-SaveFiles $Source
        }
    }

    # One data.bin inside the archive (or handed over directly) instead of a tree.
    if ($found.Count -lt $wanted.Count -and -not $binFile -and $Source) {
        $bins = Get-ChildItem -LiteralPath $Source -Recurse -File -Filter '*.bin' -ErrorAction SilentlyContinue
        if ($bins.Count -gt 1) {
            Write-Host '[warn] more than one .bin in the archive; using the first'
        }
        if ($bins.Count -ge 1) { $binFile = $bins | Sort-Object FullName | Select-Object -First 1 }
    }

    if ($found.Count -lt $wanted.Count -and $binFile) {
        if (-not $staging) {
            $staging = Join-Path ([IO.Path]::GetTempPath()) ('seaofstar-save-' + [guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $staging | Out-Null
        }
        $unpacked = Join-Path $staging 'unpacked'
        New-Item -ItemType Directory -Path $unpacked -Force | Out-Null
        Write-Host "reading $($binFile.Name)"
        Expand-DataBin $binFile.FullName $unpacked
        $found = Find-SaveFiles $unpacked
    }

    if ($found.Count -lt $wanted.Count) {
        $missing = $wanted | Where-Object { -not $found.ContainsKey($_) }
        Write-Host "[fail] $($found.Count) of $($wanted.Count) save files found"
        if ($missing.Count) { Write-Host "       missing: $($missing -join ', ')" }
        exit 1
    }

    # The folder the files came in says which region wrote them.
    $where = $found['banner.bin'].Directory.FullName
    if ($where -match 'SO3J') {
        Write-Host '[warn] these files sit under SO3J: a Japanese save will not load in the US build (SO3EE9).'
    }

    Write-Host ''
    Write-Host ('{0,-12} {1,10} {2,10}  {3}' -f 'file', 'bytes', 'expected', 'state')
    $bad = 0
    foreach ($name in $wanted) {
        $file = $found[$name]
        $want = $expected[$name]
        $state = if ($file.Length -eq $want) { 'ok' } else { $bad++; 'size differs' }
        Write-Host ('{0,-12} {1,10} {2,10}  {3}' -f $name, $file.Length, $want, $state)
    }

    # A slot of zeros is an empty slot; the game preallocates all sixteen at boot.
    $counts = foreach ($i in 0..15) {
        $file = $found['RFF2_{0:d2}' -f $i]
        $bytes = [IO.File]::ReadAllBytes($file.FullName)
        $nonzero = 0
        for ($b = 0; $b -lt $bytes.Length; $b++) { if ($bytes[$b] -ne 0) { $nonzero++ } }
        $nonzero
    }
    $filled = ($counts | Where-Object { $_ -gt 256 }).Count

    Write-Host ''
    Write-Host "slots with data: $filled of 16"
    if ($bad) { Write-Host "[warn] $bad file(s) are not the size this build writes; they may be another version." }
    if (-not $filled) { Write-Host '[warn] every slot is zero: this is an empty save, not a completed one.' }

    if ($Check) {
        Write-Host '[ ok ] checked only; nothing was copied'
        exit 0
    }

    if (Test-Path -LiteralPath $SaveDir) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $backup = Join-Path $backupRoot $stamp
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Tree $SaveDir $backup
        Write-Host "[ ok ] backed up the current slots to $backup"
    }

    New-Item -ItemType Directory -Path $SaveDir -Force | Out-Null
    foreach ($name in $wanted) {
        Copy-Item -LiteralPath $found[$name].FullName -Destination (Join-Path $SaveDir $name) -Force
    }

    $copied = (Get-ChildItem -LiteralPath $SaveDir -File | Where-Object { $wanted -contains $_.Name }).Count
    Write-Host "[ ok ] copied $copied files into $SaveDir"
    $pristine = New-PristineCopy $SaveDir
    Write-Host "[ ok ] wrote pristine copy with manifest.sha256 to $pristine"
    Write-Host '       start the game and open Load; the imported slots should be listed.'
}
finally {
    if ($staging -and (Test-Path -LiteralPath $staging)) { Remove-Item -LiteralPath $staging -Recurse -Force }
}
