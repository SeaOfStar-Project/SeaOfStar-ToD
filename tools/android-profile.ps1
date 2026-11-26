<#
.SYNOPSIS
  Samples the running game on the phone with simpleperf and prints where its threads spend their time.

.DESCRIPTION
  The game must already be running (start it yourself; this script never starts or stops it). Steps:
    1. finds the game's pid (adb shell pidof), copies the NDK's arm64 simpleperf into the app's files folder (run-as, debug APK only);
    2. records -p <pid> with frame-pointer call graphs for -Seconds seconds (simpleperf record --call-graph fp -f -Freq --duration);
    3. pulls perf.data to work\android-build\runs\profile-<tag>-<stamp>\ ;
    4. with the NDK's host simpleperf.exe and the unstripped libwiirecomp.so / libtitle.so of work\android-build (copied to a
       symfs folder, flat by name and by build id) it writes
         threads.txt   the per-thread summary (samples and share per thread id and thread name)
         top-<thread>.txt  the top -Top symbols by self time of each thread in -Threads (default game and gx-cmds), with the library name
       and prints both.
  Thread names come from the runtime itself (game, gx-cmds, audio, ui, shader and saver threads).

  -Version     only runs simpleperf --version on the phone (no game needed, starts nothing) and on this PC.
  -ReportOnly  skips the recording: -Data names an existing perf.data to symbolize again.
  -DryRun      prints every command and runs none.

.EXAMPLE
  tools\android-profile.ps1 -Version
  tools\android-profile.ps1 -Tag room -Seconds 20 -DryRun
  tools\android-profile.ps1 -Tag room -Seconds 20          # with the game already running, in the scene to measure
  tools\android-profile.ps1 -Tag again -ReportOnly -Data work\android-build\runs\profile-room-20261126-120000\perf.data
#>
[CmdletBinding()]
param(
    [string]$Tag = 'run',
    [int]$Seconds = 15,
    [int]$Freq = 4000,
    [int]$Top = 40,
    [string[]]$Threads = @('game', 'gx-cmds'),
    [string]$Serial,
    [string]$Package = 'io.github.seaofstarproject.tod',
    [string]$Libs,
    [string]$Data,
    [switch]$ReportOnly,
    [switch]$Version,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:MSYS_NO_PATHCONV = '1'
if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }   # Android Studio's default; set ANDROID_HOME to override
$perfDir = Join-Path $env:ANDROID_HOME 'ndk\27.2.12479018\simpleperf'
$hostPerf = Join-Path $perfDir 'bin\windows\x86_64\simpleperf.exe'
$devicePerf = Join-Path $perfDir 'bin\android\arm64\simpleperf'
$ndkBin = Join-Path $env:ANDROID_HOME 'ndk\27.2.12479018\toolchains\llvm\prebuilt\windows-x86_64\bin'
if (-not $Libs) { $Libs = Join-Path $root 'work\android-build' }   # the unstripped libraries (the APK carries the stripped copies)
$internal = "/data/user/0/$Package/files"
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $root "work\android-build\runs\profile-$Tag-$stamp"

$adbExe = $null
$adbCmd = Get-Command adb -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($adbCmd) { $adbExe = $adbCmd.Source }
elseif (Test-Path -LiteralPath (Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe')) { $adbExe = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe' }
if (-not $adbExe -and -not $DryRun) { throw 'adb.exe not found on PATH or in $env:ANDROID_HOME\platform-tools' }
if (-not $adbExe) { $adbExe = 'adb' }
$serialArgs = if ($Serial) { @('-s', $Serial) } else { @() }

$script:Exit = 0
function Run([string]$exe, [string[]]$arguments) {
    if ($DryRun) { Write-Host ('   ' + (Split-Path -Leaf $exe) + ' ' + (($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' ')); return '' }
    $saved = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try {
        $text = (& $exe @arguments 2>&1 | ForEach-Object { "$_" } | Out-String).Trim()
        $script:Exit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    return $text
}
function Adb([string[]]$arguments) { Run $adbExe ($serialArgs + $arguments) }
function RunAs([string]$inner) { Adb @('shell', "run-as $Package $inner") }
function Pull-AppFile([string]$remote, [string]$local) {
    $line = '"' + $adbExe + '" ' + ($serialArgs -join ' ') + " exec-out run-as $Package cat $remote > " + '"' + $local + '"'
    if ($DryRun) { Write-Host "   cmd /c $line"; return }
    & cmd.exe /c ('"' + $line + '"') 2>$null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $local) -or (Get-Item -LiteralPath $local).Length -eq 0) { throw "could not pull $remote" }
}

if ($Version) {
    Write-Host '== simpleperf on this PC (the NDK copy that reads perf.data)'
    if (-not $DryRun -and -not (Test-Path $hostPerf)) { throw "missing $hostPerf" }
    Write-Host (Run $hostPerf @('--version'))
    Write-Host '== simpleperf on the phone (system copy, as the shell user)'
    Write-Host (Adb @('shell', 'simpleperf --version'))
    Write-Host '== phone: Android release, ABI, security.perf_harden (0 lets the shell and debuggable apps profile)'
    Write-Host (Adb @('shell', 'getprop ro.build.version.release; getprop ro.product.cpu.abi; getprop security.perf_harden'))
    return
}

if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $runDir | Out-Null }
Write-Host "run folder: $runDir"
$perfData = Join-Path $runDir 'perf.data'

