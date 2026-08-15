param(
    [Parameter(Mandatory = $true)]
    [string]$Recording,
    [ValidateRange(0, 1000000)]
    [int]$Start = 0,
    [ValidateRange(1, 10000)]
    [int]$Step = 12,
    [ValidateSet("composite", "bg1", "bg2", "bg3", "bg4", "obj",
                 "composite,bg1,obj", "composite,bg1,bg2,bg3,obj")]
    [string]$Layers = "composite,bg1,bg2,bg3,obj",
    [string]$OutputDirectory,
    [switch]$ReuseCapture,
    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"
$Root = [IO.Path]::GetFullPath($PSScriptRoot)
$Tool = Join-Path $Root "tools\audit_widescreen_route.py"
$Executable = Join-Path $Root "dkc2_snesrecomp_headless.exe"
$RomConfig = Join-Path $Root "rom.cfg"

$RecordingPath = if ([IO.Path]::IsPathRooted($Recording)) {
    [IO.Path]::GetFullPath($Recording)
} else {
    [IO.Path]::GetFullPath((Join-Path $Root $Recording))
}
foreach ($Path in @($Tool, $Executable, $RomConfig, $RecordingPath)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required route-audit file is missing: $Path"
    }
}

$RomName = (Get-Content -LiteralPath $RomConfig |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Select-Object -First 1).Trim()
$Rom = if ([IO.Path]::IsPathRooted($RomName)) {
    [IO.Path]::GetFullPath($RomName)
} else {
    [IO.Path]::GetFullPath((Join-Path $Root $RomName))
}
if (-not (Test-Path -LiteralPath $Rom -PathType Leaf)) {
    throw "Configured ROM is missing: $Rom"
}

$Python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $Python) {
    throw "Python was not found. Install Python 3 or add python.exe to PATH."
}

$CaptureRoot = Join-Path $Root "captures"
New-Item -ItemType Directory -Path $CaptureRoot -Force | Out-Null
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$Output = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $CaptureRoot ("route-{0}-{1}" -f (
        [IO.Path]::GetFileNameWithoutExtension($RecordingPath)), $Stamp)
} elseif ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
}
if ($ReuseCapture -and [string]::IsNullOrWhiteSpace($OutputDirectory)) {
    throw "-ReuseCapture requires -OutputDirectory for the prior capture folder."
}

$Arguments = @(
    $Tool,
    "--executable", $Executable,
    "--rom", $Rom,
    "--input-recording", $RecordingPath,
    "--start", $Start.ToString(),
    "--step", $Step.ToString(),
    "--layers", $Layers,
    "--output-dir", $Output,
    "--timeout", "900"
)
$RecordingBase = [IO.Path]::GetFileNameWithoutExtension($RecordingPath)
$StartSram = Join-Path (Split-Path -Parent $RecordingPath) (
    $RecordingBase + ".start.srm")
if (Test-Path -LiteralPath $StartSram -PathType Leaf) {
    $Arguments += @("--sram", $StartSram)
}
if ($ReuseCapture) {
    $Arguments += "--reuse-capture"
}

if ($ReuseCapture) {
    Write-Host "Reanalyzing existing route capture $RecordingBase..."
} else {
    Write-Host "Auditing route $RecordingBase (one sample every $Step frames)..."
}
& $Python.Source @Arguments
if ($LASTEXITCODE -ne 0) {
    throw "Widescreen route audit failed with exit code $LASTEXITCODE."
}

$Report = Join-Path $Output "index.html"
if (-not (Test-Path -LiteralPath $Report -PathType Leaf)) {
    throw "Route audit completed without creating index.html."
}
Write-Host "Route audit report:"
Write-Host "  $Report"
if ($OpenReport) {
    Start-Process -FilePath $Report
}
