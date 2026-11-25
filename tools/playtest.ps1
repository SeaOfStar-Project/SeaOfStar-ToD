<#
.SYNOPSIS
  The single entry point for every game test run (see "Playtests" in docs/04-tooling.md).

.DESCRIPTION
  Takes work\playtest.lock, refuses to start while a game, build or other playtest is running,
  seeds a private save folder from the newest pristine copy, starts wii-recomp.exe with a unique
  control pipe, plays an optional timeline, saves pictures, and always kills the process tree
  and releases the lock in a finally block (also on Ctrl+C).

  Timeline lines (same format as logs\play-script.ps1), seconds from the moment the pipe is up:
      70.0 hold a
      70.06 release a
      73 shot probe-73.ppm        (relative paths go under the run's shots\ folder)
  Verbs are the control-pipe commands (press, hold, release, stick, pointer, shot, ...).
  'stick x y' moves the Nunchuk stick and the Classic Controller's left stick together,
  like the keyboard's WASD (menus read the Nunchuk stick).
  'gshot' is accepted as an alias of 'shot'.

.EXAMPLE
  tools\playtest.ps1 -Tag boot1 -Seconds 240 -Every 5
  tools\playtest.ps1 -Tag loadprobe -Script logs\loadprobe.txt -DryRun

  Graphics settings: by default the run reads config\launcher.ini [options] (fsr, sharpness, scale,
  window, present; fullscreen is ignored) and sets the same WII_RECOMP_* variables the launcher
  sets (launcher/src/main.cpp BuildEnvironment), so frame times match real play. -Scale <s>
  (e.g. 2 or 3/2) overrides the scale; -RuntimeDefaults skips the ini (runtime defaults, render
  scale 2). A WII_RECOMP_* variable already set in the calling environment wins over the ini.
  Each value is printed with its source and recorded in summary.json.

  -NoControl does not set WII_RECOMP_CONTROL and opens no pipe (input/mouse behave as from the
  launcher); it is refused together with -Script or -Every > 0.

  -Tree boots the extracted disc tree (extracted\SO3EE9\sys) with the loose title.dll
  (WII_RECOMP_NATIVE), exactly like the launcher does when it does not use the packed image.
#>
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$Seconds = 0,
    [string]$Script,
    [double]$Every = 0,
    [switch]$Window,
    [switch]$Coverage,
    [switch]$Tree,
    [switch]$SeedShared,
    [string]$Pristine,
    [string]$Scale,
    [switch]$RuntimeDefaults,
    [switch]$NoControl,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$inv = [Globalization.CultureInfo]::InvariantCulture

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'work\wii-recomp\recomp\build\wii-recomp.exe'
$image = Join-Path $root 'work\wii-recomp\games\SO3EE9.wrd'
$discTree = Join-Path $root 'extracted\SO3EE9\sys'
$titleDll = Join-Path $root 'work\wii-recomp\recomp\build-SO3EE9\title.dll'
$buildDir = Join-Path $root 'work\wii-recomp\recomp\build'
$pristineRoot = Join-Path $buildDir 'saves\pristine'
$sharedTitleData = Join-Path $buildDir 'title-data'
$liveShared2 = Join-Path $buildDir 'saves\shared2'
$lockPath = Join-Path $root 'work\playtest.lock'
$titleId = '00010000-534f3345'
$saveRel = 'title\00010000\534f3345\data'

if ($Tag -notmatch '^[A-Za-z0-9._-]+$') { throw "bad -Tag '$Tag' (letters, digits, . _ - only)" }

function Fail([string]$msg, [int]$code = 2) {
    [Console]::Error.WriteLine("playtest: REFUSED/FAILED: $msg")
    exit $code
}

# ---- static checks -------------------------------------------------------------------------
if ($NoControl -and $Script) { Fail '-NoControl cannot be combined with -Script (no control pipe, so a timeline cannot be played)' }
if ($NoControl -and $Every -gt 0) { Fail '-NoControl cannot be combined with -Every > 0 (no control pipe, so shots cannot be taken)' }
$needs = if ($Tree) { @($exe, $discTree, $titleDll) } else { @($exe, $image) }
foreach ($need in $needs) {
    if (-not (Test-Path -LiteralPath $need)) { Fail "missing $need" }
}
if (-not $Coverage -and -not (Test-Path -LiteralPath (Join-Path $sharedTitleData $titleId))) {
    Fail "no title-data to copy at $sharedTitleData\$titleId"
}

# Timeline
$events = @()
$lastEvent = 0.0
$scriptPath = $null
if ($Script) {
    $scriptPath = if (Test-Path -LiteralPath $Script) { (Resolve-Path -LiteralPath $Script).Path } else { Join-Path $root $Script }
    if (-not (Test-Path -LiteralPath $scriptPath)) { Fail "no timeline at $scriptPath" }
    foreach ($line in Get-Content -LiteralPath $scriptPath) {
        $trim = $line.Trim()
        if (-not $trim -or $trim.StartsWith('#')) { continue }
        $parts = $trim -split '\s+', 3
        if ($parts.Count -lt 2) { Fail "bad timeline line: $line" }
        $at = 0.0
        if (-not [double]::TryParse($parts[0], [Globalization.NumberStyles]::Float, $inv, [ref]$at)) { Fail "bad time in timeline line: $line" }
        $verb = $parts[1]
        if ($verb -eq 'gshot') { $verb = 'shot' }
        $arg = if ($parts.Count -ge 3) { $parts[2] } else { '' }
        $events += [pscustomobject]@{ At = $at; Verb = $verb; Arg = $arg; Done = $false }
        if ($at -gt $lastEvent) { $lastEvent = $at }
    }
    $events = @($events | Sort-Object At)
}
if ($Seconds -le 0) {
    $Seconds = if ($scriptPath) { [int][Math]::Ceiling($lastEvent) + 3 } else { 120 }
}

# Pristine save copy: the named folder (any name), else the newest YYYYMMDD-HHMMSS stamp that has a manifest.
if (-not (Test-Path -LiteralPath $pristineRoot)) { Fail "no pristine save copies at $pristineRoot (nothing to seed from)" }
$pristineDir = $null
if ($Pristine) {
    $pristineDir = Join-Path $pristineRoot $Pristine
    if (-not (Test-Path -LiteralPath $pristineDir)) { Fail "pristine stamp '$Pristine' not found in $pristineRoot" }
} else {
    $cand = Get-ChildItem -LiteralPath $pristineRoot -Directory |
        Where-Object { $_.Name -match '^\d{8}-\d{6}$' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'manifest.sha256')) } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $cand) { Fail "no pristine save copy with manifest.sha256 under $pristineRoot" }
    $pristineDir = $cand.FullName
}
$pristineStamp = Split-Path $pristineDir -Leaf
$manifestPath = Join-Path $pristineDir 'manifest.sha256'
if (-not (Test-Path -LiteralPath $manifestPath)) { Fail "no manifest.sha256 in $pristineDir" }
$manifest = [ordered]@{}
foreach ($line in Get-Content -LiteralPath $manifestPath) {
    if ($line -match '^\s*([0-9A-Fa-f]{64})\s+\*?(.+?)\s*$') { $manifest[$Matches[2]] = $Matches[1].ToLowerInvariant() }
}
if ($manifest.Count -eq 0) { Fail "manifest $manifestPath lists no files" }

