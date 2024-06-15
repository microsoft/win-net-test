# .SYNOPSIS
# Get the version number from the repository.

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

try {
    Write-Output $(Get-ProjectBuildVersionString)
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
