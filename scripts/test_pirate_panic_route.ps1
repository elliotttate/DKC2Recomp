param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,

    [Parameter(Mandatory = $true)]
    [Alias("Input")]
    [string]$InputRecording,

    [string]$Executable = "",

    [ValidateRange(2, 1000000)]
    [int]$Frames = 45000,

    [ValidateRange(1, 1000000)]
    [int]$MinimumActiveFrames = 300,

    [ValidateRange(0, 1000)]
    [int]$MinimumCompletionFlagChanges = 1,

    [ValidateRange(0, 1000)]
    [int]$MinimumExitTransitions = 1,

    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedInputSha256 = ""
)

$ErrorActionPreference = "Stop"
$Repository = Split-Path -Parent $PSScriptRoot
if (-not $Executable) {
    $Executable = Join-Path $Repository `
        "build-snesrecomp\Release\dkc2_snesrecomp_headless.exe"
}

$RomPath = (Resolve-Path -LiteralPath $Rom).Path
$InputPath = (Resolve-Path -LiteralPath $InputRecording).Path
$ExecutablePath = (Resolve-Path -LiteralPath $Executable).Path

if ($ExpectedInputSha256) {
    $actualInputHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $InputPath).
        Hash.ToLowerInvariant()
    if ($actualInputHash -ne $ExpectedInputSha256.ToLowerInvariant()) {
        throw "Input recording SHA-256 mismatch: expected $ExpectedInputSha256, got $actualInputHash."
    }
}

$previousInput = $env:SNESRECOMP_INPUT_PLAY
$previousErrorAction = $ErrorActionPreference
$env:SNESRECOMP_INPUT_PLAY = $InputPath
try {
    $ErrorActionPreference = "Continue"
    $lines = @(& $ExecutablePath $RomPath $Frames 2>&1)
    $exitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorAction
    if ([string]::IsNullOrEmpty($previousInput)) {
        Remove-Item Env:\SNESRECOMP_INPUT_PLAY -ErrorAction SilentlyContinue
    } else {
        $env:SNESRECOMP_INPUT_PLAY = $previousInput
    }
}

$lines | ForEach-Object { Write-Host $_ }
$output = $lines -join "`n"

if ($exitCode -ne 0) {
    throw "Pirate Panic route run exited with code $exitCode."
}
if ($output -match '(?m)^\[unresolved-abandon\]') {
    throw "Pirate Panic route reached an unresolved native/interpreter dispatch. Fix the static coverage before accepting this recording."
}
if ($output -match '(?m)^\[interp_cap\]') {
    throw "Pirate Panic route hit the interpreter cap. Fix the runtime path before accepting this recording."
}
if ($output -notmatch "result=completed frames=$Frames(?:\r?\n|$)") {
    throw "Pirate Panic route did not report the requested completed frame count."
}
if ($output -notmatch "(?m)^input_play: loaded ([0-9]+) frames ") {
    throw "Pirate Panic route did not load an input recording."
}
$recordedFrames = [int64]$Matches[1]
if ($recordedFrames -le 0) {
    throw "Pirate Panic route loaded an empty input recording."
}
if ($recordedFrames -lt $Frames) {
    Write-Warning "Recording has $recordedFrames frames but the test ran $Frames; frames past EOF replay neutral input."
}

if ($output -notmatch '(?m)^run_stats .*audio_clipped_samples=([0-9]+) .*audio_max_delta=([0-9]+) ') {
    throw "Pirate Panic route did not report audio integrity telemetry."
}
if ([int64]$Matches[1] -ne 0) {
    throw "Pirate Panic route produced clipped audio."
}

if ($output -notmatch '(?m)^pirate_panic_stats entered=([01]) first_frame=(-?[0-9]+) active_frames=([0-9]+) completion_flag_changes=([0-9]+) exit_transitions=([0-9]+) ') {
    throw "Pirate Panic route did not report Pirate Panic telemetry."
}

$entered = [int]$Matches[1]
$firstFrame = [int64]$Matches[2]
$activeFrames = [int64]$Matches[3]
$completionFlagChanges = [int]$Matches[4]
$exitTransitions = [int]$Matches[5]

if ($entered -ne 1 -or $firstFrame -lt 1) {
    throw "Replay never entered Pirate Panic."
}
if ($activeFrames -lt $MinimumActiveFrames) {
    throw "Replay entered Pirate Panic but only stayed active for $activeFrames frames; expected at least $MinimumActiveFrames."
}
if ($completionFlagChanges -lt $MinimumCompletionFlagChanges) {
    throw "Replay did not change the Pirate Panic completion flags."
}
if ($exitTransitions -lt $MinimumExitTransitions) {
    throw "Replay did not trigger a Pirate Panic level-exit transition."
}

Write-Host "Pirate Panic route gate passed: entered at frame $firstFrame, active for $activeFrames frames, completion flag changes $completionFlagChanges, exit transitions $exitTransitions."
