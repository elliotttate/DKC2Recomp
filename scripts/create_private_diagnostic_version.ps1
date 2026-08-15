<#
Create one append-only, private DKC2 widescreen diagnostic kit.

Unlike the normal public packager, this explicitly includes the caller's
verified ROM, saves, private input fixtures, trace executable, and diagnostic
tools. The destination must remain outside the Git repository.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$RomPath,
    [string]$BuildDirectory = "build-snesrecomp\Release",
    [string]$TraceBuildDirectory = "build-snesrecomp-diagnostics\Release",
    [string]$SavesDirectory = "build-snesrecomp\Release\saves",
    [string]$LauncherConfigPath = "build-snesrecomp\Release\launcher.cfg",
    [string]$KeybindsConfigPath = "build-snesrecomp\Release\keybinds.ini",
    [string]$InputRecordingsDirectory = "recordings",
    [string]$OutputDirectory = "",
    [ValidateRange(1, 9999)]
    [int]$Sequence = 10
)

$ErrorActionPreference = "Stop"
$ExpectedRomSha256 =
    "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633"

function Resolve-FromRepository([string]$Path, [string]$Repository) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $Repository $Path))
}

$Repository = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$Build = Resolve-FromRepository $BuildDirectory $Repository
$TraceBuild = Resolve-FromRepository $TraceBuildDirectory $Repository
$Rom = Resolve-FromRepository $RomPath $Repository
$Saves = Resolve-FromRepository $SavesDirectory $Repository
$LauncherConfig = Resolve-FromRepository $LauncherConfigPath $Repository
$KeybindsConfig = Resolve-FromRepository $KeybindsConfigPath $Repository
$InputRecordings =
    Resolve-FromRepository $InputRecordingsDirectory $Repository
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputRoot = [IO.Path]::GetFullPath(
        (Join-Path $Repository "..\DKC2 Personal Test Builds"))
} else {
    $OutputRoot = Resolve-FromRepository $OutputDirectory $Repository
}

