[CmdletBinding()]
param(
    # Build the native code only. The launcher's Rebuild packs by default so Play runs
    # the pre-baked image, with the disc, native code and shader recipes in one file.
    [switch]$NoPack,
    [int]$Jobs = 8,
    # Boot the packed image for a few seconds and report the shader recipes it carried,
    # which proves the image is pre-baked rather than compiling during play.
    [switch]$Verify,
    # The extracted disc to translate, when the launcher's Game files page points at one
    # other than config/game.json. It can be the extracted root or its sys folder.
    [string]$Disc
)

# Translate the user's own extracted disc with wii-recomp and compile the title library.
# This is the "Rebuild title" action behind the launcher; it does not start the game.

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
$config = Get-GameConfig
$paths = Get-GamePaths $config

$wiiRecomp = Get-RuntimeRoot
$addGame = Join-Path $wiiRecomp 'recomp/tools/add_game.py'
$msysBin = 'C:\msys64\clang64\bin'
$clang = Join-Path $msysBin 'clang.exe'
$library = Join-Path $wiiRecomp 'recomp/build-SO3EE9/title.dll'
$image = Join-Path $wiiRecomp 'games/SO3EE9.wrd'
$logPath = Join-Path $paths.Logs 'wii-recomp-build.log'

$discRoot = $paths.Extracted
if ($Disc) {
    $discRoot = Resolve-RepoPath $Disc
    if (Test-Path -LiteralPath (Join-Path $discRoot 'main.dol')) {
        $discRoot = Split-Path -Parent $discRoot
    }
}

if (-not (Test-Path -LiteralPath $addGame)) {
    throw "The runtime sources are missing ($wiiRecomp). See docs/04-tooling.md."
}
if (-not (Test-Path -LiteralPath $clang)) {
    throw "MSYS2 CLANG64 is missing ($clang). See docs/04-tooling.md."
}
if (-not (Test-Path -LiteralPath (Join-Path $discRoot 'sys/main.dol'))) {
    throw "Extracted disc not found in $discRoot. Run tools/extract-disc.ps1 or choose the folder on Game files."
}

New-Item -ItemType Directory -Force -Path $paths.Logs | Out-Null

Write-Section 'Translate and build'
$env:MSYS2_ROOT = 'C:\msys64'
$arguments = @($discRoot, '--jobs', $Jobs)
if ($NoPack) { $arguments += '--no-pack' }
Push-Location $wiiRecomp
try {
    & python $addGame @arguments 2>&1 | Tee-Object -FilePath $logPath
    if ($LASTEXITCODE -ne 0) { throw "add_game.py failed ($LASTEXITCODE); see $logPath" }
}
finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $library)) {
    throw "Build finished but $library was not produced. See $logPath."
}

Write-Section 'Result'
Write-Item 'title.dll' $library 'ok'
if (-not $NoPack) {
    if (-not (Test-Path -LiteralPath $image)) {
        throw "Build finished but $image was not produced. See $logPath."
    }
    $size = [math]::Round((Get-Item -LiteralPath $image).Length / 1GB, 2)
    Write-Item 'pre-baked image' "$image ($size GB)" 'ok'
}

# What is baked so far. The recipes are the title's own shader keys, collected while it
# drew; the profile lists the functions that ran and are compiled without counters.
$titleData = Join-Path $wiiRecomp 'recomp/build/title-data/00010000-534f3345'
$shaderPath = Join-Path $titleData 'shaders.bin'
$profilePath = Join-Path $titleData 'profile.txt'
if (Test-Path -LiteralPath $shaderPath) {
    $bytes = [IO.File]::ReadAllBytes($shaderPath)
    if ($bytes.Length -ge 32 -and [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -eq 'WRSR') {
        $version = [BitConverter]::ToUInt32($bytes, 4)
        $recordBytes = [BitConverter]::ToUInt32($bytes, 8)
        if ($recordBytes -gt 0) {
            $recipes = [math]::Floor(($bytes.Length - 32) / $recordBytes)
            Write-Item 'shader recipes' "$recipes (format $version)" 'ok'
        }
    }
}
if (Test-Path -LiteralPath $profilePath) {
    $functions = @(Get-Content -LiteralPath $profilePath | Where-Object { $_ -and -not $_.StartsWith('#') }).Count
    Write-Item 'profiled functions' "$functions" 'ok'
}
Write-Host 'Recipes grow as the game draws new scenes. Play further, then rebuild to add them.'

if ($Verify -and -not $NoPack) {
    # Boot the image as though only the console and the image existed: no loose library,
    # no loose recipes. The console then extracts the native code from the image and the
    # log says how many recipes came from the disc, which is what makes it self-contained.
    Write-Section 'Boot the packed image alone'
    $console = Join-Path $wiiRecomp 'recomp/build/wii-recomp.exe'
    $verifyRoot = Join-Path $paths.Logs 'prebake-verify'
    $verifyLog = Join-Path $paths.Logs 'prebake-verify.log'
    Remove-Item -LiteralPath $verifyRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $verifyRoot | Out-Null
    Remove-Item Env:WII_RECOMP_NATIVE -ErrorAction SilentlyContinue
    $env:WII_RECOMP_TITLE_DATA = Join-Path $verifyRoot 'title-data'
    $env:WII_RECOMP_CACHE = Join-Path $verifyRoot 'native-cache'
    $env:PATH = "$msysBin;$env:PATH"
    $env:WII_RECOMP_ACTIVITY = '1'
    $env:WII_RECOMP_WINDOW = '640x360'
    $env:WII_RECOMP_SCALE = '1'
    $env:WII_RECOMP_PRESENT = 'mailbox'
    $env:WII_RECOMP_FSR = '0'
    $process = Start-Process -FilePath $console -ArgumentList "`"$image`"" `
        -WorkingDirectory (Join-Path $wiiRecomp 'recomp/build') `
        -RedirectStandardError $verifyLog -RedirectStandardOutput (Join-Path $paths.Logs 'prebake-verify.out.log') `
        -PassThru
    Start-Sleep -Seconds 20
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    Start-Sleep -Milliseconds 500
    $recipeLine = Select-String -Path $verifyLog -Pattern 'shader recipes:' | Select-Object -First 1
    $nativeLine = Select-String -Path $verifyLog -Pattern 'native functions' | Select-Object -First 1
    $seconds = @(Select-String -Path $verifyLog -Pattern '^second \d+:')
    if ($nativeLine) { Write-Item 'native code in the image' $nativeLine.Line.Trim() 'ok' }
    if ($recipeLine) {
        $state = if ($recipeLine.Line -match '\((?!0 from the disc)\d+ from the disc\)') { 'ok' } else { 'warn' }
        Write-Item 'shader recipes in the image' $recipeLine.Line.Trim() $state
    }
    if ($seconds.Count -gt 0) {
        $last = $seconds[-1].Line.Trim()
        Write-Item 'last second' $last 'info'
    }
    Remove-Item -LiteralPath $verifyRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "Full log: $logPath"