function Get-HashMap([string]$dir, [string[]]$names) {
    $map = [ordered]@{}
    foreach ($n in $names) {
        $f = Join-Path $dir $n
        if (Test-Path -LiteralPath $f) { $map[$n] = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLowerInvariant() }
        else { $map[$n] = 'MISSING' }
    }
    return $map
}
function Test-HashMap($expected, $actual) {
    $bad = @()
    foreach ($k in $expected.Keys) { if ($actual[$k] -ne $expected[$k]) { $bad += $k } }
    return $bad
}
$srcBad = Test-HashMap $manifest (Get-HashMap $pristineDir @($manifest.Keys))
if ($srcBad.Count -gt 0) { Fail "pristine copy $pristineStamp does not match its manifest: $($srcBad -join ', ')" }

# ---- collision checks ----------------------------------------------------------------------
function Test-PidAlive([int]$p) { return [bool](Get-Process -Id $p -ErrorAction SilentlyContinue) }

function Get-Collisions {
    $found = @()
    foreach ($n in @('wii-recomp', 'ninja', 'clang', 'SeaOfStar-ToD', 'SolarFlare-ToD', 'rftod-launcher')) {
        foreach ($pr in @(Get-Process -Name $n -ErrorAction SilentlyContinue)) { $found += "$($pr.ProcessName).exe pid $($pr.Id)" }
    }
    try {
        foreach ($c in @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" -ErrorAction Stop)) {
            if ($c.ProcessId -ne $PID -and $c.CommandLine -match 'wii-recomp-build\.ps1|wii-recomp-boot\.ps1|playtest\.ps1') {
                $found += "script pid $($c.ProcessId): $($c.CommandLine)"
            }
        }
    } catch { }
    return $found
}

