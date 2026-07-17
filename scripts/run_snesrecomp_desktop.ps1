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
$Executable = Join-Path $Repository `
    "$BuildDirectory\$Configuration\dkc2_snesrecomp_desktop.exe"

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Desktop build not found at '$Executable'. Build the dkc2_snesrecomp_desktop target first."
}

& $Executable $RomPath
exit $LASTEXITCODE
