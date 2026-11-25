[CmdletBinding()]
param(
    [switch]$SkipDolRecompBuild,
    [switch]$ModernGekko,
    [switch]$WiiRecomp
)

. "$PSScriptRoot/common.ps1"

$root = Get-RepoRoot
$work = Join-Path $root 'work'
$tools = Join-Path $work 'tools'
$logs = Join-Path $root 'logs'

New-Item -ItemType Directory -Force -Path $work, $tools, $logs | Out-Null

Write-Section 'nodtool'

$nodtoolPath = Join-Path $tools 'nodtool.exe'
$nodtoolVersion = 'v2.0.0-alpha.12'
$nodtoolUrl = "https://github.com/encounter/nod/releases/download/$nodtoolVersion/nodtool-windows-x86_64.exe"
$nodtoolSha256 = 'f65de62c44f0ad5d179eff85bd1e92099713ffacbf717f651fb761129a25c7bc'

if (Test-Path -LiteralPath $nodtoolPath) {
    $hash = Get-Sha256 $nodtoolPath
    if ($hash -eq $nodtoolSha256) {
        Write-Item 'nodtool' "$nodtoolPath (verified)" 'ok'
    }
    else {
        Write-Item 'nodtool' "hash mismatch, re-downloading" 'warn'
        Remove-Item -LiteralPath $nodtoolPath -Force
    }
}

if (-not (Test-Path -LiteralPath $nodtoolPath)) {
    Write-Item 'nodtool' "downloading $nodtoolVersion" 'info'
    Invoke-Checked 'curl.exe' @('-L', '--fail', '--silent', '--show-error', '--output', $nodtoolPath, $nodtoolUrl) (Join-Path $logs 'nodtool-download.log')
    $hash = Get-Sha256 $nodtoolPath
    if ($hash -ne $nodtoolSha256) {
        throw "nodtool hash mismatch: expected $nodtoolSha256, got $hash"
    }
    Write-Item 'nodtool' "$nodtoolPath (verified)" 'ok'
}

Write-Section 'DolRecomp'

$dolrecompDir = Join-Path $work 'DolRecomp'
$dolrecompExe = Join-Path $dolrecompDir 'build/Release/dolrecomp.exe'

if (-not (Test-Path -LiteralPath (Join-Path $dolrecompDir 'CMakeLists.txt'))) {
    Invoke-Checked 'git' @('clone', '--depth', '1', 'https://github.com/ExpansionPak/DolRecomp', $dolrecompDir) (Join-Path $logs 'clone-dolrecomp.log')
}

Push-Location $dolrecompDir
try {
    Invoke-Checked 'git' @('submodule', 'update', '--init', '--recursive') (Join-Path $logs 'dolrecomp-submodule.log')
}
finally {
    Pop-Location
}

if ($SkipDolRecompBuild) {
    Write-Item 'DolRecomp' 'clone complete, build skipped' 'info'
}
elseif (Test-Path -LiteralPath $dolrecompExe) {
    Write-Item 'DolRecomp' "$dolrecompExe (already built)" 'ok'
}
else {
    Invoke-Checked 'cmake' @('-S', $dolrecompDir, '-B', (Join-Path $dolrecompDir 'build'), '-G', 'Visual Studio 17 2022', '-A', 'x64') (Join-Path $logs 'dolrecomp-configure.log')
    Invoke-Checked 'cmake' @('--build', (Join-Path $dolrecompDir 'build'), '--config', 'Release', '--parallel') (Join-Path $logs 'dolrecomp-build.log')

    if (-not (Test-Path -LiteralPath $dolrecompExe)) {
        throw "DolRecomp build finished but $dolrecompExe was not produced"
    }
    Write-Item 'DolRecomp' $dolrecompExe 'ok'
}

if ($ModernGekko) {
    Write-Section 'ModernGekko-Template'
    $gekkoDir = Join-Path $work 'ModernGekko-Template'
    if (-not (Test-Path -LiteralPath (Join-Path $gekkoDir 'CMakeLists.txt'))) {
        Write-Item 'ModernGekko' 'cloning with submodules (large, Dolphin-derived)' 'info'
        Invoke-Checked 'git' @('clone', '--recurse-submodules', '--depth', '1', 'https://github.com/ExpansionPak/ModernGekko-Template', $gekkoDir) (Join-Path $logs 'clone-moderngekko.log')
    }
    else {
        Write-Item 'ModernGekko' 'already cloned' 'ok'
    }
    Write-Item 'ModernGekko' 'build with its own instructions; see docs/04-tooling.md' 'info'
}

if ($WiiRecomp) {
    Write-Section 'wii-recomp'
    $wiiDir = Get-RuntimeRoot
    if (Test-Path -LiteralPath (Join-Path $root 'runtime/recomp/tools/add_game.py')) {
        Write-Item 'wii-recomp' 'the runtime is part of this repository (runtime/)' 'ok'
    }
    elseif (-not (Test-Path -LiteralPath (Join-Path $wiiDir 'README.md'))) {
        $wiiDir = Join-Path $work 'wii-recomp'
        Invoke-Checked 'git' @('clone', '--depth', '1', 'https://github.com/Zontex/wii-recomp', $wiiDir) (Join-Path $logs 'clone-wii-recomp.log')
    }
    else {
        Write-Item 'wii-recomp' 'already cloned' 'ok'
    }
    if (-not (Test-Path -LiteralPath 'C:\msys64\clang64\bin\clang.exe')) {
        Write-Item 'MSYS2 CLANG64' 'missing; see docs/04-tooling.md' 'warn'
    }
    else {
        Write-Item 'MSYS2 CLANG64' 'C:\msys64\clang64' 'ok'
    }
}

Write-Section 'Result'
Write-Host 'Tool fetch complete.' -ForegroundColor Green
