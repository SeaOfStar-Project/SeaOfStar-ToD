[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'Release',
    [switch]$Clean
)

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
$config = Get-GameConfig -AllowExample
$paths = Get-GamePaths $config

$source = Join-Path $root 'launcher'
$buildDir = Join-Path $paths.Build 'launcher'

if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
    throw "Launcher sources not found: $source"
}

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    Write-Item 'build directory' 'removing' 'warn'
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

Write-Section 'Configure'

if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    Invoke-Checked 'cmake' @('-S', $source, '-B', $buildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64') (Join-Path $paths.Logs 'launcher-configure.log')
}
else {
    Write-Item 'build directory' "$buildDir (already configured)" 'ok'
}

Write-Section 'Build'

Invoke-Checked 'cmake' @('--build', $buildDir, '--config', $Configuration, '--parallel') (Join-Path $paths.Logs 'launcher-build.log')

$exe = Join-Path $buildDir "$Configuration/SeaOfStar-ToD.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Build finished but $exe was not produced"
}

Copy-Item -LiteralPath $exe -Destination (Join-Path $buildDir 'SeaOfStar-ToD.exe') -Force

Write-Section 'Result'
Write-Item 'launcher' $exe 'ok'
Write-Item 'launcher path' (Join-Path $buildDir 'SeaOfStar-ToD.exe') 'ok'
