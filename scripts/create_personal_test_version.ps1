<#
Create a private, ready-to-run copy of one normal Version NN package.

The destination MUST be outside the Git repository. The private copy adds the
caller's verified DKC2 ROM, a relative rom.cfg, and an optional saves folder.
It must never be uploaded or used as a public release artifact.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$PublicVersionDirectory,
    [Parameter(Mandatory = $true)]
    [string]$RomPath,
    [string]$SavesDirectory = "",
    [string]$LauncherConfigPath = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ExpectedRomSha256 =
    "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633"

function Resolve-From([string]$Path, [string]$Base) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}

$Repository = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$PublicVersion = Resolve-From $PublicVersionDirectory $Repository
$Rom = Resolve-From $RomPath $Repository

if (-not (Test-Path -LiteralPath $PublicVersion -PathType Container)) {
    throw "Normal version folder does not exist: $PublicVersion"
}
$VersionName = Split-Path -Leaf $PublicVersion
if ($VersionName -notmatch '^Version [0-9]+$' -or
    -not (Test-Path -LiteralPath (Join-Path $PublicVersion "VERSION.txt")) -or
    -not (Test-Path -LiteralPath (Join-Path $PublicVersion "DKC2Recomp.exe"))) {
    throw "The source must be a completed normal Version NN package."
}
if (-not (Test-Path -LiteralPath $Rom -PathType Leaf)) {
    throw "Testing ROM does not exist: $Rom"
}
$ActualRomSha256 =
    (Get-FileHash -LiteralPath $Rom -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualRomSha256 -ne $ExpectedRomSha256) {
    throw "Testing ROM SHA-256 is not the required headerless USA v1.0 image."
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputRoot = [IO.Path]::GetFullPath(
        (Join-Path $Repository "..\DKC2 Personal Test Builds"))
} else {
    $OutputRoot = Resolve-From $OutputDirectory $Repository
}
$RepositoryRoot = $Repository.TrimEnd('\') + '\'
$OutputRootWithSlash = $OutputRoot.TrimEnd('\') + '\'
if ($OutputRootWithSlash.StartsWith(
        $RepositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Private test bundles must be outside the Git repository."
}

$Destination = Join-Path $OutputRoot $VersionName
if (Test-Path -LiteralPath $Destination) {
    throw "Refusing to overwrite an existing private version: $Destination"
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
Copy-Item -LiteralPath $PublicVersion -Destination $Destination -Recurse

$RomName = Split-Path -Leaf $Rom
Copy-Item -LiteralPath $Rom -Destination (Join-Path $Destination $RomName)
[IO.File]::WriteAllText(
    (Join-Path $Destination "rom.cfg"),
    $RomName + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

if (-not [string]::IsNullOrWhiteSpace($SavesDirectory)) {
    $Saves = Resolve-From $SavesDirectory $Repository
    if (-not (Test-Path -LiteralPath $Saves -PathType Container)) {
        throw "Saves folder does not exist: $Saves"
    }
    $PrivateSaves = Join-Path $Destination "saves"
    Copy-Item -LiteralPath $Saves -Destination $PrivateSaves -Recurse
}

if (-not [string]::IsNullOrWhiteSpace($LauncherConfigPath)) {
    $LauncherConfig = Resolve-From $LauncherConfigPath $Repository
    if (-not (Test-Path -LiteralPath $LauncherConfig -PathType Leaf)) {
        throw "Launcher configuration does not exist: $LauncherConfig"
    }
    $PrivateConfig = Join-Path $Destination "launcher.cfg"
    Copy-Item -LiteralPath $LauncherConfig -Destination $PrivateConfig
}

$Notice = @(
    "PRIVATE DKC2 TEST BUNDLE",
    "",
    "This folder contains a copyrighted ROM and may contain private saves.",
    "Do not commit, upload, publish, or attach this folder to a GitHub release.",
    "The matching normal $VersionName folder remains the public-safe package.",
    "",
    "RomSha256=$ActualRomSha256",
    "CreatedUtc=$([DateTime]::UtcNow.ToString('o'))"
) -join [Environment]::NewLine
[IO.File]::WriteAllText(
    (Join-Path $Destination "PRIVATE-TEST-BUNDLE.txt"),
    $Notice + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

Write-Output "private_version_folder=$Destination"
Write-Output "private_version_name=$VersionName"
Write-Output "private_rom=$RomName"
