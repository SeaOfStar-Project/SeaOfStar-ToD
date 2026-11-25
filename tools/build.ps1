[CmdletBinding()]
param(
    [switch]$NoPack,
    [int]$Jobs = 8,
    [switch]$Verify,
    [string]$Disc
)

# Builds the runtime for the user's own extracted disc: translates it, compiles the title
# library and (unless -NoPack) packs the pre-baked image. This is a thin wrapper over
# tools/wii-recomp-build.ps1, which the launcher's Build page also runs.

. "$PSScriptRoot/common.ps1"

$forward = @{ Jobs = $Jobs }
if ($NoPack) { $forward.NoPack = $true }
if ($Verify) { $forward.Verify = $true }
if ($Disc) { $forward.Disc = $Disc }
& (Join-Path $PSScriptRoot 'wii-recomp-build.ps1') @forward
exit $LASTEXITCODE