function Read-Lock {
    if (-not (Test-Path -LiteralPath $lockPath)) { return $null }
    try { return (Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json) } catch { return [pscustomobject]@{ pid = 0; tag = '?'; start = '?' } }
}
# Returns a refusal text, or $null when the lock is free or stale (stale lock removed unless -Check).
function Get-LockRefusal([switch]$Check) {
    $l = Read-Lock
    if (-not $l) { return $null }
    $lp = 0
    [void][int]::TryParse([string]$l.pid, [ref]$lp)
    if ($lp -gt 0 -and (Test-PidAlive $lp)) { return "another playtest holds $lockPath (pid $lp, tag $($l.tag), started $($l.start))" }
    if (-not $Check) { Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue }
    return $null
}

$refusal = Get-LockRefusal -Check:$DryRun
if ($refusal) { Fail $refusal }
$coll = @(Get-Collisions)
if ($coll.Count -gt 0) { Fail ("something else is running: " + ($coll -join '; ')) }

# ---- plan ----------------------------------------------------------------------------------
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $root "work\playtests\$Tag-$stamp"
$shotsDir = Join-Path $runDir 'shots'
$runSaves = Join-Path $runDir 'saves'
$runSaveData = Join-Path $runSaves $saveRel
$runShared2 = Join-Path $runSaves 'shared2'
$seedShared2 = [bool]($SeedShared -and (Test-Path -LiteralPath $liveShared2))
$shared2Text = if ($seedShared2) { "copied from $liveShared2" } else { 'fresh (game makes a new one)' }
$runTitleData = if ($Coverage) { $sharedTitleData } else { Join-Path $runDir 'title-data' }
$outLog = Join-Path $runDir 'run.out.log'
$errLog = Join-Path $runDir 'run.err.log'
$pipeName = 'pt-' + [guid]::NewGuid().ToString('N').Substring(0, 12)

$envPlan = [ordered]@{
    WII_RECOMP_SAVES        = $runSaves
    WII_RECOMP_TITLE_DATA   = $runTitleData
    WII_RECOMP_FPS_OVERLAY  = '0'
}
if (-not $NoControl) { $envPlan['WII_RECOMP_CONTROL'] = $pipeName }
if (-not $Window) { $envPlan['WII_RECOMP_NO_WINDOW'] = '1' }

