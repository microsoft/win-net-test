#
# Assembles a nuget package for development with win-net-test tools.
# Code must be built before running this script.
#

param (
    [ValidateSet("x64", "arm64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Config = "Debug"
)

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
Generate-WinConfig -Arch $Platform -Config $Config

try {
    $Version = Get-ProjectBuildVersionString
    $WinArchConfig = "$WinArch" + "$WinConfig"

    $DstPath = "artifacts\pkg\$WinArchConfig"
    New-Item -ItemType Directory -Path $DstPath -ErrorAction Ignore

    $Content = Get-Content "tools\nuget\win-net-test.nuspec.in"
    $Content = $Content.Replace("{version}", $Version)
    $Content = $Content.Replace("{winarchconfig}", $WinArchConfig)
    Set-Content $DstPath\win-net-test.nuspec $Content

    nuget.exe pack $DstPath\win-net-test.nuspec -OutputDirectory $DstPath
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
