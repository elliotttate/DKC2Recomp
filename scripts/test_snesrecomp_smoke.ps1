param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,

    [string]$Executable = "",

    [ValidateRange(2, 1000000)]
    [int]$Frames = 600,

    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedFrameSha256 = "",

    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedWramSha256 = "",

    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedVramSha256 = "",

    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedCgramSha256 = "",

    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedOamSha256 = "",

    [ValidateRange(0, 1000)]
    [int]$MinimumAttractCycles = 0,

    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedStateEventSha256 = "",

    [ValidatePattern('^$|^[0-9a-fA-F]{16}$')]
    [string]$ExpectedAudioFnv1a = "",

    [ValidateRange(1, 65536)]
    [int]$MaximumAudioDelta = 20000,

    [switch]$RequireOamSourceMatch
)

$ErrorActionPreference = "Stop"
$Repository = Split-Path -Parent $PSScriptRoot
if (-not $Executable) {
    $Executable = Join-Path $Repository `
        "build-snesrecomp\Release\dkc2_snesrecomp_headless.exe"
}

$RomPath = (Resolve-Path -LiteralPath $Rom).Path
$ExecutablePath = (Resolve-Path -LiteralPath $Executable).Path
# Current SNESrecomp can announce its append-only Tier-2 discovery journal on
# stderr. That diagnostic is not a smoke-test failure, but Windows PowerShell
# promotes native stderr to an ErrorRecord while ErrorActionPreference is Stop.
# Keep ordinary gates quiet; dedicated Tier-2 capture runs can opt in directly.
$env:SNESRECOMP_TIER2_VERBOSE = "0"
$lines = @(& $ExecutablePath $RomPath $Frames 2>&1)
$exitCode = $LASTEXITCODE
$lines | ForEach-Object { Write-Host $_ }
$output = $lines -join "`n"

if ($exitCode -ne 0) {
    throw "Native smoke run exited with code $exitCode."
}
if ($output -notmatch "result=completed frames=$Frames(?:\r?\n|$)") {
    throw "Native smoke run did not report the requested completed frame count."
}
if ($output -notmatch "cgram_words=([0-9]+)") {
    throw "Native smoke run did not report CGRAM occupancy."
}
if ([int]$Matches[1] -eq 0) {
    throw "Native smoke run completed with an empty palette (CGRAM)."
}
if ($output -notmatch 'intro_state=\$([0-9a-fA-F]{4})') {
    throw "Native smoke run did not report the DKC2 intro state."
}
if ([Convert]::ToInt32($Matches[1], 16) -eq 0) {
    throw "DKC2 intro state did not advance from zero."
}

if ($output -notmatch "run_stats video_active_frames=([0-9]+)") {
    throw "Native smoke run did not report aggregate video activity."
}
if ([int64]$Matches[1] -eq 0) {
    throw "Native smoke run never produced a nonzero framebuffer."
}
if ($output -notmatch '(?m)^run_stats .*audio_frames=([0-9]+) audio_nonzero_samples=([0-9]+) .*audio_clipped_samples=([0-9]+) audio_peak=([0-9]+) audio_max_delta=([0-9]+) audio_fnv1a=([0-9a-fA-F]{16})$') {
    throw "Native smoke run did not report complete audio integrity telemetry."
}
$audioFrames = [int64]$Matches[1]
$audioNonzeroSamples = [int64]$Matches[2]
$audioClippedSamples = [int64]$Matches[3]
$audioPeak = [int]$Matches[4]
$audioMaxDelta = [int]$Matches[5]
$audioFnv1a = $Matches[6].ToLowerInvariant()
if ($audioFrames -eq 0 -or $audioNonzeroSamples -eq 0) {
    throw "Native smoke run did not produce active audio."
}
if ($audioClippedSamples -ne 0 -or $audioPeak -ge 32760) {
    throw "Native smoke run produced clipped audio ($audioClippedSamples samples, peak $audioPeak)."
}
if ($audioMaxDelta -gt $MaximumAudioDelta) {
    throw "Native smoke run produced a suspicious audio discontinuity ($audioMaxDelta > $MaximumAudioDelta)."
}
if ($ExpectedAudioFnv1a -and
    $audioFnv1a -ne $ExpectedAudioFnv1a.ToLowerInvariant()) {
    throw "audio_fnv1a mismatch: expected $ExpectedAudioFnv1a, got $audioFnv1a."
}

function Get-ReportedHash {
    param([Parameter(Mandatory = $true)][string]$Name)
    if ($output -notmatch "(?m)^$Name=([0-9a-fA-F]{64})$") {
        throw "Native smoke run did not report $Name."
    }
    return $Matches[1].ToLowerInvariant()
}

function Assert-ExpectedHash {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowEmptyString()][string]$Expected = ""
    )
    if (-not $Expected) {
        return
    }
    $actual = Get-ReportedHash -Name $Name
    if ($actual -ne $Expected.ToLowerInvariant()) {
        throw "$Name mismatch: expected $Expected, got $actual."
    }
}

$oamHash = Get-ReportedHash -Name "oam_sha256"
$oamSourceHash = Get-ReportedHash -Name "oam_source_sha256"
if ($RequireOamSourceMatch -and $oamHash -ne $oamSourceHash) {
    throw "PPU OAM differs from DKC2's complete WRAM OAM source table."
}

Assert-ExpectedHash -Name "frame_sha256" -Expected $ExpectedFrameSha256
Assert-ExpectedHash -Name "wram_sha256" -Expected $ExpectedWramSha256
Assert-ExpectedHash -Name "vram_sha256" -Expected $ExpectedVramSha256
Assert-ExpectedHash -Name "cgram_sha256" -Expected $ExpectedCgramSha256
Assert-ExpectedHash -Name "oam_sha256" -Expected $ExpectedOamSha256
Assert-ExpectedHash -Name "state_event_sha256" `
    -Expected $ExpectedStateEventSha256

if ($output -notmatch '(?m)^state_stats events=([0-9]+) .*attract_cycles=([0-9]+) sequence_errors=([0-9]+) ') {
    throw "Native smoke run did not report attract-state statistics."
}
$attractCycles = [int]$Matches[2]
$sequenceErrors = [int]$Matches[3]
if ($sequenceErrors -ne 0) {
    throw "Attract sequence reported $sequenceErrors ordering error(s)."
}
if ($attractCycles -lt $MinimumAttractCycles) {
    throw "Expected at least $MinimumAttractCycles complete attract cycles, got $attractCycles."
}

Write-Host "Native smoke gate passed: $Frames frames, advancing intro, nonzero palette, active video/audio without clipping or discontinuities, reported OAM state, and $attractCycles complete attract cycle(s)."
