param([Parameter(Mandatory = $true)][string]$Script)
$ErrorActionPreference = 'Stop'
$Root = Join-Path ([IO.Path]::GetTempPath()) ('dkc2-release-test-' + [Guid]::NewGuid().ToString('N'))
function Fixture([string]$Relative) {
    $Path = Join-Path $Root $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, "synthetic fixture: $Relative")
}
try {
    Fixture 'scripts/make_release.ps1'
    Copy-Item -LiteralPath $Script -Destination (Join-Path $Root 'scripts/make_release.ps1')
    foreach ($Relative in @(
        'build/DKC2Recomp.exe', 'build/DKC2RecompSDL.exe', 'build/SDL2.dll',
        'build/assets/fonts/LatoLatin-Regular.ttf', 'build/assets/fonts/LatoLatin-Bold.ttf',
        'build/assets/img/brand_mark.tga', 'build/assets/img/verdict_ok.tga',
        'build/assets/img/verdict_warn.tga', 'build/assets/img/verdict_bad.tga',
        'build/assets/img/verdict_none.tga', 'build/assets/img/pad.tga', 'build/assets/img/boxart.tga',
        'README.md', 'CHANGELOG.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
        'docs/PROJECT_KONGS.md', 'docs/MSU1_AUDIO.md', 'scripts/import_project_kongs.py',
        'third_party/dkc3_menu/LICENSE', 'third_party/dkc_msu1/LICENSE-DKC1.txt',
        'third_party/dkc_msu1/LICENSE-DKC3.txt', 'third_party/project_kongs/LICENSE-MIT.txt',
        'third_party/licenses/Lato-OFL.txt', 'recomp-ui/src/third_party/imgui/LICENSE.txt',
        'third_party/lakesnes_apu/LICENSE.txt', 'snesrecomp/THIRD_PARTY_ATTRIBUTION.md',
        'sdl/LICENSE.txt', 'runtime/msvcp140.dll', 'runtime/vcruntime140.dll',
        'runtime/vcruntime140_1.dll', 'runtime/SDL2.dll', 'runtime/libgcc_s_seh-1.dll',
        'runtime/libstdc++-6.dll', 'runtime/libwinpthread-1.dll',
        'share/licenses/SDL2/LICENSE.txt', 'share/licenses/gcc-libs/COPYING.LIB',
        'share/licenses/gcc-libs/COPYING.RUNTIME', 'share/licenses/libwinpthread/COPYING',
        'build/rom.cfg', 'build/msu1.cfg', 'build/kongs.cfg', 'build/private.sfc',
        'build/private.pcm', 'build/private.dkc2kongs', 'build/saves/save.srm')) { Fixture $Relative }
    foreach ($Kind in @('MSVC', 'MinGW')) {
        & (Join-Path $Root 'scripts/make_release.ps1') -Version '1.2.3' -BuildDirectory 'build' `
            -RuntimeKind $Kind -RuntimeBinDirectory (Join-Path $Root 'runtime') `
            -SdlLicensePath (Join-Path $Root 'sdl/LICENSE.txt') | Out-Null
        $Archive = Join-Path $Root 'release-stage/DKC2Recomp-v1.2.3-Windows-x64.zip'
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $Zip = [IO.Compression.ZipFile]::OpenRead($Archive)
        try {
            $Names = @($Zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
            foreach ($Name in @('DKC2Recomp.exe', 'DKC2RecompSDL.exe', 'SDL2.dll',
                'docs/MSU1_AUDIO.md', 'scripts/import_project_kongs.py',
                'third_party/dkc_msu1/LICENSE-DKC1.txt')) {
                if ($Name -notin $Names) { throw "Missing packaged file: $Name" }
            }
            if (@($Names | Where-Object { $_ -match '\.(sfc|pcm|srm|cfg|dkc2kongs)$' }).Count) {
                throw 'Private input entered release'
            }
            $ExpectedRuntime = if ($Kind -eq 'MSVC') { 'vcruntime140_1.dll' } else { 'libwinpthread-1.dll' }
            if ($ExpectedRuntime -notin $Names) { throw 'Runtime not packaged' }
            $Reader = [IO.StreamReader]::new($Zip.GetEntry('VERSION.txt').Open())
            try { $Manifest = $Reader.ReadToEnd() } finally { $Reader.Dispose() }
            # Independent .NET calculation checks the manifest's public hash.
            $Sha = [Security.Cryptography.SHA256]::Create()
            try {
                $Expected = [BitConverter]::ToString($Sha.ComputeHash(
                    [IO.File]::ReadAllBytes((Join-Path $Root 'build/DKC2Recomp.exe')))).Replace('-', '').ToLowerInvariant()
            } finally { $Sha.Dispose() }
            if ($Manifest -notmatch "(?m)^DKC2RecompSha256=$Expected\r?`$") {
                throw 'Executable checksum does not match fixture'
            }
        } finally { $Zip.Dispose() }
        $Sha = [Security.Cryptography.SHA256]::Create()
        try {
            $Hash = [BitConverter]::ToString($Sha.ComputeHash(
                [IO.File]::ReadAllBytes($Archive))).Replace('-', '').ToLowerInvariant()
        } finally { $Sha.Dispose() }
        if (-not (Get-Content -LiteralPath "$Archive.sha256" -Raw).StartsWith($Hash)) {
            throw 'Checksum does not match archive'
        }
    }
    $Rejected = $false
    try {
        & (Join-Path $Root 'scripts/make_release.ps1') -Version '../outside' | Out-Null
    } catch { $Rejected = $true }
    if (-not $Rejected) { throw 'Invalid release path was accepted' }
    Write-Output 'release package tests passed'
} finally {
    $Resolved = [IO.Path]::GetFullPath($Root)
    $TempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $Resolved.StartsWith($TempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside temporary directory'
    }
    if (Test-Path -LiteralPath $Resolved) { Remove-Item -LiteralPath $Resolved -Recurse -Force }
}
