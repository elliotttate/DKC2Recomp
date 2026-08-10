param(
    [Parameter(Mandatory = $true)]
    [string]$Recording,
    [Parameter(Mandatory = $true)]
    [ValidateRange(0, 1000000)]
    [int]$Frame,
    [ValidateSet("composite", "bg1", "bg2", "bg3", "bg4", "obj",
                 "composite,bg1,obj", "composite,bg1,bg2,bg3,obj")]
    [string]$Layers = "composite,bg1,bg2,bg3,obj",
    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"
$Root = [IO.Path]::GetFullPath($PSScriptRoot)
$Tool = Join-Path $Root "tools\capture_widescreen_diagnostics.py"
$Executable = Join-Path $Root "dkc2_snesrecomp_diagnostics.exe"
$RomConfig = Join-Path $Root "rom.cfg"

if ([IO.Path]::IsPathRooted($Recording)) {
    $RecordingPath = [IO.Path]::GetFullPath($Recording)
} else {
    $RecordingPath = [IO.Path]::GetFullPath((Join-Path $Root $Recording))
}
foreach ($Path in @($Tool, $Executable, $RomConfig, $RecordingPath)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required diagnostic file is missing: $Path"
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
$Output = Join-Path $CaptureRoot ("frame-{0:D6}-{1}" -f $Frame, $Stamp)

$Arguments = @(
    $Tool,
    "--executable", $Executable,
    "--rom", $Rom,
    "--input-recording", $RecordingPath,
    "--frame", $Frame.ToString(),
    "--layers", $Layers,
    "--output-dir", $Output,
    "--timeout", "240"
)
$RecordingBase = [IO.Path]::GetFileNameWithoutExtension($RecordingPath)
$StartSram = Join-Path (Split-Path -Parent $RecordingPath) (
    $RecordingBase + ".start.srm")
if (Test-Path -LiteralPath $StartSram -PathType Leaf) {
    $Arguments += @("--sram", $StartSram)
}

Write-Host "Diagnosing frame $Frame..."
& $Python.Source @Arguments
if ($LASTEXITCODE -ne 0) {
    throw "Widescreen diagnosis failed with exit code $LASTEXITCODE."
}

$Report = Join-Path $Output "index.html"
if (-not (Test-Path -LiteralPath $Report -PathType Leaf)) {
    throw "Diagnosis completed without creating index.html."
}
Write-Host "Diagnostic report:"
Write-Host "  $Report"
if ($OpenReport) {
    Start-Process -FilePath $Report
}
