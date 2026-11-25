[CmdletBinding()]
param()

. "$PSScriptRoot/common.ps1"

$failures = 0
$warnings = 0

Write-Host 'SeaOfStar-ToD - environment check' -ForegroundColor White

Write-Section 'Host'

$is64 = [Environment]::Is64BitOperatingSystem
if ($is64) { Write-Item 'OS' ([Environment]::OSVersion.VersionString) 'ok' }
else { Write-Item 'OS' '64-bit Windows is required' 'fail'; $failures++ }

Write-Item 'PowerShell' $PSVersionTable.PSVersion.ToString() 'info'

Write-Section 'Required tools'

$requiredTools = @(
    @{ Name = 'git';    Label = 'git' },
    @{ Name = 'cmake';  Label = 'cmake' },
    @{ Name = 'python'; Label = 'python' }
)

foreach ($tool in $requiredTools) {
    $path = Get-CommandPath $tool.Name
    if ($path) { Write-Item $tool.Label $path 'ok' }
    else { Write-Item $tool.Label 'not found in PATH' 'fail'; $failures++ }
}

$ninja = Get-CommandPath 'ninja'
if ($ninja) { Write-Item 'ninja' $ninja 'ok' }
else { Write-Item 'ninja' 'optional; the VS generator is used by default' 'warn'; $warnings++ }

Write-Section 'Visual Studio C++ toolchain'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = $null
if (Test-Path -LiteralPath $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vsPath = ($vsPath | Select-Object -First 1)
}

if ($vsPath) {
    Write-Item 'MSVC' $vsPath 'ok'
}
else {
    Write-Item 'MSVC' 'Visual Studio 2022 Build Tools with Desktop C++ not found' 'fail'
    $failures++
}

Write-Section 'Disc tooling'

$nodtool = Get-CommandPath 'nodtool'
if (-not $nodtool -and (Test-Path -LiteralPath (Join-Path (Get-RepoRoot) 'work/tools/nodtool.exe'))) {
    $nodtool = Join-Path (Get-RepoRoot) 'work/tools/nodtool.exe'
}
if ($nodtool) { Write-Item 'nodtool' $nodtool 'ok' }
else { Write-Item 'nodtool' 'run tools/fetch-tools.ps1 to download it' 'warn'; $warnings++ }

$wit = Get-CommandPath 'wit'
if ($wit) { Write-Item 'wit' $wit 'ok' }
else { Write-Item 'wit' 'optional alternative to nodtool' 'info' }

Write-Section 'Reference emulator'

$dolphinCandidates = @(
    'C:\Program Files\Dolphin\Dolphin.exe',
    'C:\Program Files\Dolphin Emulator\Dolphin.exe',
    (Join-Path $env:LOCALAPPDATA 'Dolphin Emulator\Dolphin.exe')
)
$dolphin = $dolphinCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($dolphin) { Write-Item 'Dolphin' $dolphin 'ok' }
else { Write-Item 'Dolphin' 'install it for FIFO/frame/audio reference dumps' 'warn'; $warnings++ }

Write-Section 'Project state'

$config = $null
try {
    $config = Get-GameConfig -AllowExample
    Write-Item 'config' $config._configPath 'ok'
}
catch {
    Write-Item 'config' $_.Exception.Message 'fail'
    $failures++
}

if ($config) {
    $paths = Get-GamePaths $config

    if (Test-Path -LiteralPath $paths.Disc) {
        $size = (Get-Item -LiteralPath $paths.Disc).Length
        Write-Item 'disc image' ("{0} ({1:N2} GB)" -f $paths.Disc, ($size / 1GB)) 'ok'
    }
    else {
        Write-Item 'disc image' "missing: $($paths.Disc)" 'fail'
        $failures++
    }

    if (Test-Path -LiteralPath $paths.MainDol) {
        $size = (Get-Item -LiteralPath $paths.MainDol).Length
        Write-Item 'main.dol' ("{0} ({1:N2} MB)" -f $paths.MainDol, ($size / 1MB)) 'ok'
    }
    else {
        Write-Item 'main.dol' "missing: $($paths.MainDol)" 'warn'
        $warnings++
    }

    $manifest = Join-Path (Get-RepoRoot) 'analysis/disc-manifest.json'
    if (Test-Path -LiteralPath $manifest) { Write-Item 'disc manifest' $manifest 'ok' }
    else { Write-Item 'disc manifest' 'run tools/extract-disc.ps1' 'info' }

    $dolrecomp = Join-Path $paths.Work 'DolRecomp/build/Release/dolrecomp.exe'
    if (Test-Path -LiteralPath $dolrecomp) { Write-Item 'dolrecomp' $dolrecomp 'ok' }
    else { Write-Item 'dolrecomp' 'run tools/fetch-tools.ps1' 'info' }

    $runtimeRoot = Get-RuntimeRoot
    if (Test-Path -LiteralPath (Join-Path $runtimeRoot 'recomp/tools/add_game.py')) { Write-Item 'runtime sources' $runtimeRoot 'ok' }
    else { Write-Item 'runtime sources' "missing: $runtimeRoot" 'fail'; $failures++ }
    $consoleExe = Join-Path $runtimeRoot 'recomp/build/wii-recomp.exe'
    if (Test-Path -LiteralPath $consoleExe) { Write-Item 'wii-recomp.exe' $consoleExe 'ok' }
    else { Write-Item 'wii-recomp.exe' 'run tools/build.ps1' 'info' }
}

Write-Section 'Result'

if ($failures -gt 0) {
    Write-Host "$failures required check(s) failed, $warnings warning(s)." -ForegroundColor Red
    exit 1
}

Write-Host "Required checks passed, $warnings warning(s)." -ForegroundColor Green
exit 0
