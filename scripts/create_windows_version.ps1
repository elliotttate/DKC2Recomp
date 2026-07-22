<#
Create an append-only, user-testable Windows snapshot.

The compiler build tree is intentionally reused, but every testable handoff is
copied into a new "Version NN" folder. Existing versions are never overwritten.
ROMs, saves, generated code, captures, diagnostics, and local configuration are
excluded by an allowlist plus a final forbidden-content audit.
#>
param(
    [string]$BuildDirectory = "build-snesrecomp\Release",
    [string]$OutputDirectory = "versions",
    [ValidateRange(0, 9999)]
    [int]$Sequence = 0,
    [switch]$AllowDirty
)

$ErrorActionPreference = "Stop"

function Resolve-FromRepository([string]$Path, [string]$Repository) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $Repository $Path))
}

function Invoke-RepositoryGit([string]$Arguments, [string]$Repository) {
    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = "git.exe"
    $StartInfo.Arguments = "-C `"$Repository`" $Arguments"
    $StartInfo.UseShellExecute = $false
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::Start($StartInfo)
    $StandardOutput = $Process.StandardOutput.ReadToEnd()
    $StandardError = $Process.StandardError.ReadToEnd()
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) {
        throw "Git command failed: git $Arguments`n$StandardError"
    }
    return $StandardOutput.Trim()
}

$Repository = Split-Path -Parent $PSScriptRoot
$Build = Resolve-FromRepository $BuildDirectory $Repository
$Output = Resolve-FromRepository $OutputDirectory $Repository

$Executables = @("DKC2Recomp.exe", "DKC2RecompSDL.exe")
foreach ($Name in $Executables) {
    $Path = Join-Path $Build $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required playable executable is missing: $Path"
    }
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

$Branch = Invoke-RepositoryGit "branch --show-current" $Repository
if (-not $Branch) { $Branch = "detached" }
$Commit = Invoke-RepositoryGit "rev-parse HEAD" $Repository
$Status = Invoke-RepositoryGit `
    "-c core.safecrlf=false status --porcelain=v1 --untracked-files=normal --ignore-submodules=none" `
    $Repository
$Dirty = -not [string]::IsNullOrWhiteSpace($Status)
if ($Dirty -and -not $AllowDirty) {
    throw "Refusing to package a dirty source tree. Commit/stash it, or pass -AllowDirty for an explicitly marked development snapshot."
}

$ProjectText = Get-Content -LiteralPath (Join-Path $Repository "CMakeLists.txt") -Raw
$ProjectVersion = "unknown"
if ($ProjectText -match '(?s)project\s*\([^)]*?VERSION\s+([^\s\)]+)') {
    $ProjectVersion = $Matches[1]
}
$CacheCandidates = @(
    (Join-Path $Build "CMakeCache.txt"),
    (Join-Path (Split-Path -Parent $Build) "CMakeCache.txt")
)
$CMakeGenerator = "unknown"
$BuildConfiguration = "unknown"
foreach ($Candidate in $CacheCandidates) {
    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        $CacheLines = Get-Content -LiteralPath $Candidate
        $GeneratorLine = $CacheLines |
            Where-Object { $_ -like 'CMAKE_GENERATOR:INTERNAL=*' } |
            Select-Object -First 1
        if ($GeneratorLine) {
            $CMakeGenerator = $GeneratorLine.Substring($GeneratorLine.IndexOf('=') + 1)
        }
        $BuildTypeLine = $CacheLines |
            Where-Object { $_ -like 'CMAKE_BUILD_TYPE:STRING=*' } |
            Select-Object -First 1
        if ($BuildTypeLine -and $BuildTypeLine.Substring($BuildTypeLine.IndexOf('=') + 1)) {
            $BuildConfiguration = $BuildTypeLine.Substring($BuildTypeLine.IndexOf('=') + 1)
        } elseif ((Split-Path -Leaf $Build) -match '^(Debug|Release|RelWithDebInfo|MinSizeRel)$') {
            $BuildConfiguration = Split-Path -Leaf $Build
        }
        break
    }
}

