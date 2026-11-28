[CmdletBinding()]
param(
    [string]$SourcePath,
    [string]$CloneTo,
    [string]$Ref,
    [switch]$CheckOnly,
    [switch]$Uninstall,
    [switch]$Build,
    [string]$Msys2Root = 'C:\msys64'
)
$ErrorActionPreference = 'Stop'
if ($SourcePath -and $CloneTo) { throw 'Choose SourcePath or CloneTo.' }
if ($CheckOnly -and $Build) { throw 'CheckOnly cannot build.' }
$pythonCommand = Get-Command py, python, python3 -ErrorAction SilentlyContinue |
    Where-Object { $_.Source -notmatch '[\\/]WindowsApps[\\/]' } | Select-Object -First 1
if (-not $pythonCommand) { throw 'Install Python 3.10+ and Git, then run this installer again.' }
$pythonArgs = @()
if ($pythonCommand.Name -eq 'py.exe' -or $pythonCommand.Name -eq 'py') { $pythonArgs += '-3' }
$pythonArgs += (Join-Path $PSScriptRoot 'install.py')
if ($SourcePath) { $pythonArgs += @('--source', $SourcePath) }
else {
    if (-not $CloneTo) { $CloneTo = Join-Path (Get-Location) '86Box-PC98' }
    $pythonArgs += @('--clone', $CloneTo)
}
if ($Ref) { $pythonArgs += @('--ref', $Ref) }
if ($CheckOnly) { $pythonArgs += '--check' }
if ($Uninstall) { $pythonArgs += '--uninstall' }
& $pythonCommand.Source @pythonArgs
if ($LASTEXITCODE -ne 0) { throw "Source installation failed (exit $LASTEXITCODE)." }
if ($Build -and -not $Uninstall) {
    if (-not $SourcePath) { $SourcePath = $CloneTo }
    & (Join-Path $PSScriptRoot 'Build-PC98.ps1') -SourcePath $SourcePath -Msys2Root $Msys2Root
}
