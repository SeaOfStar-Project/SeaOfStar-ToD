<#
.SYNOPSIS
Renders every launcher surface, and the Home states, to PNG files without opening a window.

.EXAMPLE
tools\render-ui.ps1 -Out work\ui-shots\after
tools\render-ui.ps1 -Out work\ui-shots\after-125 -Scale 1.25
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Out,
    [string]$Exe,
    [double]$Scale = 1.0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'build\launcher\SeaOfStar-ToD.exe' }
if (-not (Test-Path -LiteralPath $Exe)) { throw "Launcher not found: $Exe (run tools\build-launcher.ps1)" }
if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path (Get-Location) $Out }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$surfaces = 'home', 'settings', 'gamefiles', 'build', 'input', 'input-nopad', 'controls-keyboard', 'controls-listening',
    'controls-gamepad', 'controls-mouse', 'controls-player', 'widgets'
$states = 'notready', 'building', 'running', 'crash', 'stale'

$jobs = @()
foreach ($s in $surfaces) { $jobs += ,@($s, $null, "$s.png") }
foreach ($st in $states) { $jobs += ,@('home', $st, "home-$st.png") }

$failed = 0
foreach ($job in $jobs) {
    $file = Join-Path $Out $job[2]
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    $arguments = @('--render', $job[0], $file)
    if ($job[1]) { $arguments += @('--state', $job[1]) }
    if ($Scale -ne 1.0) { $arguments += @('--scale', $Scale.ToString([Globalization.CultureInfo]::InvariantCulture)) }
    # The launcher is a GUI program; Start-Process -Wait waits for it and reports its exit code.
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $file)) {
        Write-Host ("FAIL  {0}  (exit {1})" -f $job[2], $process.ExitCode) -ForegroundColor Red
        $failed++
    } else {
        Write-Host ("ok    {0}" -f $file)
    }
}
if ($failed) { throw "$failed render(s) failed" }
