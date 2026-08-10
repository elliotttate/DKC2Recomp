param(
    [string]$RecordingName = "pirate-panic-full-route.input",
    [ValidateRange(0, 1000000)]
    [int]$TestFrames = 0
)

$ErrorActionPreference = "Stop"
$Root = [IO.Path]::GetFullPath($PSScriptRoot)
$RecordingDirectory = Join-Path $Root "recordings"
$DiagnosticsDirectory = Join-Path $Root "diagnostics"
$Executable = Join-Path $Root "DKC2Recomp.exe"
$RomConfig = Join-Path $Root "rom.cfg"

if ([IO.Path]::GetFileName($RecordingName) -ne $RecordingName -or
    -not $RecordingName.EndsWith(
        ".input", [StringComparison]::OrdinalIgnoreCase)) {
    throw "RecordingName must be a single filename ending in .input."
}
foreach ($Path in @($Executable, $RomConfig)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Diagnostic kit is incomplete: $Path"
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

New-Item -ItemType Directory -Path $RecordingDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $DiagnosticsDirectory -Force | Out-Null
$Recording = Join-Path $RecordingDirectory $RecordingName
if (Test-Path -LiteralPath $Recording) {
    throw "Recording already exists. Choose a new name: $Recording"
}

$RecordingBase = [IO.Path]::GetFileNameWithoutExtension($RecordingName)
$StartSram = Join-Path $RecordingDirectory ($RecordingBase + ".start.srm")
$SessionPath = Join-Path $RecordingDirectory (
    $RecordingBase + ".session.json")
$Tier2Manifest = Join-Path $DiagnosticsDirectory (
    $RecordingBase + ".tier2_coverage.json")
$PerformanceCapture = Join-Path $DiagnosticsDirectory (
    $RecordingBase + ".performance.log")
$LastRunCapture = Join-Path $DiagnosticsDirectory (
    $RecordingBase + ".last_run_report.json")
$LivePerformance = Join-Path $Root "performance.log"
$LiveLastRun = Join-Path $DiagnosticsDirectory "last_run_report.json"
foreach ($EvidencePath in @(
        $Recording, $StartSram, $SessionPath, $Tier2Manifest,
        $PerformanceCapture, $LastRunCapture)) {
    if (Test-Path -LiteralPath $EvidencePath) {
        throw "Diagnostic evidence already exists. Choose a new recording name: $EvidencePath"
    }
}
$LiveSram = Join-Path $Root "saves\save.srm"
if (Test-Path -LiteralPath $LiveSram -PathType Leaf) {
    Copy-Item -LiteralPath $LiveSram -Destination $StartSram
}

$StartInfo = [Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Executable
$StartInfo.WorkingDirectory = $Root
$StartInfo.UseShellExecute = $false
$StartInfo.Arguments = '"' + $Rom + '"'
$StartInfo.EnvironmentVariables["SNESRECOMP_INPUT_REC"] = $Recording
$StartInfo.EnvironmentVariables["SNESRECOMP_TIER2_MANIFEST"] = $Tier2Manifest
$StartInfo.EnvironmentVariables["SNESRECOMP_INTERP_TRACE"] = "1"
$StartInfo.EnvironmentVariables["DKC2_DESKTOP_PERF"] = "1"
$StartInfo.EnvironmentVariables["DKC2_WIDESCREEN"] = "1"
if ($TestFrames -gt 0) {
    $StartInfo.EnvironmentVariables["DKC2_DESKTOP_TEST_FRAMES"] =
        $TestFrames.ToString()
    $StartInfo.EnvironmentVariables["DKC2_DESKTOP_TEST_HIDDEN"] = "1"
    $StartInfo.EnvironmentVariables["DKC2_DESKTOP_DISABLE_SRAM"] = "1"
    $StartInfo.EnvironmentVariables["SNESRECOMP_NO_LAUNCHER"] = "1"
}

# These two filenames are rolling program outputs, not named evidence. Remove
# them before launch so an absent output cannot be mistaken for this run.
foreach ($RollingPath in @($LivePerformance, $LiveLastRun)) {
    if (Test-Path -LiteralPath $RollingPath -PathType Leaf) {
        Remove-Item -LiteralPath $RollingPath -Force
    }
}
$StartedUtc = [DateTime]::UtcNow
Write-Host "Recording enabled:"
Write-Host "  $Recording"
if (Test-Path -LiteralPath $StartSram) {
    Write-Host "Starting SRAM preserved:"
    Write-Host "  $StartSram"
}
Write-Host "Close the game normally when the route is complete."

$Process = [Diagnostics.Process]::new()
$Process.StartInfo = $StartInfo
try {
    if (-not $Process.Start()) {
        throw "Windows could not launch DKC2Recomp.exe."
    }
    $Process.WaitForExit()
    $ExitCode = $Process.ExitCode
} finally {
    $Process.Dispose()
}

if (-not (Test-Path -LiteralPath $Recording -PathType Leaf)) {
    throw "The game exited without creating the recording: $Recording"
}
$FrameCount = (Get-Content -LiteralPath $Recording).Count
if ($FrameCount -le 0) {
    throw "The recording was created but contains no frames: $Recording"
}
if ($ExitCode -ne 0) {
    throw "DKC2Recomp exited with code $ExitCode after recording $FrameCount frames."
}

if (-not (Test-Path -LiteralPath $LivePerformance -PathType Leaf)) {
    throw "The run closed without writing its performance log: $LivePerformance"
}
if ((Get-Item -LiteralPath $LivePerformance).LastWriteTimeUtc -lt
    $StartedUtc) {
    throw "Performance log was not freshly written by this run: $LivePerformance"
}
Copy-Item -LiteralPath $LivePerformance -Destination $PerformanceCapture
if (-not (Test-Path -LiteralPath $LiveLastRun -PathType Leaf)) {
    throw "The run closed without writing its last-run report: $LiveLastRun"
}
if ((Get-Item -LiteralPath $LiveLastRun).LastWriteTimeUtc -lt
    $StartedUtc) {
    throw "Last-run report was not freshly written by this run: $LiveLastRun"
}
Copy-Item -LiteralPath $LiveLastRun -Destination $LastRunCapture
if (-not (Test-Path -LiteralPath $Tier2Manifest -PathType Leaf)) {
    throw "The run closed without writing tier-2 coverage: $Tier2Manifest"
}

$Session = [ordered]@{
    schema = "dkc2-private-recording-v1"
    recording = $RecordingName
    frames = $FrameCount
    last_frame = $FrameCount - 1
    started_utc = $StartedUtc.ToString("o")
    completed_utc = [DateTime]::UtcNow.ToString("o")
    executable_sha256 = (
        Get-FileHash -LiteralPath $Executable -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    rom_sha256 = (
        Get-FileHash -LiteralPath $Rom -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    starting_sram = if (Test-Path -LiteralPath $StartSram) {
        Split-Path -Leaf $StartSram
    } else {
        $null
    }
    tier2_coverage = Split-Path -Leaf $Tier2Manifest
    performance_log = if (
        Test-Path -LiteralPath $PerformanceCapture -PathType Leaf) {
        Split-Path -Leaf $PerformanceCapture
    } else {
        $null
    }
    last_run_report = if (
        Test-Path -LiteralPath $LastRunCapture -PathType Leaf) {
        Split-Path -Leaf $LastRunCapture
    } else {
        $null
    }
}
$Session | ConvertTo-Json | Set-Content -LiteralPath $SessionPath -Encoding UTF8

Write-Host ""
Write-Host "Recording succeeded."
Write-Host "Frames: $FrameCount"
Write-Host "Last frame: $($FrameCount - 1)"
Write-Host "Session metadata: $SessionPath"
Write-Host "Tier-2 coverage: $Tier2Manifest"
if (Test-Path -LiteralPath $PerformanceCapture -PathType Leaf) {
    Write-Host "Performance log: $PerformanceCapture"
}
if (Test-Path -LiteralPath $LastRunCapture -PathType Leaf) {
    Write-Host "Last-run report: $LastRunCapture"
}
