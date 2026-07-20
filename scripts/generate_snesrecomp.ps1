param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,

    [string]$Python = "python",

    [string]$SnesrecompRoot = "",

    [ValidateSet("native", "python", "auto")]
    [string]$AnalysisBackend = "native",

    [int]$MaxInstructions = 4096,

    [int]$MaxNodes = 100000,

    [int]$BankShardThresholdKiB = 1024,

    [int]$BankShardPcSpan = 0x10
)

$ErrorActionPreference = "Stop"

$Repository = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SnesrecompRoot)) {
    $SnesrecompRoot = Join-Path $Repository "snesrecomp"
} else {
    $SnesrecompRoot = (Resolve-Path -LiteralPath $SnesrecompRoot).Path
}
$Emitter = Join-Path $SnesrecompRoot "tools\v2_emit.py"
$NativeBuilder = Join-Path $SnesrecompRoot "tools\build_native_analyzer.py"
$NativeBinaryName = if ($env:OS -eq "Windows_NT") {
    "snesrecomp-analyze.exe"
} else {
    "snesrecomp-analyze"
}
$NativeAnalyzer = Join-Path $SnesrecompRoot `
    "recompiler-rs\target\release\$NativeBinaryName"
$HeaderSync = Join-Path $SnesrecompRoot "tools\v2_sync_funcs_h.py"
$ConfigDirectory = Join-Path $Repository "recomp"
$OutputDirectory = Join-Path $Repository "generated\snesrecomp"
$ExpectedHash = "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633"
$ExpectedSize = 0x400000

if (-not (Test-Path -LiteralPath $Emitter -PathType Leaf)) {
    throw "snesrecomp is not initialized. Run: git submodule update --init --recursive"
}

if (-not (Test-Path -LiteralPath $HeaderSync -PathType Leaf)) {
    throw "snesrecomp header synchronizer is missing: $HeaderSync"
}

if ($AnalysisBackend -eq "native") {
    if (-not (Test-Path -LiteralPath $NativeBuilder -PathType Leaf)) {
        throw "snesrecomp native analyzer builder is missing: $NativeBuilder"
    }

    $Cargo = Get-Command cargo -ErrorAction SilentlyContinue
    if ($null -ne $Cargo) {
        # Refresh source builds when the Rust toolchain is available.
        & $Python $NativeBuilder
        if ($LASTEXITCODE -ne 0) {
            throw "snesrecomp native analyzer build failed with exit code $LASTEXITCODE."
        }
    } elseif (-not (Test-Path -LiteralPath $NativeAnalyzer -PathType Leaf)) {
        throw "Cargo is unavailable and no prebuilt native analyzer exists at $NativeAnalyzer."
    } else {
        Write-Host "Cargo is unavailable; using prebuilt native analyzer: $NativeAnalyzer"
    }
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

& $Python $HeaderSync `
    --cfg-dir $ConfigDirectory `
    --out (Join-Path $ConfigDirectory "funcs.h")

if ($LASTEXITCODE -ne 0) {
    throw "snesrecomp funcs.h synchronization failed with exit code $LASTEXITCODE."
}

& $Python $Emitter `
    --rom $RomPath `
    --cfg-dir $ConfigDirectory `
    --out-dir $OutputDirectory `
    --no-host-root-scan `
    --no-hle `
    --cfg-roots `
    --analysis-backend $AnalysisBackend `
    --max-insns $MaxInstructions `
    --max-nodes $MaxNodes `
    --bank-shard-threshold-kib $BankShardThresholdKiB `
    --bank-shard-pc-span $BankShardPcSpan

if ($LASTEXITCODE -ne 0) {
    throw "snesrecomp generation failed with exit code $LASTEXITCODE."
}

Write-Host "Generated private sources in $OutputDirectory"
Write-Host "The ROM and generated game code remain ignored by Git."
