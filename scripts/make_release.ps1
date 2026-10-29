<#
Package a completed DKC2Recomp Windows release build.

The zip contains the executables, selected compiler runtime dependencies, the Dear ImGui
recomp-ui assets, the North American retail cover used by the launcher,
README, changelog, and license. It deliberately does not stage ROMs, generated
C, saves, screenshots, audio, or local launcher state.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$BuildDirectory = "build-release",

    [string]$RuntimeBinDirectory = "C:\msys64\mingw64\bin",

    [ValidateSet("MinGW", "MSVC")]
    [string]$RuntimeKind = "MinGW",

    [string]$SdlLicensePath = ""
)

$ErrorActionPreference = "Stop"
# CTest may inherit PowerShell 7's module path while running Windows PowerShell.
# Resolve hashing and ZIP commands against the executing host's bundled modules.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Archive/Microsoft.PowerShell.Archive.psd1')
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$') {
    throw "Version must be a three-part release number with an optional suffix."
}

$Repository = Split-Path -Parent $PSScriptRoot
$Build = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else { Join-Path $Repository $BuildDirectory }
$Executable = Join-Path $Build "DKC2Recomp.exe"
$Assets = Join-Path $Build "assets"
$Output = Join-Path $Repository "release-stage"
$StageName = "DKC2Recomp-v$Version-Windows-x64"
$Stage = Join-Path $Output $StageName
$Archive = Join-Path $Output "$StageName.zip"

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Release executable missing: $Executable"
}
if (-not (Test-Path -LiteralPath $Assets -PathType Container)) {
    throw "Dear ImGui launcher assets missing: $Assets"
}

$OutputRoot = [IO.Path]::GetFullPath($Output).TrimEnd('\') + '\'
$StagePath = [IO.Path]::GetFullPath($Stage)
$ArchivePath = [IO.Path]::GetFullPath($Archive)
if (-not $StagePath.StartsWith($OutputRoot, [StringComparison]::OrdinalIgnoreCase) -or
    -not $ArchivePath.StartsWith($OutputRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean release paths outside release-stage."
}

if (Test-Path -LiteralPath $Stage) {
    Remove-Item -LiteralPath $Stage -Recurse -Force
}
if (Test-Path -LiteralPath $Archive) {
    Remove-Item -LiteralPath $Archive -Force
}
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

Copy-Item -LiteralPath $Executable -Destination $Stage
if (Test-Path -LiteralPath (Join-Path $Build "DKC2RecompSDL.exe")) {
    Copy-Item -LiteralPath (Join-Path $Build "DKC2RecompSDL.exe") -Destination $Stage
}

# recomp-ui's build directory contains art for every supported console. Stage
# only the generic launcher chrome, licensed fonts, SNES controller art, and
# the DKC2 cover explicitly selected by this project.
$StageFonts = Join-Path $Stage "assets\fonts"
$StageImages = Join-Path $Stage "assets\img"
New-Item -ItemType Directory -Path $StageFonts -Force | Out-Null
New-Item -ItemType Directory -Path $StageImages -Force | Out-Null
$LauncherAssets = @(
    @{ Source = "assets\fonts\LatoLatin-Regular.ttf"; Destination = $StageFonts },
    @{ Source = "assets\fonts\LatoLatin-Bold.ttf"; Destination = $StageFonts },
    @{ Source = "assets\img\brand_mark.tga"; Destination = $StageImages },
    @{ Source = "assets\img\verdict_ok.tga"; Destination = $StageImages },
    @{ Source = "assets\img\verdict_warn.tga"; Destination = $StageImages },
    @{ Source = "assets\img\verdict_bad.tga"; Destination = $StageImages },
    @{ Source = "assets\img\verdict_none.tga"; Destination = $StageImages },
    @{ Source = "assets\img\pad.tga"; Destination = $StageImages },
    @{ Source = "assets\img\boxart.tga"; Destination = $StageImages }
)
foreach ($Asset in $LauncherAssets) {
    $Source = Join-Path $Build $Asset.Source
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required launcher asset missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $Asset.Destination
}

Copy-Item -LiteralPath (Join-Path $Repository "README.md") -Destination $Stage
Copy-Item -LiteralPath (Join-Path $Repository "CHANGELOG.md") -Destination $Stage
Copy-Item -LiteralPath (Join-Path $Repository "LICENSE") -Destination $Stage
Copy-Item -LiteralPath (Join-Path $Repository "THIRD_PARTY_NOTICES.md") -Destination $Stage
$ReleaseNotes = Join-Path $Repository "RELEASE-NOTES-v$Version.md"
if (Test-Path -LiteralPath $ReleaseNotes -PathType Leaf) {
    Copy-Item -LiteralPath $ReleaseNotes -Destination $Stage
}
foreach ($Relative in @("docs\PROJECT_KONGS.md", "docs\MSU1_AUDIO.md",
                        "scripts\import_project_kongs.py")) {
    $Target = Join-Path $Stage $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $Target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Repository $Relative) -Destination $Target
}
foreach ($Component in @("dkc3_menu", "dkc_msu1", "project_kongs")) {
    $Target = Join-Path $Stage "third_party\$Component"
    New-Item -ItemType Directory -Path $Target -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $Repository "third_party\$Component") -File |
        Where-Object { $_.Name -eq "README.md" -or $_.Name -like "LICENSE*" } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $Target }
}