New-Item -ItemType Directory -Path $Output -Force | Out-Null
$OutputRoot = [IO.Path]::GetFullPath($Output).TrimEnd('\') + '\'

if ($Sequence -eq 0) {
    $Highest = 0
    foreach ($Directory in Get-ChildItem -LiteralPath $Output -Directory) {
        if ($Directory.Name -match '^Version ([0-9]+)$') {
            $Value = [int]$Matches[1]
            if ($Value -gt $Highest) { $Highest = $Value }
        }
    }
    $Sequence = $Highest + 1
}

$FolderName = "Version {0:D2}" -f $Sequence
$Destination = Join-Path $Output $FolderName
$Staging = Join-Path $Output (".{0}.staging-{1}" -f $FolderName, $PID)
$DestinationPath = [IO.Path]::GetFullPath($Destination)
$StagingPath = [IO.Path]::GetFullPath($Staging)
foreach ($Path in @($DestinationPath, $StagingPath)) {
    if (-not $Path.StartsWith($OutputRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to create a version path outside the output directory: $Path"
    }
}
if (Test-Path -LiteralPath $Destination) {
    throw "$FolderName already exists. Existing versions are never overwritten."
}
if (Test-Path -LiteralPath $Staging) {
    throw "Temporary version staging path already exists: $Staging"
}

try {
    New-Item -ItemType Directory -Path $Staging | Out-Null
    foreach ($Name in $Executables) {
        Copy-Item -LiteralPath (Join-Path $Build $Name) -Destination $Staging
    }

    foreach ($Relative in $LauncherAssets) {
        $Target = Join-Path $Staging $Relative
        $TargetDirectory = Split-Path -Parent $Target
        New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $Build $Relative) -Destination $Target
    }

    foreach ($Name in @("README.md", "CHANGELOG.md", "LICENSE",
                         "THIRD_PARTY_NOTICES.md")) {
        Copy-Item -LiteralPath (Join-Path $Repository $Name) -Destination $Staging
    }

    $PrimaryExecutable = Join-Path $Staging "DKC2Recomp.exe"
    $PortableExecutable = Join-Path $Staging "DKC2RecompSDL.exe"
    $PrimaryHash = (Get-FileHash -LiteralPath $PrimaryExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    $PortableHash = (Get-FileHash -LiteralPath $PortableExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    @(
        "DKC2Recomp test build $FolderName",
        "SnapshotSequence=$('{0:D2}' -f $Sequence)",
        "ProjectVersion=$ProjectVersion",
        "CreatedUtc=$([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "SourceBranch=$Branch",
        "SourceCommit=$Commit",
        "SourceWorkingTreeDirty=$($Dirty.ToString().ToLowerInvariant())",
        "CMakeGenerator=$CMakeGenerator",
        "BuildConfiguration=$BuildConfiguration",
        "DKC2RecompSha256=$PrimaryHash",
        "DKC2RecompSDLSha256=$PortableHash",
        "",
        "This package intentionally contains no ROM. Start DKC2Recomp.exe and",
        "select your legally obtained North American v1.0 ROM when prompted."
    ) | Set-Content -LiteralPath (Join-Path $Staging "VERSION.txt") -Encoding UTF8

    $ForbiddenExtensions = @(
        ".fig", ".rom", ".sfc", ".smc", ".swc", ".sav", ".srm",
        ".state", ".wram", ".vram", ".oam", ".ppm", ".bmp", ".png",
        ".wav", ".pcm", ".mp3", ".flac", ".dmp", ".log")
    $ForbiddenNames = @(
        "rom.cfg", "launcher.cfg", "config.ini", "keybinds.ini",
        "performance.log", "tier2_coverage.json")
    $Forbidden = @(Get-ChildItem -LiteralPath $Staging -Recurse -File |
        Where-Object {
            $ForbiddenExtensions -contains $_.Extension.ToLowerInvariant() -or
            $ForbiddenNames -contains $_.Name.ToLowerInvariant()
        })
    if ($Forbidden.Count -ne 0) {
        throw "Version contains forbidden private/runtime artifacts: $($Forbidden.FullName -join ', ')"
    }
    $ForbiddenDirectories = @(Get-ChildItem -LiteralPath $Staging -Recurse -Directory |
        Where-Object { $_.Name.ToLowerInvariant() -in @(
            "diagnostics", "generated", "private", "saves") })
    if ($ForbiddenDirectories.Count -ne 0) {
        throw "Version contains forbidden directories: $($ForbiddenDirectories.FullName -join ', ')"
    }
    $UnexpectedExecutables = @(Get-ChildItem -LiteralPath $Staging -Recurse -File -Filter *.exe |
        Where-Object { $_.Name -notin $Executables })
    if ($UnexpectedExecutables.Count -ne 0) {
        throw "Version contains unexpected executables: $($UnexpectedExecutables.FullName -join ', ')"
    }

    Move-Item -LiteralPath $Staging -Destination $Destination
} catch {
    if (Test-Path -LiteralPath $Staging) {
        Remove-Item -LiteralPath $Staging -Recurse -Force
    }
    throw
}

Write-Output "version_folder=$DestinationPath"
Write-Output "version_sequence=$Sequence"
Write-Output "version_files=$((Get-ChildItem -LiteralPath $Destination -Recurse -File).Count)"