$RepositoryWithSlash = $Repository.TrimEnd('\') + '\'
$OutputWithSlash = $OutputRoot.TrimEnd('\') + '\'
if ($OutputWithSlash.StartsWith(
        $RepositoryWithSlash, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Private diagnostic versions must be outside the Git repository."
}

$VersionName = "Version {0:D2}" -f $Sequence
$Destination = [IO.Path]::GetFullPath((Join-Path $OutputRoot $VersionName))
$Staging = [IO.Path]::GetFullPath((Join-Path $OutputRoot (
    ".{0}.diagnostic-staging-{1}" -f $VersionName, $PID)))
foreach ($Path in @($Destination, $Staging)) {
    if (-not ($Path.TrimEnd('\') + '\').StartsWith(
            $OutputWithSlash, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing a diagnostic path outside the output root: $Path"
    }
}
if (Test-Path -LiteralPath $Destination) {
    throw "Refusing to overwrite existing private version: $Destination"
}
if (Test-Path -LiteralPath $Staging) {
    throw "Diagnostic staging path already exists: $Staging"
}

$RequiredFiles = [ordered]@{
    "DKC2Recomp.exe" =
        (Join-Path $Build "DKC2Recomp.exe")
    "DKC2RecompSDL.exe" =
        (Join-Path $Build "DKC2RecompSDL.exe")
    "dkc2_snesrecomp_headless.exe" =
        (Join-Path $Build "dkc2_snesrecomp_headless.exe")
    "dkc2_snesrecomp_diagnostics.exe" =
        (Join-Path $TraceBuild "dkc2_snesrecomp_headless.exe")
}
foreach ($Source in $RequiredFiles.Values) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required executable is missing: $Source"
    }
}
foreach ($Path in @($Rom, $LauncherConfig, $KeybindsConfig)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required private file is missing: $Path"
    }
}
if (-not (Test-Path -LiteralPath $Saves -PathType Container)) {
    throw "Required saves folder is missing: $Saves"
}
$ActualRomSha256 =
    (Get-FileHash -LiteralPath $Rom -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualRomSha256 -ne $ExpectedRomSha256) {
    throw "ROM is not the required headerless DKC2 USA v1.0 image."
}

$LauncherAssets = @(
    "assets\fonts\LatoLatin-Regular.ttf",
    "assets\fonts\LatoLatin-Bold.ttf",
    "assets\img\brand_mark.tga",
    "assets\img\verdict_ok.tga",
    "assets\img\verdict_warn.tga",
    "assets\img\verdict_bad.tga",
    "assets\img\verdict_none.tga",
    "assets\img\pad.tga",
    "assets\img\boxart.tga"
)
foreach ($Relative in $LauncherAssets) {
    $Path = Join-Path $Build $Relative
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required launcher asset is missing: $Path"
    }
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
try {
    New-Item -ItemType Directory -Path $Staging | Out-Null
    foreach ($Folder in @(
            "assets", "captures", "diagnostics", "recordings", "saves",
            "tools")) {
        New-Item -ItemType Directory -Path (Join-Path $Staging $Folder) `
            -Force | Out-Null
    }
    foreach ($Pair in $RequiredFiles.GetEnumerator()) {
        Copy-Item -LiteralPath $Pair.Value `
            -Destination (Join-Path $Staging $Pair.Key)
    }
    foreach ($Relative in $LauncherAssets) {
        $Target = Join-Path $Staging $Relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $Target) `
            -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $Build $Relative) `
            -Destination $Target
    }

    $RomName = Split-Path -Leaf $Rom
    Copy-Item -LiteralPath $Rom -Destination (Join-Path $Staging $RomName)
    [IO.File]::WriteAllText(
        (Join-Path $Staging "rom.cfg"),
        $RomName + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $LauncherConfig `
        -Destination (Join-Path $Staging "launcher.cfg")
    Copy-Item -LiteralPath $KeybindsConfig `
        -Destination (Join-Path $Staging "keybinds.ini")
    Get-ChildItem -LiteralPath $Saves -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName `
            -Destination (Join-Path $Staging "saves") -Recurse
    }
    if (Test-Path -LiteralPath $InputRecordings -PathType Container) {
        Get-ChildItem -LiteralPath $InputRecordings -File -Filter "*.input" |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName `
                    -Destination (Join-Path $Staging "recordings")
                $BaseName = [IO.Path]::GetFileNameWithoutExtension($_.Name)
                foreach ($Suffix in @(".start.srm", ".session.json")) {
                    $PairedSource = Join-Path $InputRecordings (
                        $BaseName + $Suffix)
                    if (Test-Path -LiteralPath $PairedSource -PathType Leaf) {
                        Copy-Item -LiteralPath $PairedSource `
                            -Destination (Join-Path $Staging "recordings")
                    }
                }
            }
    }

    Copy-Item -LiteralPath (
        Join-Path $Repository "scripts\capture_tcp_screenshot.py") `
        -Destination (Join-Path $Staging "tools")
    Copy-Item -LiteralPath (
        Join-Path $Repository "scripts\dkc2_symbols_generated.py") `
        -Destination (Join-Path $Staging "tools")
    Copy-Item -LiteralPath (
        Join-Path $Repository "scripts\capture_widescreen_diagnostics.py") `
        -Destination (Join-Path $Staging "tools")
    Copy-Item -LiteralPath (
        Join-Path $Repository "scripts\audit_widescreen_route.py") `
        -Destination (Join-Path $Staging "tools")
    Copy-Item -LiteralPath (
        Join-Path $Repository "scripts\validate_swanky_run.py") `
        -Destination (Join-Path $Staging "tools")
    foreach ($Name in @(
            "Record-Pirate-Panic.ps1", "Diagnose-Frame.ps1", "Audit-Route.ps1",
            "Verify-Diagnostic-Kit.ps1", "TESTING_README.md")) {
        Copy-Item -LiteralPath (
            Join-Path $Repository "scripts\private_diagnostic_version\$Name") `
            -Destination (Join-Path $Staging $Name)
    }
    Copy-Item -LiteralPath (
        Join-Path $Repository "docs\WIDESCREEN_DIAGNOSTICS.md") `
        -Destination (Join-Path $Staging "WIDESCREEN_DIAGNOSTICS.md")

    $Branch = (& git -C $Repository branch --show-current).Trim()
    $Commit = (& git -C $Repository rev-parse HEAD).Trim()
    $WorkingTreeStatus =
        (& git -C $Repository status --porcelain | Out-String).Trim()
    $WorkingTreeDirty =
        -not [string]::IsNullOrWhiteSpace($WorkingTreeStatus)
    $ManifestLines = @(
        "PRIVATE DKC2 DIAGNOSTIC TEST BUILD",
        "",
        "Contains a copyrighted ROM and may contain private saves, input",
        "recordings, WRAM, VRAM, and screenshots.",
        "DO NOT COMMIT, UPLOAD, PUBLISH, OR ATTACH THIS FOLDER TO GITHUB.",
        "",
        "Version=$VersionName",
        "CreatedUtc=$([DateTime]::UtcNow.ToString('o'))",
        "SourceBranch=$Branch",
        "SourceCommit=$Commit",
        "SourceWorkingTreeDirty=$($WorkingTreeDirty.ToString().ToLowerInvariant())",
        "RomSha256=$ActualRomSha256"
    )
    foreach ($Name in $RequiredFiles.Keys) {
        $Hash = (Get-FileHash -LiteralPath (
            Join-Path $Staging $Name) -Algorithm SHA256).Hash.ToLowerInvariant()
        $ManifestLines += "$($Name)Sha256=$Hash"
    }
    [IO.File]::WriteAllText(
        (Join-Path $Staging "PRIVATE-TEST-BUNDLE.txt"),
        ($ManifestLines -join [Environment]::NewLine) +
            [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))

    Move-Item -LiteralPath $Staging -Destination $Destination
} catch {
    $StagingWithSlash = $Staging.TrimEnd('\') + '\'
    if ($StagingWithSlash.StartsWith(
            $OutputWithSlash, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $Staging)) {
        Remove-Item -LiteralPath $Staging -Recurse -Force
    }
    throw
}

Write-Output "private_diagnostic_version=$Destination"
Write-Output "rom_sha256=$ActualRomSha256"
Write-Output "files=$((Get-ChildItem -LiteralPath $Destination -Recurse -File).Count)"
