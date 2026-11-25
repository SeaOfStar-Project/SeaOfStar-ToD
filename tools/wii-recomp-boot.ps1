[CmdletBinding()]
param(
    [int]$Seconds = 60,
    [switch]$Rebuild,
    [switch]$Shot
)

# Boot the user's own disc through wii-recomp, the most complete existing Wii runtime.
# This is a bring-up baseline, not part of the final runtime. Nothing is committed:
# the translated code and the console build stay under work/.

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
$config = Get-GameConfig
$paths = Get-GamePaths $config

$wiiRecomp = Get-RuntimeRoot
$addGame = Join-Path $wiiRecomp 'recomp/tools/add_game.py'
$msysBin = 'C:\msys64\clang64\bin'
$clang = Join-Path $msysBin 'clang.exe'
$console = Join-Path $wiiRecomp 'recomp/build/wii-recomp.exe'
$library = Join-Path $wiiRecomp 'recomp/build-SO3EE9/title.dll'
$logPath = Join-Path $paths.Logs 'wii-recomp-boot.log'
$shotPath = Join-Path $root 'analysis/wii-recomp-shot.ppm'

if (-not (Test-Path -LiteralPath $addGame)) {
    throw "The runtime sources are missing ($wiiRecomp). See docs/04-tooling.md."
}
if (-not (Test-Path -LiteralPath $clang)) {
    throw "MSYS2 CLANG64 is missing ($clang). See docs/04-tooling.md."
}
if (-not (Test-Path -LiteralPath $paths.MainDol)) {
    throw "Extracted disc not found. Run tools/extract-disc.ps1."
}

# The console wants the sys folder itself (boot.bin, fst.bin, main.dol); it finds files/
# one level up. Passing the disc root fails with "isn't an extracted disc".
$drive = Split-Path -Parent $paths.MainDol

New-Item -ItemType Directory -Force -Path $paths.Logs, (Split-Path -Parent $shotPath) | Out-Null

Write-Section 'Translate and build'
if ($Rebuild -or -not (Test-Path -LiteralPath $library)) {
    $env:MSYS2_ROOT = 'C:\msys64'
    Push-Location $wiiRecomp
    try {
        & python $addGame $paths.Extracted --no-pack 2>&1 | Tee-Object -FilePath $logPath
        if ($LASTEXITCODE -ne 0) { throw "add_game.py failed ($LASTEXITCODE); see $logPath" }
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Item 'title.dll' 'already built (use -Rebuild to translate again)' 'ok'
}

if (-not (Test-Path -LiteralPath $console)) {
    throw "wii-recomp.exe was not built. See $logPath."
}

function Send-ControlCommand {
    param([Parameter(Mandatory = $true)][string]$Command)

    try {
        $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'wii', [System.IO.Pipes.PipeDirection]::Out)
        $pipe.Connect(3000)
        $writer = New-Object System.IO.StreamWriter($pipe)
        $writer.WriteLine($Command)
        $writer.Flush()
        $writer.Dispose()
        $pipe.Dispose()
        return $true
    }
    catch {
        return $false
    }
}

Write-Section "Boot for $Seconds seconds"
$env:PATH = "$msysBin;$env:PATH"
$env:WII_RECOMP_NATIVE = $library
$env:WII_RECOMP_ACTIVITY = '1'
$env:WII_RECOMP_FPS_LOG = '1'
$env:WII_RECOMP_DISC_LOG = '1'
if ($Shot) {
    $env:WII_RECOMP_CONTROL = 'wii'
    Remove-Item -LiteralPath $shotPath -Force -ErrorAction SilentlyContinue
}
else {
    Remove-Item Env:\WII_RECOMP_CONTROL -ErrorAction SilentlyContinue
}

$bootLog = Join-Path $paths.Logs 'wii-recomp-run.log'
$errLog = Join-Path $paths.Logs 'wii-recomp-run.err.log'
$process = Start-Process -FilePath $console `
    -ArgumentList @($drive) `
    -WorkingDirectory (Join-Path $wiiRecomp 'recomp/build') `
    -RedirectStandardOutput $bootLog `
    -RedirectStandardError $errLog `
    -PassThru

if ($Shot) {
    $shotAt = [math]::Max(5, [int]($Seconds * 0.5))
    Start-Sleep -Seconds $shotAt
    if ($process.HasExited) {
        Write-Item 'screenshot' "console exited before ${shotAt}s" 'warn'
    }
    else {
        $ok = Send-ControlCommand "shot $shotPath"
        Write-Item 'screenshot' $(if ($ok) { "$shotPath (requested at ${shotAt}s)" } else { 'control pipe unavailable' }) $(if ($ok) { 'ok' } else { 'warn' })
    }
    $deadline = (Get-Date).AddSeconds([math]::Max(1, $Seconds - $shotAt))
}
else {
    $deadline = (Get-Date).AddSeconds($Seconds)
}

while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 1
    $process.Refresh()
}
if (-not $process.HasExited) {
    $process.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 3
    if (-not $process.HasExited) { $process.Kill() }
}
$process.WaitForExit()

Write-Section 'Result'
$exitCode = if ($null -ne $process.ExitCode) { "$($process.ExitCode)" } else { 'unknown' }
Write-Item 'exit code' $exitCode 'info'
if (Test-Path -LiteralPath $shotPath) {
    Write-Item 'screenshot' "$shotPath ($((Get-Item $shotPath).Length) bytes)" 'ok'
}
Write-Host ''
Get-Content -LiteralPath $errLog -Tail 25 -ErrorAction SilentlyContinue
Write-Host ''
Write-Host "Full log: $errLog"
