[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [string]$Msys2Root = 'C:\msys64',
    [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
if ($Jobs -lt 1) { throw 'Jobs must be at least 1.' }
$sourceDirectory = (Resolve-Path -LiteralPath $SourcePath).Path
$mingwDirectory = Join-Path $Msys2Root 'mingw64'
$toolDirectory = Join-Path $mingwDirectory 'bin'
$qtDirectory = Join-Path $mingwDirectory 'qt5-static'
foreach ($required in @('cmake.exe', 'ninja.exe', 'gcc.exe', 'g++.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $toolDirectory $required))) {
        throw "Missing MSYS2 MinGW64 tool: $required. See README.md for dependencies."
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $qtDirectory 'lib/cmake/Qt5'))) {
    throw 'Missing MSYS2 MinGW64 static Qt5. See README.md for dependencies.'
}
$originalPath = $env:PATH
try {
    $env:PATH = "$toolDirectory;$originalPath"
    $buildDirectory = Join-Path $sourceDirectory 'build-pc98'
    & (Join-Path $toolDirectory 'cmake.exe') -S $sourceDirectory -B $buildDirectory -G Ninja `
        -DCMAKE_BUILD_TYPE=Release -DRELEASE=ON -DUSE_QT6=OFF -DSTATIC_BUILD=ON `
        "-DCMAKE_PREFIX_PATH=$qtDirectory" "-DCMAKE_C_COMPILER=$(Join-Path $toolDirectory 'gcc.exe')" `
        "-DCMAKE_CXX_COMPILER=$(Join-Path $toolDirectory 'g++.exe')"
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }
    & (Join-Path $toolDirectory 'cmake.exe') --build $buildDirectory --target 86Box --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE). Source remains installed." }
    Write-Host "Built: $(Join-Path $buildDirectory 'src/86Box.exe')"
} finally { $env:PATH = $originalPath }
