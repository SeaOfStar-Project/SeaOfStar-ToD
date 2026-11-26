<#
.SYNOPSIS
  Starts the game on the phone over adb (debug APK only), drives it with a timeline, and collects the evidence.

.DESCRIPTION
  The debug APK exports GameActivity, and every WII_RECOMP_* extra of the start becomes an environment variable
  of the game (GamePaths.environment). This script:
    1. force-stops the app, clears logcat, makes the app's shots folder;
    2. starts  am start -n <package>/.GameActivity  with the extras you give (-Scale, -GxDebug, -Validation, -Env);
    3. waits for the control FIFO (/data/user/0/<package>/files/control) to appear;
    4. plays the timeline (the same format as tools\playtest.ps1: "seconds verb args", '#' comments;
       press, hold, release, stick, pointer, shot ...), writing each command with
         adb shell "run-as <package> timeout 3 sh -c 'echo CMD > /data/user/0/<package>/files/control'"
       (one string: adb shell joins its arguments, and 'timeout' keeps a write from hanging forever if the game died);
    5. pulls every shot (run-as cat, through adb exec-out), converts .ppm to .png, pulls the run log, gaps.txt and
       logcat into work\android-build\runs\<tag>-<stamp>\;
    6. force-stops the app (always, also on error).

  Differences from the PC's playtest.ps1: the POSIX control FIFO does not answer, so there are no replies and no
  "-> NAME" variables (such a line is refused); a 'shot' is confirmed by its file appearing. A shot's path is
  always inside the app's files folder (the game can write nowhere else the shell can read back); a relative name
  goes to files/shots/ and a pulled copy lands in the run folder's shots\.

  Only the main session runs this (CLAUDE.md "Playtest flow"). Use -DryRun to print every adb command and run none.

.EXAMPLE
  tools\android-run.ps1 -Tag load12 -Script logs\load12.txt -Scale 3/2 -GxDebug nodual,nocull
  tools\android-run.ps1 -Tag validate -Validation -Seconds 90 -Every 15
  tools\android-run.ps1 -Tag dry -Script logs\load12.txt -DryRun
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Script,
    [int]$Seconds = 0,
    [double]$Every = 0,
    [string]$Scale,
    [string]$GxDebug,
    [switch]$Validation,
    [string[]]$Env = @(),
    [int]$BootTimeout = 180,
    [string]$Serial,
    [string]$Package = 'io.github.seaofstarproject.tod',
    [switch]$NoStop,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$inv = [Globalization.CultureInfo]::InvariantCulture
$root = Split-Path -Parent $PSScriptRoot
$env:MSYS_NO_PATHCONV = '1'   # Git Bash must not rewrite /data/... in adb arguments
if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }   # Android Studio's default; set ANDROID_HOME to override

function Fail($text) { throw $text }

$internal = "/data/user/0/$Package/files"
$control = "$internal/control"
$shotsRemote = "$internal/shots"
$external = "/sdcard/Android/data/$Package/files/SO3EE9"

# --- adb
$adbExe = $null
$adbCmd = Get-Command adb -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($adbCmd) { $adbExe = $adbCmd.Source }
elseif (Test-Path -LiteralPath (Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe')) { $adbExe = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe' }
if (-not $adbExe -and -not $DryRun) { Fail 'adb.exe not found on PATH or in $env:ANDROID_HOME\platform-tools' }
if (-not $adbExe) { $adbExe = 'adb' }
$serialArgs = if ($Serial) { @('-s', $Serial) } else { @() }

function Show-Adb([string[]]$arguments) { Write-Host ('   adb ' + (($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' ')) }
# Runs adb and returns its text (stderr merged). In a dry run, prints the command and returns ''.
# Never throws on adb's own stderr or exit code (under $ErrorActionPreference = 'Stop' a native stderr line merged by 2>&1
# becomes a terminating error, and 'dumpsys | grep' says "Broken pipe" on stderr): the exit code is left in $script:AdbExit
# for the caller that cares (am start); polls such as 'ls' and 'grep' are meant to fail quietly.
$script:AdbExit = 0
function Invoke-Native([string]$exe, [string[]]$arguments) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $text = (& $exe @arguments 2>&1 | ForEach-Object { if ($_ -is [Management.Automation.ErrorRecord]) { $_.ToString() } else { "$_" } } | Out-String).Trim()
        $script:AdbExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    return $text
}
function Adb([string[]]$arguments) {
    $all = $serialArgs + $arguments
    if ($DryRun) { Show-Adb $all; return '' }
    Invoke-Native $adbExe $all
}
# One remote command, as a single string (adb shell joins its arguments with spaces).
function Remote([string]$command) { Adb @('shell', $command) }
function RunAsShell([string]$command) { Remote $command }
# The app's own files, as the app's user. 'inner' must not contain double quotes.
function RunAs([string]$inner) { Remote "run-as $Package $inner" }

# A file of the app's, copied to this PC byte for byte. PowerShell's > would re-encode, so cmd does the redirect.
function Pull-AppFile([string]$remotePath, [string]$localPath) {
    $q = '"'
    $line = $q + $adbExe + $q + ' ' + (($serialArgs | ForEach-Object { $_ }) -join ' ') + ' exec-out run-as ' + $Package + ' cat ' + $remotePath + ' > ' + $q + $localPath + $q
    if ($DryRun) { Write-Host "   cmd /c $line"; return $true }
    & cmd.exe /c ('"' + $line + '"') 2>$null
    return ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $localPath) -and (Get-Item -LiteralPath $localPath).Length -gt 0)
}