# Graphics settings: same variables and mapping as the launcher's BuildEnvironment.
# Priority: -Scale (scale only) > variable already set by the caller > launcher.ini > runtime default.
$iniPath = Join-Path $root 'config\launcher.ini'
$iniOpts = @{}
$iniNote = 'runtime defaults (-RuntimeDefaults)'
if (-not $RuntimeDefaults) {
    if (Test-Path -LiteralPath $iniPath) {
        $iniNote = $iniPath
        $inOptions = $false
        foreach ($line in Get-Content -LiteralPath $iniPath) {
            $t = $line.Trim()
            if ($t -match '^\[(.+)\]$') { $inOptions = ($Matches[1] -eq 'options'); continue }
            if ($inOptions -and $t -match '^([^=;#]+?)\s*=\s*(.*)$') { $iniOpts[$Matches[1].ToLowerInvariant()] = $Matches[2].Trim() }
        }
    } else {
        $iniNote = "runtime defaults ($iniPath missing)"
        Write-Host "[warn] $iniPath not found; using runtime graphics defaults"
    }
}
$iniEnv = [ordered]@{}
if ($iniOpts.ContainsKey('fsr')) { $iniEnv['WII_RECOMP_FSR'] = $(if ($iniOpts['fsr'] -ne '0' -and $iniOpts['fsr'] -ne '') { '1' } else { '0' }) }
if ($iniOpts.ContainsKey('sharpness')) {
    $sv = 0.0
    if ([double]::TryParse($iniOpts['sharpness'], [Globalization.NumberStyles]::Float, $inv, [ref]$sv)) {
        $iniEnv['WII_RECOMP_FSR_SHARPNESS'] = $sv.ToString('0.00', $inv)
    }
}
function ConvertTo-ScaleText([double]$v) {
    if ([Math]::Abs($v - 1.5) -lt 0.01) { return '3/2' }
    if ([Math]::Abs($v - 2.5) -lt 0.01) { return '5/2' }
    return [string][int][Math]::Round($v)
}
if ($iniOpts.ContainsKey('scale')) {
    $cv = 0.0
    if ([double]::TryParse($iniOpts['scale'], [Globalization.NumberStyles]::Float, $inv, [ref]$cv)) {
        $iniEnv['WII_RECOMP_SCALE'] = ConvertTo-ScaleText $cv
    }
}
if ($iniOpts.ContainsKey('window') -and $iniOpts['window']) { $iniEnv['WII_RECOMP_WINDOW'] = $iniOpts['window'] }
if ($iniOpts.ContainsKey('present') -and $iniOpts['present']) { $iniEnv['WII_RECOMP_PRESENT'] = $iniOpts['present'] }
if ($Scale) {
    if ($Scale -notmatch '^\d+(/\d+)?$') { Fail "bad -Scale '$Scale' (use e.g. 2 or 3/2)" }
}
$graphics = [ordered]@{}
foreach ($gk in @('WII_RECOMP_SCALE', 'WII_RECOMP_FSR', 'WII_RECOMP_FSR_SHARPNESS', 'WII_RECOMP_WINDOW', 'WII_RECOMP_PRESENT')) {
    $parentVal = [Environment]::GetEnvironmentVariable($gk)
    if ($gk -eq 'WII_RECOMP_SCALE' -and $Scale) {
        $envPlan[$gk] = $Scale; $graphics[$gk] = [ordered]@{ value = $Scale; source = '-Scale' }
    } elseif ($parentVal) {
        $graphics[$gk] = [ordered]@{ value = $parentVal; source = 'parent environment' }
    } elseif ($iniEnv.Contains($gk)) {
        $envPlan[$gk] = $iniEnv[$gk]; $graphics[$gk] = [ordered]@{ value = $iniEnv[$gk]; source = 'launcher.ini' }
    } else {
        $graphics[$gk] = [ordered]@{ value = '(unset)'; source = 'runtime default' }
    }
}

$mode = if ($DryRun) { 'DRY RUN, nothing started' } else { 'live' }
$tlText = if ($scriptPath) { "$scriptPath ($($events.Count) events, last at $($lastEvent)s)" } else { '(none)' }
$shotText = if ($Every -gt 0) { "$Every s" } else { 'off' }
$winText = if ($Window) { 'shown' } else { 'windowless' }
$tdText = if ($Coverage) { "SHARED (coverage run writes shaders.bin/profile.txt): $sharedTitleData" } else { "private copy of $sharedTitleData" }
Write-Host "playtest plan ($mode)"
Write-Host "  tag            : $Tag"
Write-Host "  run folder     : $runDir"
Write-Host "  exe            : $exe"
$sourceText = if ($Tree) { "tree $discTree" } else { "image $image" }
$launchTarget = if ($Tree) { $discTree } else { $image }
if ($Tree) { $envPlan['WII_RECOMP_NATIVE'] = $titleDll }
Write-Host "  source         : $sourceText"
Write-Host "  seconds        : $Seconds"
Write-Host "  timeline       : $tlText"
Write-Host "  shot every     : $shotText"
Write-Host "  control pipe   : $(if ($NoControl) { 'none (-NoControl: input/mouse exactly as from the launcher)' } else { $pipeName })"
Write-Host "  window         : $winText"
Write-Host "  title-data     : $tdText"
Write-Host "  pristine saves : $pristineStamp ($($manifest.Count) files, manifest verified) -> $runSaveData"
Write-Host "  shared2        : $shared2Text"
Write-Host "  lock           : $lockPath (free)"
Write-Host "  PATH prepend   : C:\msys64\clang64\bin"
Write-Host "  graphics       : from $iniNote (fullscreen ignored)"
foreach ($gk in $graphics.Keys) { Write-Host ("    {0}={1}  [{2}]" -f $gk, $graphics[$gk].value, $graphics[$gk].source) }
Write-Host "  env:"
foreach ($k in $envPlan.Keys) { Write-Host ("    {0}={1}" -f $k, $envPlan[$k]) }
if ($DryRun) { Write-Host 'dry run: all checks passed, nothing created or started.'; exit 0 }

