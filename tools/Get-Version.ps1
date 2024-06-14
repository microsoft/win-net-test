# .SYNOPSIS
# Get the version number from the repository.

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

Write-Output $(Get-ProjectBuildVersionString)