# --- extras
$extras = [ordered]@{}
if ($Scale) {
    if ($Scale -notmatch '^\d+(/\d+)?$') { Fail "bad -Scale '$Scale' (use e.g. 2 or 3/2)" }
    $extras['WII_RECOMP_SCALE'] = $Scale
}
if ($GxDebug) { $extras['WII_RECOMP_GX_DEBUG'] = $GxDebug }
if ($Validation) { $extras['WII_RECOMP_VULKAN_VALIDATION'] = '1' }
foreach ($pair in $Env) {
    if ($pair -notmatch '^(WII_RECOMP_[A-Z0-9_]+)=(.*)$') { Fail "bad -Env '$pair' (use WII_RECOMP_NAME=VALUE)" }
    $extras[$Matches[1]] = $Matches[2]
}
foreach ($k in $extras.Keys) {
    if ($extras[$k] -match "['""\\\`$;&|<>\r\n]") { Fail "$k's value has a character that would need shell quoting: $($extras[$k])" }
}

# --- timeline (same lines as playtest.ps1)
$events = @()
$lastEvent = 0.0
$scriptPath = $null
if ($Script) {
    $scriptPath = if (Test-Path -LiteralPath $Script) { (Resolve-Path -LiteralPath $Script).Path } else { Join-Path $root $Script }
    if (-not (Test-Path -LiteralPath $scriptPath)) { Fail "no timeline at $scriptPath" }
    foreach ($line in Get-Content -LiteralPath $scriptPath) {
        $trim = $line.Trim()
        if (-not $trim -or $trim.StartsWith('#')) { continue }
        if ($trim -match '->\s*[A-Za-z_]\w*$' -or $trim -match '\$[A-Za-z_]') { Fail "timeline variables need replies, which the phone's control FIFO does not give: $line" }
        $parts = $trim -split '\s+', 3
        if ($parts.Count -lt 2) { Fail "bad timeline line: $line" }
        $at = 0.0
        if (-not [double]::TryParse($parts[0], [Globalization.NumberStyles]::Float, $inv, [ref]$at)) { Fail "bad time in timeline line: $line" }
        $verb = $parts[1]
        if ($verb -eq 'gshot') { $verb = 'shot' }
        $arg = if ($parts.Count -ge 3) { $parts[2] } else { '' }
        if ("$verb $arg" -match "['""\\\`$;&|<>]") { Fail "timeline line has a character that would need shell quoting: $line" }
        $events += [pscustomobject]@{ At = $at; Verb = $verb; Arg = $arg }
        if ($at -gt $lastEvent) { $lastEvent = $at }
    }
    $events = @($events | Sort-Object At)
}
if ($Seconds -le 0) { $Seconds = if ($scriptPath) { [int][Math]::Ceiling($lastEvent) + 3 } else { 60 } }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $root "work\android-build\runs\$Tag-$stamp"
$shotsDir = Join-Path $runDir 'shots'
Write-Host "run folder : $runDir"
Write-Host "extras     : $(if ($extras.Count) { ($extras.GetEnumerator() | ForEach-Object { $_.Key + '=' + $_.Value }) -join ' ' } else { '(none)' })"
Write-Host "timeline   : $(if ($scriptPath) { "$scriptPath ($($events.Count) lines, $Seconds s)" } else { "none ($Seconds s)" }); auto shot every $(if ($Every -gt 0) { "$Every s" } else { 'off' })"
if ($DryRun) { Write-Host '   [dry-run] commands are printed, none is run' }

