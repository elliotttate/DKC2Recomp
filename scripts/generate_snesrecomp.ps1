param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,

    [string]$Python = "python",

    [int]$MaxInstructions = 500000,

    [int]$MaxNodes = 100000
)

$ErrorActionPreference = "Stop"

$Repository = Split-Path -Parent $PSScriptRoot
$Emitter = Join-Path $Repository "snesrecomp\tools\v2_emit.py"
$ConfigDirectory = Join-Path $Repository "recomp"
$OutputDirectory = Join-Path $Repository "generated\snesrecomp"
$ExpectedHash = "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633"
$ExpectedSize = 0x400000

if (-not (Test-Path -LiteralPath $Emitter -PathType Leaf)) {
    throw "snesrecomp is not initialized. Run: git submodule update --init --recursive"
}

$RomPath = (Resolve-Path -LiteralPath $Rom).Path
$RomFile = Get-Item -LiteralPath $RomPath
if ($RomFile.Length -ne $ExpectedSize) {
    throw "Unsupported ROM size $($RomFile.Length); expected $ExpectedSize bytes."
}

$ActualHash = (Get-FileHash -LiteralPath $RomPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualHash -ne $ExpectedHash) {
    throw "Unsupported ROM SHA-256 $ActualHash."
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

& $Python $Emitter `
    --rom $RomPath `
    --cfg-dir $ConfigDirectory `
    --out-dir $OutputDirectory `
    --no-host-root-scan `
    --no-hle `
    --max-insns $MaxInstructions `
    --max-nodes $MaxNodes

if ($LASTEXITCODE -ne 0) {
    throw "snesrecomp generation failed with exit code $LASTEXITCODE."
}

Write-Host "Generated private sources in $OutputDirectory"
Write-Host "The ROM and generated game code remain ignored by Git."
