param(
    [Parameter(Mandatory = $true)]
    [string]$Script
)

$ErrorActionPreference = "Stop"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$Root = Join-Path ([IO.Path]::GetTempPath()) (
    "dkc2-version-test-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))
$Build = Join-Path $Root "build"
$Output = Join-Path $Root "versions"
$Assets = @(
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

try {
    New-Item -ItemType Directory -Path $Build -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $Build "DKC2Recomp.exe"), [byte[]](1, 2, 3))
    [IO.File]::WriteAllBytes((Join-Path $Build "DKC2RecompSDL.exe"), [byte[]](4, 5, 6))
    foreach ($Relative in $Assets) {
        $Path = Join-Path $Build $Relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
        [IO.File]::WriteAllBytes($Path, [byte[]](7, 8, 9))
    }

    & $Script -BuildDirectory $Build -OutputDirectory $Output -AllowDirty | Out-Null
    & $Script -BuildDirectory $Build -OutputDirectory $Output -AllowDirty | Out-Null
    $Version1 = Join-Path $Output "Version 01"
    $Version2 = Join-Path $Output "Version 02"
    Assert-True `
        (Test-Path -LiteralPath $Version1 -PathType Container) `
        "The first automatic snapshot was not Version 01."
    Assert-True `
        (Test-Path -LiteralPath $Version2 -PathType Container) `
        "The second automatic snapshot was not Version 02."
    Assert-True `
        (Test-Path -LiteralPath (Join-Path $Version1 "VERSION.txt") -PathType Leaf) `
        "Version 01 is missing its provenance manifest."
    $Manifest = Get-Content -LiteralPath (Join-Path $Version1 "VERSION.txt") -Raw
    Assert-True ($Manifest -match '(?m)^SnapshotSequence=01\r?$') `
        "The manifest does not distinguish the snapshot sequence."
    Assert-True ($Manifest -match '(?m)^ProjectVersion=0\.0\.1\r?$') `
        "The manifest does not record the semantic project version."
    Assert-True ($Manifest -match '(?m)^SourceWorkingTreeDirty=true\r?$') `
        "The explicitly allowed dirty test snapshot was not marked dirty."

    $OverwriteRejected = $false
    try {
        & $Script -BuildDirectory $Build -OutputDirectory $Output -Sequence 2 -AllowDirty | Out-Null
    } catch {
        $OverwriteRejected = $true
    }
    Assert-True $OverwriteRejected "An existing numbered version was overwritten."
    $Forbidden = @(Get-ChildItem -LiteralPath $Version2 -Recurse -File |
        Where-Object { $_.Extension -in @(".smc", ".sav", ".srm", ".png") })
    Assert-True ($Forbidden.Count -eq 0) "A private artifact entered the version folder."

    Write-Output "version snapshot tests passed"
} finally {
    if (Test-Path -LiteralPath $Root) {
        $Resolved = [IO.Path]::GetFullPath($Root)
        $TempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $Resolved.StartsWith($TempRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove test output outside the temporary directory: $Resolved"
        }
        Remove-Item -LiteralPath $Resolved -Recurse -Force
    }
}
