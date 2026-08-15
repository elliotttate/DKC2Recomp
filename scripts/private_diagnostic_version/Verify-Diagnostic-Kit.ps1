param(
    [ValidateRange(60, 600)]
    [int]$Frames = 120
)

$ErrorActionPreference = "Stop"
$Root = [IO.Path]::GetFullPath($PSScriptRoot)
$Required = @(
    "DKC2Recomp.exe",
    "DKC2RecompSDL.exe",
    "dkc2_snesrecomp_headless.exe",
    "dkc2_snesrecomp_diagnostics.exe",
    "rom.cfg",
    "launcher.cfg",
    "keybinds.ini",
    "TESTING_README.md",
    "Record-Pirate-Panic.ps1",
    "Diagnose-Frame.ps1",
    "Audit-Route.ps1",
    "tools\capture_tcp_screenshot.py",
    "tools\capture_widescreen_diagnostics.py",
    "tools\audit_widescreen_route.py",
    "tools\validate_swanky_run.py"
)
foreach ($Relative in $Required) {
    $Path = Join-Path $Root $Relative
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Diagnostic kit is missing: $Relative"
    }
}
foreach ($Relative in @("recordings", "captures", "diagnostics", "saves")) {
    $Path = Join-Path $Root $Relative
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Diagnostic kit is missing folder: $Relative"
    }
}

$Name = "self-test-{0}.input" -f (Get-Date -Format "yyyyMMdd-HHmmss")
& (Join-Path $Root "Record-Pirate-Panic.ps1") `
    -RecordingName $Name -TestFrames $Frames
$Recording = Join-Path $Root ("recordings\" + $Name)
$Count = (Get-Content -LiteralPath $Recording).Count
if ($Count -ne $Frames) {
    throw "Expected $Frames recorded frames; found $Count."
}

& (Join-Path $Root "Diagnose-Frame.ps1") `
    -Recording ("recordings\" + $Name) `
    -Frame ($Frames - 1) `
    -Layers "composite,bg1,obj"

Write-Host ""
Write-Host "Private diagnostic kit verification passed."
Write-Host "Recorded and replayed $Frames deterministic frames."
