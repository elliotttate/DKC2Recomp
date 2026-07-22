param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,

    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [ValidateSet("clean", "die", "seh")]
    [string]$Mode = "die",

    [switch]$KeepArtifacts
)

$ErrorActionPreference = "Stop"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$romPath = (Resolve-Path -LiteralPath $Rom).Path
$exePath = (Resolve-Path -LiteralPath $Executable).Path
$exeDirectory = Split-Path -Parent $exePath
$diagnostics = Join-Path $exeDirectory (
    "diagnostic-drill-{0}-{1}-{2}" -f $Mode, $PID,
    [Guid]::NewGuid().ToString("N"))

$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $exePath
$start.WorkingDirectory = $exeDirectory
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.CreateNoWindow = $true
$start.Arguments = '"' + $romPath + '"'
$start.EnvironmentVariables["DKC2_DIAGNOSTICS_DIR"] = $diagnostics
$start.EnvironmentVariables["DKC2_DESKTOP_TEST_FRAMES"] = "180"
$start.EnvironmentVariables["DKC2_DESKTOP_TEST_HIDDEN"] = "1"
$start.EnvironmentVariables["DKC2_DESKTOP_DISABLE_SRAM"] = "1"
$start.EnvironmentVariables["SNESRECOMP_NO_LAUNCHER"] = "1"
if ($Mode -eq "clean") {
    $start.EnvironmentVariables["DKC2_DIAGNOSTIC_BUNDLE"] = "1"
} else {
    $start.EnvironmentVariables["SNESRECOMP_CRASH_TEST"] = $Mode
}

$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $start
$completed = $false
try {
    Assert-True $process.Start() "Could not launch the diagnostic drill."
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $completed = $process.WaitForExit(30000)
    if (-not $completed) {
        $process.Kill()
        throw "Diagnostic drill timed out after 30 seconds."
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()

    if ($Mode -eq "clean") {
        Assert-True ($process.ExitCode -eq 0) (
            "Clean drill exited with {0}.`n{1}`n{2}" -f
            $process.ExitCode, $stdout, $stderr)
    } else {
        Assert-True ($process.ExitCode -ne 0) (
            "Crash drill unexpectedly exited successfully.`n{0}`n{1}" -f
            $stdout, $stderr)
    }

    $rolling = Join-Path $diagnostics "last_run_report.json"
    Assert-True (Test-Path -LiteralPath $rolling -PathType Leaf) (
        "Missing rolling diagnostic report: $rolling")
    $bundles = @(Get-ChildItem -LiteralPath $diagnostics -Directory |
        Where-Object Name -Like "diagnostic_bundle_*")
    Assert-True ($bundles.Count -eq 1) (
        "Expected exactly one diagnostic bundle; found $($bundles.Count).")

    $reportPath = Join-Path $bundles[0].FullName "report.json"
    $readmePath = Join-Path $bundles[0].FullName "README.txt"
    Assert-True (Test-Path -LiteralPath $reportPath -PathType Leaf) (
        "Diagnostic bundle is missing report.json.")
    Assert-True (Test-Path -LiteralPath $readmePath -PathType Leaf) (
        "Diagnostic bundle is missing README.txt.")
    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    Assert-True ($report.schema -eq "dkc2-diagnostic-v1") (
        "Unexpected diagnostic schema: $($report.schema)")
    Assert-True ($report.dkc2.host -eq "sdl2") (
        "Unexpected diagnostic host: $($report.dkc2.host)")
    foreach ($field in @("rom_bytes", "rom_path", "save_data", "save_states",
            "generated_game_code", "screenshots", "audio_capture")) {
        Assert-True ($report.privacy.$field -eq $false) (
            "Privacy declaration '$field' was not false.")
    }

    $expectedOutcome = @{
        clean = "clean_exit"
        die = "fatal_exit"
        seh = "crashed"
    }[$Mode]
    Assert-True ($report.dkc2.outcome -eq $expectedOutcome) (
        "Expected outcome '$expectedOutcome'; got '$($report.dkc2.outcome)'.")
    if ($Mode -ne "clean") {
        Assert-True (-not [string]::IsNullOrWhiteSpace($report.fatal)) (
            "Crash report does not contain a fatal reason.")
        Assert-True ($report.dkc2.frame -ge 119) (
            "Crash drill fired before its deterministic frame checkpoint.")
    }

    $allowed = @(
        "README.txt", "report.json", "launcher.cfg", "performance.log",
        "crash_minidump.dmp")
    $unexpected = @(Get-ChildItem -LiteralPath $bundles[0].FullName -File |
        Where-Object Name -NotIn $allowed)
    Assert-True ($unexpected.Count -eq 0) (
        "Bundle contains a non-allowlisted file: " +
        (($unexpected | Select-Object -ExpandProperty Name) -join ", "))
    if ($Mode -eq "seh") {
        $dump = Join-Path $bundles[0].FullName "crash_minidump.dmp"
        Assert-True (Test-Path -LiteralPath $dump -PathType Leaf) (
            "Windows exception bundle is missing crash_minidump.dmp.")
        Assert-True ((Get-Item -LiteralPath $dump).Length -gt 0) (
            "Windows exception minidump is empty.")
    }

    Write-Output (
        "diagnostic_drill=passed mode={0} outcome={1} files={2}" -f
        $Mode, $report.dkc2.outcome,
        (@(Get-ChildItem -LiteralPath $bundles[0].FullName -File).Count))
} finally {
    $process.Dispose()
    if ($completed -and -not $KeepArtifacts -and
        (Test-Path -LiteralPath $diagnostics)) {
        Remove-Item -LiteralPath $diagnostics -Recurse -Force
    } elseif (Test-Path -LiteralPath $diagnostics) {
        Write-Output "Diagnostic artifacts preserved at: $diagnostics"
    }
}