$LicenseDirectory = Join-Path $Stage "licenses"
New-Item -ItemType Directory -Path $LicenseDirectory -Force | Out-Null
$LicenseFiles = @(
    @{ Source = (Join-Path $Repository "third_party\licenses\Lato-OFL.txt"); Name = "Lato-OFL.txt" },
    @{ Source = (Join-Path $Repository "recomp-ui\src\third_party\imgui\LICENSE.txt"); Name = "DearImGui-LICENSE.txt" },
    @{ Source = (Join-Path $Repository "third_party\lakesnes_apu\LICENSE.txt"); Name = "LakeSnes-LICENSE.txt" },
    @{ Source = (Join-Path $Repository "snesrecomp\THIRD_PARTY_ATTRIBUTION.md"); Name = "snesrecomp-THIRD_PARTY_ATTRIBUTION.md" }
)
if ($RuntimeKind -eq "MinGW") {
    $LicenseFiles += @(
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\SDL2\LICENSE.txt"); Name = "SDL2-LICENSE.txt" },
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\gcc-libs\COPYING.LIB"); Name = "GCC-COPYING.LIB" },
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\gcc-libs\COPYING.RUNTIME"); Name = "GCC-COPYING.RUNTIME" },
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\libwinpthread\COPYING"); Name = "libwinpthread-COPYING" }
)
} else {
    if (-not $SdlLicensePath) { throw "MSVC packaging requires -SdlLicensePath for the built SDL2 revision." }
    $LicenseFiles += @{ Source = $SdlLicensePath; Name = "SDL2-LICENSE.txt" }
}
foreach ($LicenseFile in $LicenseFiles) {
    $Source = [IO.Path]::GetFullPath($LicenseFile.Source)
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required third-party notice missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination (Join-Path $LicenseDirectory $LicenseFile.Name)
}

$RuntimeDlls = @(
    "SDL2.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll"
)
if ($RuntimeKind -eq "MSVC") {
    Copy-Item -LiteralPath (Join-Path $Build "SDL2.dll") -Destination $Stage
    $RuntimeDlls = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
}
foreach ($Name in $RuntimeDlls) {
    $Source = Join-Path $RuntimeBinDirectory $Name
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required $RuntimeKind runtime DLL missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $Stage
}

$ForbiddenExtensions = @(
    ".sfc", ".smc", ".fig", ".swc", ".rom",
    ".sav", ".srm", ".state", ".wram", ".vram", ".oam",
    ".ppm", ".bmp", ".png", ".wav", ".pcm", ".mp3", ".flac", ".dkc2kongs"
)
$ForbiddenFiles = Get-ChildItem -LiteralPath $Stage -Recurse -File |
    Where-Object {
        $ForbiddenExtensions -contains $_.Extension.ToLowerInvariant() -or
        $_.Name -in @("rom.cfg", "launcher.cfg", "kongs.cfg", "msu1.cfg", "keybinds.ini")
    }
if ($ForbiddenFiles) {
    throw "Release contains forbidden ROM/save/capture assets: $($ForbiddenFiles.FullName -join ', ')"
}

$ExpectedBoxArt = [IO.Path]::GetFullPath((Join-Path $StageImages "boxart.tga"))
$NamedCoverAssets = @(Get-ChildItem -LiteralPath $Stage -Recurse -File |
    Where-Object { $_.Name -match "(?i)boxart|cover[_-]?art" })
$UnexpectedCoverAssets = @($NamedCoverAssets | Where-Object {
    -not [IO.Path]::GetFullPath($_.FullName).Equals(
        $ExpectedBoxArt, [StringComparison]::OrdinalIgnoreCase)
})
if ($UnexpectedCoverAssets) {
    throw "Release contains unexpected cover art: $($UnexpectedCoverAssets.FullName -join ', ')"
}
if ($NamedCoverAssets.Count -ne 1 -or
    -not (Test-Path -LiteralPath $ExpectedBoxArt -PathType Leaf)) {
    throw "Release must contain exactly the allowlisted DKC2 launcher cover: $ExpectedBoxArt"
}

$ForbiddenDirectories = Get-ChildItem -LiteralPath $Stage -Recurse -Directory |
    Where-Object { $_.Name -in @("generated", "private", "saves") }
if ($ForbiddenDirectories) {
    throw "Release contains forbidden private/generated directories: $($ForbiddenDirectories.FullName -join ', ')"
}

$SourceCommit = "unversioned"
if (Test-Path -LiteralPath (Join-Path $Repository '.git')) {
    $SourceCommit = & git -C $Repository rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record release source commit' }
}
@(
    "ProjectVersion=$Version", "SourceCommit=$SourceCommit", "RuntimeKind=$RuntimeKind",
    "DKC2RecompSha256=$((Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant())",
    "No ROM, character pack, music pack, saves or local configuration is included."
) | Set-Content -LiteralPath (Join-Path $Stage 'VERSION.txt') -Encoding UTF8
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Archive
$Hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$Archive.sha256", "$Hash  $StageName.zip`n", [Text.Encoding]::ASCII)

Write-Output "release_archive=$Archive"
Write-Output "release_sha256=$((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Output "release_files=$((Get-ChildItem -LiteralPath $Stage -Recurse -File).Count)"
