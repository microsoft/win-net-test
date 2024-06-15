# .SYNOPSIS
# Get the OS build configuration from the VS build configuration.

param(
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

try {
    Generate-WinConfig -Arch $Arch -Config $Config
    Write-Output "$($WinArch)$($WinConfig)"
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
