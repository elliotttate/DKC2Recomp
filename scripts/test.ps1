param(
    [Parameter(Mandatory = $false)]
    [string]$Rom = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $ProjectRoot "build"
$Arguments = @("-S", $ProjectRoot, "-B", $BuildDirectory)
$CMakeCommand = Get-Command cmake -ErrorAction SilentlyContinue

if ($CMakeCommand) {
    $CMakePath = $CMakeCommand.Source
} else {
    $CMakePath = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (-not (Test-Path -LiteralPath $CMakePath)) {
        throw "CMake was not found on PATH or in Visual Studio 2022 Community."
    }
}
$CTestPath = Join-Path (Split-Path -Parent $CMakePath) "ctest.exe"

if ($Rom) {
    $Arguments += "-DDKC2_ROM=$Rom"
}

& $CMakePath @Arguments
& $CMakePath --build $BuildDirectory --config Release
& $CTestPath --test-dir $BuildDirectory -C Release --output-on-failure