# --- device check
if (-not $DryRun) {
    $listed = (Invoke-Native $adbExe @('devices')) -split "`n" | Select-Object -Skip 1 | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '\sdevice\s*$' }
    if (@($listed).Count -eq 0) { Fail 'no adb device in state "device"' }
    if (@($listed).Count -gt 1 -and -not $Serial) { Fail 'more than one device: pass -Serial' }
    New-Item -ItemType Directory -Force -Path $runDir, $shotsDir | Out-Null
}

$shots = New-Object System.Collections.Generic.List[string]
function Remote-ShotPath([string]$name) {
    if ($name.StartsWith('/')) { return $name }
    return "$shotsRemote/$name"
}

function Send-Control([string]$text) {
    # No double quotes inside: the whole remote command is one double-quoted argument for adb.
    RunAs "timeout 3 sh -c 'echo $text > $control'" | Out-Null
}

function Convert-PpmToPng([string]$ppm, [string]$png) {
    Add-Type -AssemblyName System.Drawing
    $bytes = [IO.File]::ReadAllBytes($ppm)
    $st = @{ pos = 0 }   # a hashtable: a nested function's assignments to a plain variable would not stick
    function Next-Token($bytes, $st) {
        while ($st.pos -lt $bytes.Length -and ($bytes[$st.pos] -le 32 -or $bytes[$st.pos] -eq 35)) {
            if ($bytes[$st.pos] -eq 35) { while ($st.pos -lt $bytes.Length -and $bytes[$st.pos] -ne 10) { $st.pos++ } } else { $st.pos++ }
        }
        $start = $st.pos
        while ($st.pos -lt $bytes.Length -and $bytes[$st.pos] -gt 32) { $st.pos++ }
        return [Text.Encoding]::ASCII.GetString($bytes, $start, $st.pos - $start)
    }
    if ((Next-Token $bytes $st) -ne 'P6') { throw "$ppm is not a P6 picture" }
    $w = [int](Next-Token $bytes $st); $h = [int](Next-Token $bytes $st); [void](Next-Token $bytes $st)
    $pos = $st.pos + 1   # the single white space after maxval
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
    $row = New-Object byte[] ($w * 3)
    for ($y = 0; $y -lt $h; $y++) {
        $src = $pos + $y * $w * 3
        for ($x = 0; $x -lt $w; $x++) { $row[$x * 3] = $bytes[$src + $x * 3 + 2]; $row[$x * 3 + 1] = $bytes[$src + $x * 3 + 1]; $row[$x * 3 + 2] = $bytes[$src + $x * 3] }
        [Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $row.Length)
    }
    $bmp.UnlockBits($data)
    $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

$started = $false
try {
    Write-Host '== start'
    Adb @('shell', "am force-stop $Package") | Out-Null
    Adb @('logcat', '-c') | Out-Null
    RunAs "mkdir -p $shotsRemote" | Out-Null
    RunAs "rm -f $control" | Out-Null
    $am = "am start -W -n $Package/.GameActivity"
    foreach ($k in $extras.Keys) { $am += " --es $k '" + $extras[$k] + "'" }
    # SDL_CreateWindow waits for a resumed Activity, so the screen must be on and unlocked before the start.
    # Only advice: a failing check (no grep, a broken pipe) must not stop the run, so it is tried and its errors ignored.
    try {
        $awake = ((RunAsShell 'dumpsys power | grep mWakefulness=') -split "`r?`n" | Select-Object -First 1)
        Write-Host "   screen: $awake"
        if ($awake -and $awake -notmatch 'Awake') { Remote 'input keyevent KEYCODE_WAKEUP' | Out-Null; Start-Sleep -Milliseconds 800 }
        Remote 'wm dismiss-keyguard' | Out-Null
    } catch { Write-Host "[warn] the screen check failed ($_); the start goes on" }
    Write-Host '   (the only place the game is started)'
    $amText = Remote $am
    if (-not $DryRun -and $script:AdbExit -ne 0) { Write-Host "[warn] am start exited $($script:AdbExit): $amText" }
    $started = $true

    Write-Host '== wait for the control FIFO'
    $t0 = Get-Date
    $up = $false
    while (((Get-Date) - $t0).TotalSeconds -lt $BootTimeout) {
        if ($DryRun) { Write-Host "   [dry-run] poll: run-as $Package ls $control until it exists (up to $BootTimeout s)"; $up = $true; break }
        $seen = RunAs "ls $control"
        if ($seen -eq $control) { $up = $true; break }
        Start-Sleep -Milliseconds 1000
    }
    if (-not $up) { Write-Host "[warn] the control FIFO did not appear in $BootTimeout s; no commands will be sent (run.log's last 'window:' line shows how far the window got; a 'SDL_CreateWindow has waited' line means the app was not in front with the screen on)" }
    $timelineStart = Get-Date
    Write-Host '== timeline'
    $autoNext = if ($Every -gt 0) { $Every } else { [double]::MaxValue }
    $autoCount = 0
    $i = 0
    while ($true) {
        $elapsed = if ($DryRun) { $(if ($i -lt $events.Count) { $events[$i].At } else { $Seconds }) } else { ((Get-Date) - $timelineStart).TotalSeconds }
        if ($elapsed -ge $Seconds -and $i -ge $events.Count) { break }
        if ($i -lt $events.Count -and $elapsed -ge $events[$i].At) {
            $e = $events[$i]; $i++
            if ($up) {
                if ($e.Verb -eq 'shot') {
                    $remotePath = Remote-ShotPath $e.Arg
                    $shots.Add($remotePath)
                    Send-Control "shot $remotePath"
                } else {
                    Send-Control (("$($e.Verb) $($e.Arg)").Trim())
                }
            }
            Write-Host ('  > {0,7:N2}s  {1} {2}' -f $elapsed, $e.Verb, $e.Arg)
            continue
        }
        if ($up -and $elapsed -ge $autoNext -and $elapsed -lt $Seconds) {
            $autoCount++
            $name = 'auto{0:D3}.ppm' -f $autoCount
            $shots.Add((Remote-ShotPath $name))
            Send-Control "shot $(Remote-ShotPath $name)"
            Write-Host ('  > {0,7:N2}s  auto shot {1}' -f $elapsed, $name)
            $autoNext += $Every
            continue
        }
        if ($DryRun) { if ($i -ge $events.Count) { break } else { continue } }
        Start-Sleep -Milliseconds 50
    }

    Write-Host '== collect'
    if ($shots.Count) {
        # A shot is done when its file stops growing. Give the last ones a few seconds.
        foreach ($remotePath in $shots) {
            $name = Split-Path -Leaf $remotePath
            $local = Join-Path $shotsDir $name
            $size = ''
            for ($try = 0; $try -lt 20 -and -not $DryRun; $try++) {
                $now = RunAs "ls -l $remotePath"
                if ($now -and $now -eq $size) { break }
                $size = $now
                Start-Sleep -Milliseconds 400
            }
            if (Pull-AppFile $remotePath $local) {
                if ($DryRun) { Write-Host "   [dry-run] convert $local -> .png" }
                elseif ($name -match '\.ppm$') {
                    try { Convert-PpmToPng $local ([IO.Path]::ChangeExtension($local, '.png')); Write-Host "   $name -> png" } catch { Write-Host "[warn] $name not converted: $_" }
                } else { Write-Host "   $name" }
            } else { Write-Host "[warn] no shot came back for $remotePath" }
        }
    }
    Adb @('pull', "$external/logs/run.log", (Join-Path $runDir 'run.log')) | Out-Null
    [void](Pull-AppFile "$internal/gaps.txt" (Join-Path $runDir 'gaps.txt'))
    if ($DryRun) { Show-Adb ($serialArgs + @('logcat', '-d', '-v', 'threadtime')); Write-Host "   > $(Join-Path $runDir 'logcat.txt')" }
    else { (Invoke-Native $adbExe ($serialArgs + @('logcat', '-d', '-v', 'threadtime'))) | Set-Content -LiteralPath (Join-Path $runDir 'logcat.txt') -Encoding UTF8 }
}
finally {
    if ($started -and -not $NoStop) {
        Write-Host '== stop'
        Adb @('shell', "am force-stop $Package") | Out-Null
    }
}
Write-Host "done: $runDir"
