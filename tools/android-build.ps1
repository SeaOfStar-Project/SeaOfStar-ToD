<#
Builds the runtime (libwiirecomp.so) and the translated title (libtitle.so) for Android arm64-v8a
with the NDK, from the dev clone of the runtime. Nothing here starts the game.

  tools\android-build.ps1                 build what is out of date
  tools\android-build.ps1 -Clean          start over (removes work\android-build)
  tools\android-build.ps1 -SkipDeps       don't (re)build SDL3 and shaderc when they are missing

Needs: the Android SDK with NDK r27.2 (ANDROID_HOME), its cmake 3.22.1 and ninja, Python 3, and the
translated sources in work\wii-recomp\recomp\build-SO3EE9\generated (made by tools\wii-recomp-build.ps1).
Third-party builds go outside git, in $DepsRoot (default work\android-deps, or $env:SEAOFSTAR_ANDROID_DEPS; SDL3 release tarball, shaderc from the NDK's sources).

API level 30 (Android 11): it is the first level whose libc has memfd_create, which the command ring
(gx_fifo.c) maps twice. The Galaxy M33 5G ships with Android 12 and is updated to 14.
CPU: -march=armv8.2-a -mtune=cortex-a78. Both core types of the Exynos 1280 (A78, A55) implement
armv8.2-a, so one build runs on all of them; the A78 only picks the instruction scheduling.
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$SkipDeps,
    [int]$Jobs = [Environment]::ProcessorCount,
    [string]$Target = 'libwiirecomp.so,libtitle.so'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$AndroidHome = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }   # Android Studio's default
$Ndk = Join-Path $AndroidHome 'ndk\27.2.12479018'
$Cmake = Join-Path $AndroidHome 'cmake\3.22.1\bin'
$DepsRoot = if ($env:SEAOFSTAR_ANDROID_DEPS) { $env:SEAOFSTAR_ANDROID_DEPS } else { Join-Path $root 'work\android-deps' }   # third-party builds, outside git (work\ is ignored)
$Sdl = Join-Path $DepsRoot 'sdl3-src\SDL3-3.4.20'
$SdlBuild = Join-Path $DepsRoot 'sdl3-build-arm64'
$SdlInstall = Join-Path $DepsRoot 'sdl3-install-arm64'
$ShadercBuild = Join-Path $DepsRoot 'shaderc-build'
$Wii = Join-Path $root 'work\wii-recomp\recomp'
$Generated = Join-Path $Wii 'build-SO3EE9\generated'
$Out = Join-Path $root 'work\android-build'
$NdkBin = Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin'

foreach ($path in @($Ndk, $Cmake, $Generated)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "missing: $path" }
}
$env:ANDROID_NDK_HOME = $Ndk
$env:SDL3_ANDROID_PREFIX = $SdlInstall
$env:SHADERC_ANDROID_LIBS = Join-Path $ShadercBuild 'obj\local\arm64-v8a'
$env:PATH = "$Cmake;$env:PATH"
Remove-Item Env:VULKAN_SDK -ErrorAction SilentlyContinue   # the NDK has its own Vulkan headers

function Invoke-Step($what, [scriptblock]$block) {
    Write-Host "== $what"
    & $block
    if ($LASTEXITCODE -ne 0) { throw "$what failed ($LASTEXITCODE)" }
}

if (-not $SkipDeps) {
    # SDL3: the latest stable release tarball from github.com/libsdl-org/SDL (3.4.20 here), shared, arm64-v8a.
    if (-not (Test-Path (Join-Path $SdlInstall 'lib\libSDL3.so'))) {
        if (-not (Test-Path $Sdl)) {
            New-Item -ItemType Directory -Force -Path (Join-Path $DepsRoot 'dl'), (Join-Path $DepsRoot 'sdl3-src') | Out-Null
            $tar = Join-Path $DepsRoot 'dl\SDL3-3.4.20.tar.gz'
            Invoke-Step 'download SDL3 3.4.20' { curl.exe -sL -o $tar https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz }
            Invoke-Step 'unpack SDL3' { tar -xzf $tar -C (Join-Path $DepsRoot 'sdl3-src') }
        }
        Invoke-Step 'configure SDL3' { cmake -S $Sdl -B $SdlBuild -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$Ndk\build\cmake\android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$SdlInstall" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF }
        Invoke-Step 'build SDL3' { cmake --build $SdlBuild -j $Jobs }
        Invoke-Step 'install SDL3' { cmake --install $SdlBuild }
    }
    # shaderc: the NDK ships its source (sources/third_party/shaderc, with glslang and SPIRV-Tools); ndk-build makes static libraries.
    if (-not (Test-Path (Join-Path $env:SHADERC_ANDROID_LIBS 'libshaderc.a'))) {
        New-Item -ItemType Directory -Force -Path (Join-Path $ShadercBuild 'jni') | Out-Null
        Set-Content -Path (Join-Path $ShadercBuild 'jni\Android.mk') -Value ("include " + (Join-Path $Ndk 'sources\third_party\shaderc\Android.mk').Replace('\', '/'))
        Set-Content -Path (Join-Path $ShadercBuild 'jni\Application.mk') -Value "APP_ABI := arm64-v8a`nAPP_PLATFORM := android-30`nAPP_STL := c++_static`nAPP_OPTIM := release`nAPP_CPPFLAGS += -fPIC"
        Invoke-Step 'build shaderc (ndk-build)' { & (Join-Path $Ndk 'ndk-build.cmd') "NDK_PROJECT_PATH=$ShadercBuild" "NDK_APPLICATION_MK=$ShadercBuild\jni\Application.mk" "APP_BUILD_SCRIPT=$ShadercBuild\jni\Android.mk" "-j$Jobs" }
    }
}

if ($Clean) { Remove-Item -Recurse -Force $Out -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path (Join-Path $Out 'obj') | Out-Null
Push-Location $Out
try {
    Invoke-Step 'write the build file (build_file.py --target android-arm64)' {
        python (Join-Path $Wii 'translate\build_file.py') $Out --target android-arm64 --generated $Generated
    }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Invoke-Step 'ninja' { ninja -j $Jobs ($Target -split ',') }
    Write-Host ("ninja wall clock: {0:N0} s" -f $clock.Elapsed.TotalSeconds)
    # A copy without debug sections (shaderc's libraries bring a lot) is what goes in an APK.
    New-Item -ItemType Directory -Force -Path (Join-Path $Out 'stripped') | Out-Null
    foreach ($name in ($Target -split ',')) {
        & (Join-Path $NdkBin 'llvm-strip.exe') --strip-debug -o (Join-Path $Out "stripped\$name") (Join-Path $Out $name)
    }
    foreach ($name in ($Target -split ',')) {
        Write-Host "== $name"
        & (Join-Path $NdkBin 'llvm-readelf.exe') -h -d (Join-Path $Out $name) | Select-String 'Machine|Type:|NEEDED|SONAME'
        Get-Item (Join-Path $Out $name), (Join-Path $Out "stripped\$name") | ForEach-Object { '{0}: {1:N0} bytes' -f $_.FullName, $_.Length }
    }
    # What the title needs from the console: every undefined symbol must be one libwiirecomp.so (or libc) defines.
    & (Join-Path $NdkBin 'llvm-nm.exe') -u --undefined-only (Join-Path $Out 'libtitle.so') | Measure-Object | ForEach-Object { "libtitle.so undefined symbols: $($_.Count)" }
}
finally { Pop-Location }
