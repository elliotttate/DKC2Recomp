param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,

    [string]$BuildDirectory = "build-snesrecomp",

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Repository = Split-Path -Parent $PSScriptRoot
$RomPath = (Resolve-Path -LiteralPath $Rom).Path
$BuildRoot = Join-Path $Repository $BuildDirectory
$Candidates = @(
    (Join-Path $BuildRoot "$Configuration\DKC2Recomp.exe"),
    (Join-Path $BuildRoot "DKC2Recomp.exe")
)
$Executable = $Candidates | Where-Object {
    Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1

if (-not $Executable) {
    throw "Desktop build not found under '$BuildRoot'. Build the dkc2_snesrecomp_desktop target first."
}

& $Executable $RomPath
exit $LASTEXITCODE
