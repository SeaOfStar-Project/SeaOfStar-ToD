<#
Puts the Android build on a phone over adb: the debug APK (built with this PC's native libraries), the
packed image with the android-arm64 title library, and a first seed of saves. Nothing here starts the game.

  tools\android-push.ps1 -DryRun            print every step, touch nothing (no device needed)
  tools\android-push.ps1                    build the APK, build the image if missing, install, push
  tools\android-push.ps1 -ImageOnly         only (re)build work\android-build\SO3EE9.wrd, no device needed
  tools\android-push.ps1 -SkipBuild -SkipImage   install the APK that is there, push saves only if none are on the phone
  tools\android-push.ps1 -ResetSaves        overwrite the phone's saves with the seed (the game rewrites them on every boot)

The image: the Windows image (work\wii-recomp\games\SO3EE9.wrd) is copied to work\android-build\SO3EE9.wrd and its
native code is replaced with android-arm64=work\android-build\stripped\libtitle.so (make_disc.replace_native, which keeps the
disc and the shader recipes). The same thing from an extracted disc, without the Windows image:
  python work\wii-recomp\recomp\tools\make_disc.py extracted\SO3EE9 work\android-build\SO3EE9.wrd --native android-arm64=work\android-build\stripped\libtitle.so
Everything pushed is game-derived and stays local (ADR-008). Needs the native libraries from tools\android-build.ps1.

Git Bash (MSYS) rewrites a leading-slash argument such as /sdcard/... into a Windows path before adb sees it. This script
sets MSYS_NO_PATHCONV=1 for its own adb calls; when typing adb by hand from Git Bash, set it too (or use PowerShell).

Phone layout (app-specific external storage, no permission needed):
  /sdcard/Android/data/<package>/files/SO3EE9/{SO3EE9.wrd, saves/, title-data/, logs/run.log}
Launch with options (env variables go in as WII_RECOMP_ extras):
  adb shell am start -n <package>/.GameActivity --es WII_RECOMP_SCALE 1.5
#>
[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$ImageOnly,
    [switch]$SkipBuild,
    [switch]$SkipBranding,
    [switch]$ForceBranding,
    [switch]$SkipImage,
    [switch]$SkipInstall,
    [switch]$SkipSaves,
    [switch]$ResetSaves,
    [switch]$ForceImage,
    [string]$Serial,
    [string]$SavesFrom,
    [string]$JavaHome,
    [string]$Package = 'io.github.seaofstarproject.tod'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'work\android-build'
$wii = Join-Path $root 'work\wii-recomp\recomp'
$sourceImage = Join-Path $root 'work\wii-recomp\games\SO3EE9.wrd'
$image = Join-Path $out 'SO3EE9.wrd'
$titleLib = Join-Path $out 'stripped\libtitle.so'
$depsRoot = if ($env:SEAOFSTAR_ANDROID_DEPS) { $env:SEAOFSTAR_ANDROID_DEPS } else { Join-Path $root 'work\android-deps' }   # as in android-build.ps1
$sdlLibs = Join-Path $depsRoot 'sdl3-install-arm64\lib'
$apk = Join-Path $root 'android\app\build\outputs\apk\debug\app-debug.apk'
$remote = "/sdcard/Android/data/$Package/files/SO3EE9"
$env:MSYS_NO_PATHCONV = '1'   # Git Bash must not rewrite /sdcard/... in adb arguments
if ($JavaHome) { $env:JAVA_HOME = $JavaHome }   # Gradle needs a JDK it supports (17 to 21); otherwise JAVA_HOME or PATH is used as it is
if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }   # Android Studio's default; set ANDROID_HOME to override

function Step($text) { Write-Host "== $text" }
function Run($what, [scriptblock]$block) {
    if ($DryRun) { Write-Host "   [dry-run] $what"; return }
    Write-Host "   $what"
    & $block
    if ($LASTEXITCODE -ne 0) { throw "$what failed ($LASTEXITCODE)" }
}
# Resolve the real adb.exe once: a function named Adb would shadow `adb` (PowerShell names are case-insensitive).
$adbExe = $null
$adbCmd = Get-Command adb -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($adbCmd) { $adbExe = $adbCmd.Source }
elseif (Test-Path -LiteralPath (Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe')) { $adbExe = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe' }
if (-not $adbExe) { throw 'adb.exe not found on PATH or in $env:ANDROID_HOME\platform-tools' }

function Adb([string[]]$arguments) {
    if ($Serial) { $arguments = @('-s', $Serial) + $arguments }
    & $adbExe @arguments
}
function AdbText([string[]]$arguments) {
    if ($Serial) { $arguments = @('-s', $Serial) + $arguments }
    (& $adbExe @arguments 2>&1 | Out-String).Trim()
}

# 1. The image with the android-arm64 title library.
if (-not $SkipImage -or $ImageOnly) {
    Step 'image'
    if (-not (Test-Path -LiteralPath $titleLib)) { throw "missing ${titleLib}: run tools\android-build.ps1 first" }
    if ((Test-Path -LiteralPath $image) -and -not $ForceImage -and ((Get-Item $image).LastWriteTime -gt (Get-Item $titleLib).LastWriteTime)) {
        Write-Host "   $image is newer than libtitle.so, kept (-ForceImage rebuilds it)"
    } else {
        if (-not (Test-Path -LiteralPath $sourceImage)) { throw "missing ${sourceImage}: build the Windows image first, or use the make_disc.py command in this script's header" }
        Run "copy $sourceImage -> $image" { New-Item -ItemType Directory -Force -Path $out | Out-Null; Copy-Item -LiteralPath $sourceImage -Destination $image -Force; $global:LASTEXITCODE = 0 }
        $py = "import sys; sys.path.insert(0, r'$wii\tools'); import make_disc; sys.exit(0 if make_disc.replace_native(r'$image', [('android-arm64', r'$titleLib')]) else 3)"
        Run "make_disc.replace_native($image, android-arm64=$titleLib)" { python -c $py }
    }
    if ($ImageOnly) { return }
}

# 2. Android branding: the app name and icon from the user's own disc (opening.bnr), local only (work\ is ignored).
#    Skipped silently when there is no dump; the APK then keeps the neutral icon and the label SeaOfStar-ToD.
$brandingDir = Join-Path $out 'branding'
$brandingStamp = Join-Path $brandingDir 'branding.json'
$bnr = Join-Path $root 'extracted\SO3EE9\files\opening.bnr'
$useBranding = $false
if (-not $SkipBuild -and -not $SkipBranding) {
    if (Test-Path -LiteralPath $bnr) {
        Step 'branding (icon and name from opening.bnr)'
        $stale = $ForceBranding -or -not (Test-Path -LiteralPath $brandingStamp) -or ((Get-Item $brandingStamp).LastWriteTime -lt (Get-Item $bnr).LastWriteTime)
        if ($stale) { Run 'python tools\android-branding.py' { python (Join-Path $PSScriptRoot 'android-branding.py') } }
        else { Write-Host '   branding is up to date' }
    } else { Write-Host "   no ${bnr}: neutral icon and name (SeaOfStar-ToD)" }
}
if (-not $SkipBranding -and (Test-Path -LiteralPath (Join-Path $brandingDir 'res'))) { $useBranding = $true }

# 3. The APK, with this PC's libraries inside.
if (-not $SkipBuild) {
    Step 'APK (gradle assembleDebug)'
    $gradleArgs = @('assembleDebug', "-PnativeLibsDir=$out", "-PsdlLibDir=$sdlLibs", '--console=plain')
    if ($useBranding) { $gradleArgs += "-PbrandingDir=$brandingDir" }
    $vvl = $env:VVL_LAYER   # optional: path to libVkLayer_khronos_validation.so (arm64-v8a) from the Vulkan SDK's Android binaries
    if ($vvl -and (Test-Path -LiteralPath $vvl)) { $gradleArgs += "-PvalidationLayer=$vvl" }   # debug APKs only
    Run 'gradlew assembleDebug -PnativeLibsDir -PsdlLibDir' {
        Push-Location (Join-Path $root 'android')
        try { & .\gradlew.bat @gradleArgs } finally { Pop-Location }
    }
}
if (-not $DryRun -and -not (Test-Path -LiteralPath $apk)) { throw "missing ${apk}" }

# 4. The phone.
Step 'device'
if ($DryRun) { Write-Host '   [dry-run] adb devices (must list one device in state "device")' }
else {
    # adb's lines end in `r`n on Windows, so trim each one before looking for the state.
    $devices = @((AdbText @('devices')) -split "`n" | Select-Object -Skip 1 | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '\sdevice\s*$' })
    if ($devices.Count -eq 0) { throw 'no adb device in state "device": connect the phone and allow USB debugging' }
    if ($devices.Count -gt 1 -and -not $Serial) { throw 'more than one device: pass -Serial' }
}
if (-not $SkipInstall) {
    Step 'install'
    Run "adb install -r $apk" { Adb @('install', '-r', $apk) }
    # The app's folder under Android/data may only exist once the app has asked for it.
    Run "adb shell am start -n $Package/.LauncherActivity (creates the app's folders)" { Adb @('shell', 'am', 'start', '-n', "$Package/.LauncherActivity") | Out-Null; Start-Sleep -Seconds 2 }
}
Run "adb shell mkdir -p $remote/saves $remote/title-data $remote/logs" { Adb @('shell', 'mkdir', '-p', "$remote/saves", "$remote/title-data", "$remote/logs") }

# 5. The image (2.4 GB, so only when the size on the phone differs).
if (-not $SkipImage) {
    Step 'push image'
    $size = if (Test-Path -LiteralPath $image) { (Get-Item $image).Length } else { 'unknown (dry run)' }
    $onPhone = if ($DryRun) { 'not asked (dry run)' } else { AdbText @('shell', 'stat', '-c', '%s', "$remote/SO3EE9.wrd") }
    if (-not $DryRun -and $onPhone -eq "$size") { Write-Host "   already on the phone ($size bytes)" }
    else { Run "adb push $image $remote/SO3EE9.wrd  ($size bytes; on the phone: $onPhone)" { Adb @('push', $image, "$remote/SO3EE9.wrd") } }
}

# 6. A first set of saves. Never over existing ones unless -ResetSaves: the game rewrites them on every boot.
if (-not $SkipSaves) {
    Step 'saves seed'
    if (-not $SavesFrom) {
        $pristine = Join-Path $wii 'build\saves\pristine'
        $newest = if (Test-Path -LiteralPath $pristine) { Get-ChildItem -LiteralPath $pristine -Directory | Sort-Object LastWriteTime | Select-Object -Last 1 }
        if ($newest) { $SavesFrom = $newest.FullName }
    }
    if (-not $SavesFrom -or -not (Test-Path -LiteralPath $SavesFrom)) { Write-Host '   no pristine save copy found (-SavesFrom <folder with RFF2_00..15>); skipped' }
    else {
        $dest = "$remote/saves/title/00010000/534f3345/data"
        $have = if ($DryRun) { 'not asked (dry run)' } else { AdbText @('shell', "ls $dest/RFF2_00 2>/dev/null") }
        if (-not $DryRun -and $have -like '*RFF2_00*' -and -not $ResetSaves) { Write-Host '   the phone already has saves, kept (-ResetSaves overwrites)' }
        else {
            Run "adb shell mkdir -p $dest" { Adb @('shell', 'mkdir', '-p', $dest) }
            Run "adb push $SavesFrom\. $dest" { Adb @('push', "$SavesFrom\.", $dest) }
        }
    }
}

Step 'done'
Write-Host "   launch: adb shell am start -n $Package/.GameActivity     (or Play in the app)"
Write-Host "   log:    adb pull $remote/logs/run.log"
Write-Host "   (not run by this script: the game is only started by hand or by the main session's playtest)"
