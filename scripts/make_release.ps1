<#
Package a completed DKC2Recomp Windows release build.

The zip contains the executable, MinGW runtime dependencies, the Dear ImGui
recomp-ui assets, the North American retail cover used by the launcher,
README, changelog, and license. It deliberately does not stage ROMs, generated
C, saves, screenshots, audio, or local launcher state.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$BuildDirectory = "build-release",

    [string]$RuntimeBinDirectory = "C:\msys64\mingw64\bin"
)

$ErrorActionPreference = "Stop"

$Repository = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Repository $BuildDirectory
$Executable = Join-Path $Build "DKC2Recomp.exe"
$Assets = Join-Path $Build "assets"
$Output = Join-Path $Repository "release-stage"
$StageName = "DKC2Recomp-windows-x64-v$Version"
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

$LicenseDirectory = Join-Path $Stage "licenses"
New-Item -ItemType Directory -Path $LicenseDirectory -Force | Out-Null
$LicenseFiles = @(
    @{ Source = (Join-Path $Repository "third_party\licenses\Lato-OFL.txt"); Name = "Lato-OFL.txt" },
    @{ Source = (Join-Path $Repository "recomp-ui\src\third_party\imgui\LICENSE.txt"); Name = "DearImGui-LICENSE.txt" },
    @{ Source = (Join-Path $Repository "third_party\lakesnes_apu\LICENSE.txt"); Name = "LakeSnes-LICENSE.txt" },
    @{ Source = (Join-Path $Repository "snesrecomp\THIRD_PARTY_ATTRIBUTION.md"); Name = "snesrecomp-THIRD_PARTY_ATTRIBUTION.md" },
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\SDL2\LICENSE.txt"); Name = "SDL2-LICENSE.txt" },
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\gcc-libs\COPYING.LIB"); Name = "GCC-COPYING.LIB" },
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\gcc-libs\COPYING.RUNTIME"); Name = "GCC-COPYING.RUNTIME" },
    @{ Source = (Join-Path $RuntimeBinDirectory "..\share\licenses\libwinpthread\COPYING"); Name = "libwinpthread-COPYING" }
)
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
foreach ($Name in $RuntimeDlls) {
    $Source = Join-Path $RuntimeBinDirectory $Name
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required MinGW runtime DLL missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $Stage
}

$ForbiddenExtensions = @(
    ".sfc", ".smc", ".fig", ".swc", ".rom",
    ".sav", ".srm", ".state", ".wram", ".vram", ".oam",
    ".ppm", ".bmp", ".png", ".wav", ".pcm", ".mp3", ".flac"
)
$ForbiddenFiles = Get-ChildItem -LiteralPath $Stage -Recurse -File |
    Where-Object { $ForbiddenExtensions -contains $_.Extension.ToLowerInvariant() }
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

Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Archive

Write-Output "release_archive=$Archive"
Write-Output "release_sha256=$((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Output "release_files=$((Get-ChildItem -LiteralPath $Stage -Recurse -File).Count)"