if ($ReportOnly) {
    if (-not $Data) { throw '-ReportOnly needs -Data <perf.data>' }
    $perfData = if (Test-Path -LiteralPath $Data) { (Resolve-Path -LiteralPath $Data).Path } else { Join-Path $root $Data }
    if (-not $DryRun -and -not (Test-Path -LiteralPath $perfData)) { throw "no file $perfData" }
} else {
    Write-Host '== find the game'
    $pidText = Adb @('shell', "pidof $Package")
    $gamePid = ($pidText -split '\s+' | Where-Object { $_ -match '^\d+$' } | Select-Object -First 1)
    if ($DryRun) { $gamePid = '<pid>' }
    elseif (-not $gamePid) { throw "$Package isn't running: start the game first (this script never starts it)" }
    Write-Host "pid $gamePid"
    Write-Host '== put simpleperf in the app folder'
    if (-not $DryRun -and -not (Test-Path $devicePerf)) { throw "missing $devicePerf" }
    [void](Adb @('push', $devicePerf, '/data/local/tmp/simpleperf'))
    [void](Adb @('shell', 'chmod 755 /data/local/tmp/simpleperf; setprop security.perf_harden 0'))   # the setprop works only on some builds; harmless otherwise
    [void](RunAs 'cp /data/local/tmp/simpleperf files/simpleperf')
    [void](RunAs 'chmod 755 files/simpleperf')
    Write-Host "== record $Seconds s at $Freq Hz, frame-pointer call graphs"
    $out = RunAs "files/simpleperf record -p $gamePid --call-graph fp -f $Freq --duration $Seconds -o $internal/perf.data"
    Write-Host $out
    if (-not $DryRun -and $out -match '(?i)error|failed|permission denied|not found') { Write-Warning 'simpleperf reported a problem (above); the pull below may fail' }
    Write-Host '== pull perf.data'
    Pull-AppFile "$internal/perf.data" $perfData
    [void](RunAs 'rm -f files/perf.data')
    if (-not $DryRun) { '{0}: {1:N0} bytes' -f $perfData, (Get-Item $perfData).Length | Write-Host }
}

# --- symbols: the unstripped libraries, by name and by build id
$symfs = Join-Path $runDir 'symfs'
Write-Host '== symbol files'
foreach ($name in 'libwiirecomp.so', 'libtitle.so') {
    $lib = Join-Path $Libs $name
    if ($DryRun) { Write-Host "   copy $lib -> $symfs (flat and .build_id)"; continue }
    if (-not (Test-Path -LiteralPath $lib)) { throw "missing $lib (tools\android-build.ps1 makes it; the stripped copy in stripped\ has no symbols for this)" }
    New-Item -ItemType Directory -Force -Path $symfs | Out-Null
    Copy-Item -LiteralPath $lib -Destination (Join-Path $symfs $name) -Force
    $notes = & (Join-Path $ndkBin 'llvm-readelf.exe') -n $lib 2>$null | Out-String
    if ($notes -match 'Build ID:\s*([0-9a-f]{8,})') {
        $id = $Matches[1]
        $dir = Join-Path $symfs ('.build_id\' + $id.Substring(0, 2)); New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Copy-Item -LiteralPath $lib -Destination (Join-Path $dir ($id.Substring(2) + '.debug')) -Force
        Write-Host "   $name build id $id"
    } else { Write-Warning "$name has no build id; symbols are matched by name only" }
}

function Report([string[]]$extra) {
    $args2 = @('report', '-i', $perfData, '--symfs', $symfs, '-n') + $extra
    Run $hostPerf $args2
}

Write-Host '== per-thread summary'
$summary = Report @('--sort', 'tid,comm')
if (-not $DryRun) { Set-Content -LiteralPath (Join-Path $runDir 'threads.txt') -Value $summary; $summary -split "\r?\n" | Select-Object -First 30 | Write-Host }
foreach ($thread in $Threads) {
    Write-Host "== top $Top symbols by self time, thread '$thread'"
    $text = Report @('--comms', $thread, '--sort', 'dso,symbol')
    if ($DryRun) { continue }
    Set-Content -LiteralPath (Join-Path $runDir "top-$thread.txt") -Value $text
    $lines = $text -split "\r?\n"
    $at = 0; for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^Overhead') { $at = $i; break } }
    $lines | Select-Object -Skip $at -First ($Top + 1) | Write-Host
    $shown = $lines | Select-Object -Skip ($at + 1) -First $Top
    $bare = @($shown | Where-Object { $_ -match '\s0x[0-9a-f]{6,}\s*$' }).Count
    if ($bare -gt ($Top / 2)) { Write-Warning "$bare of the top lines have no symbol name: simpleperf didn't match the library files (check the build ids above against the running APK's libraries)" }
}
Write-Host "files in $runDir"