# ---- live run ------------------------------------------------------------------------------
$lockHeld = $false
$proc = $null
$pipe = $null
$writer = $null
$exitReason = 'error'
$startedAt = Get-Date
$shotCount = 0
$hashesBefore = $null
$hashesAfter = $null
$dataNames = @($manifest.Keys)
$summaryPath = Join-Path $runDir 'summary.json'

function Grab([string]$path) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    $writer.WriteLine("shot $path")
    for ($i = 0; $i -lt 80; $i++) {
        if (Test-Path -LiteralPath $path) {
            try { $s = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite'); $s.Close(); return $true } catch { }
        }
        Start-Sleep -Milliseconds 50
    }
    return $false
}

try {
    # Atomic lock: CreateNew fails if the file exists.
    $lockBody = (@{ pid = $PID; tag = $Tag; start = $startedAt.ToString('o') } | ConvertTo-Json -Compress)
    $fs = $null
    try {
        $fs = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    } catch [System.IO.IOException] {
        throw "could not take $lockPath (another playtest started first)"
    }
    $lockHeld = $true
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($lockBody)
        $fs.Write($bytes, 0, $bytes.Length)
    } finally { $fs.Dispose() }

    New-Item -ItemType Directory -Force -Path $runDir, $shotsDir, $runSaveData | Out-Null
    foreach ($n in $manifest.Keys) { Copy-Item -LiteralPath (Join-Path $pristineDir $n) -Destination (Join-Path $runSaveData $n) -Force }
    $hashesBefore = Get-HashMap $runSaveData $dataNames
    $seedBad = Test-HashMap $manifest $hashesBefore
    if ($seedBad.Count -gt 0) { throw "seeded saves do not match manifest: $($seedBad -join ', ')" }
    if ($seedShared2) {
        # Read-only source: the live console folder is only read, never written.
        New-Item -ItemType Directory -Force -Path $runShared2 | Out-Null
        Copy-Item -Path (Join-Path $liveShared2 '*') -Destination $runShared2 -Recurse -Force
    }
    if (-not $Coverage) {
        $dst = Join-Path $runDir 'title-data'
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Copy-Item -Path (Join-Path $sharedTitleData '*') -Destination $dst -Recurse -Force
    }

    foreach ($k in $envPlan.Keys) { Set-Item -Path "Env:$k" -Value $envPlan[$k] }
    if ($Window) { Remove-Item Env:WII_RECOMP_NO_WINDOW -ErrorAction SilentlyContinue }
    $env:PATH = 'C:\msys64\clang64\bin;' + $env:PATH

    $proc = Start-Process -FilePath $exe -ArgumentList ('"' + $launchTarget + '"') -WorkingDirectory (Split-Path $exe -Parent) -PassThru -RedirectStandardError $errLog -RedirectStandardOutput $outLog
    Write-Host "started pid $($proc.Id), pipe $(if ($NoControl) { '(none)' } else { $pipeName })"

    $deadline = (Get-Date).AddSeconds(25)
    while (-not $NoControl -and -not $pipe -and (Get-Date) -lt $deadline -and -not $proc.HasExited) {
        try {
            $cand = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::Out)
            $cand.Connect(500)
            $pipe = $cand
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if ($pipe) {
        $writer = New-Object System.IO.StreamWriter($pipe)
        $writer.AutoFlush = $true
    } elseif (-not $NoControl) {
        Write-Host '[warn] no control pipe; running blind (no input, no shots)'
    }

    $clock = [Diagnostics.Stopwatch]::StartNew()
    $nextShot = if ($Every -gt 0) { 3.0 } else { [double]::MaxValue }
    $exitReason = 'timeout'
    while ($true) {
        $t = $clock.Elapsed.TotalSeconds
        if ($proc.HasExited) { $exitReason = "game-exited(code $($proc.ExitCode))"; break }
        if ($t -ge $Seconds) { $exitReason = 'timeout'; break }
        if ($writer) {
            foreach ($e in @($events | Where-Object { -not $_.Done -and $t -ge $_.At })) {
                $e.Done = $true
                if ($e.Verb -eq 'shot') {
                    $p = $e.Arg
                    if (-not [IO.Path]::IsPathRooted($p)) { $p = Join-Path $shotsDir $p }
                    $ok = Grab $p
                    if ($ok) { $shotCount++ }
                    Write-Host ("  > shot {0} at {1:N2}s ok={2}" -f $p, $t, $ok)
                } else {
                    $cmd = $e.Verb
                    if ($e.Arg) { $cmd += ' ' + $e.Arg }
                    $writer.WriteLine($cmd)
                    Write-Host ("  > {0} at {1:N2}s" -f $cmd, $t)
                }
            }
            if ($t -ge $nextShot) {
                $p = Join-Path $shotsDir ("t{0:D4}.ppm" -f [int][Math]::Round($t))
                $ok = Grab $p
                if ($ok) { $shotCount++ }
                Write-Host ("  > auto shot t={0:N1}s ok={1}" -f $t, $ok)
                $nextShot = $clock.Elapsed.TotalSeconds + $Every
            }
        }
        Start-Sleep -Milliseconds 20
    }
}
catch {
    $exitReason = "error: $($_.Exception.Message)"
    # Print the original error first (with its script line) so cleanup problems cannot mask it.
    $where = if ($_.InvocationInfo) { "$($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $($_.InvocationInfo.Line.Trim())" } else { '(no location)' }
    Write-Host "playtest: ERROR $($_.Exception.GetType().FullName): $($_.Exception.Message)"
    Write-Host "playtest:   at $where"
    if ($_.ScriptStackTrace) { Write-Host "playtest:   stack: $($_.ScriptStackTrace -replace '\r?\n', ' | ')" }
}
finally {
    # Cleanup must never throw: every step is isolated and native stderr is not an error here.
    $ErrorActionPreference = 'Continue'
    if ($writer) { try { $writer.Dispose() } catch { } }
    if ($pipe) { try { $pipe.Dispose() } catch { } }
    if ($proc) {
        try {
            if (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) {
                try { $null = & taskkill.exe /PID $proc.Id /T /F 2>&1 } catch { }
                if ($exitReason -eq 'error') { $exitReason = 'interrupted (Ctrl+C or abort)' }
                # taskkill can fail on a process that is already exiting; retry via .NET and wait.
                try { if (-not $proc.HasExited) { $proc.Kill() } } catch { }
                try { [void]$proc.WaitForExit(3000) } catch { }
            }
        } catch { Write-Host "playtest: cleanup of pid failed: $($_.Exception.Message)" }
        Start-Sleep -Milliseconds 300
    }
    try {
        if (Test-Path -LiteralPath $runSaveData) { $hashesAfter = Get-HashMap $runSaveData $dataNames }
        if (Test-Path -LiteralPath $runDir) {
            $summary = [ordered]@{
                tag = $Tag
                start = $startedAt.ToString('o')
                end = (Get-Date).ToString('o')
                exit_reason = $exitReason
                pristine_stamp = $pristineStamp
                coverage_run = [bool]$Coverage
                seed_shared = $seedShared2
                source = $sourceText
                save_hashes_before = $hashesBefore
                save_hashes_after = $hashesAfter
                shot_count = $shotCount
                no_control = [bool]$NoControl
                graphics = $graphics
            }
            $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
        }
    } catch { [Console]::Error.WriteLine("playtest: summary failed: $_") }
    if ($lockHeld) { try { Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue } catch { } }
    Write-Host "playtest done: $exitReason; run folder $runDir"
}
