#
# Assembles a dev kit for FNMP/FNLWF client development.
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
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
Generate-WinConfig $Platform $Config

$Name = "fn-devkit-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\kit\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

New-Item -Path $DstPath\include -ItemType Directory > $null
copy -Recurse inc\* $DstPath\include

New-Item -Path $DstPath\lib -ItemType Directory > $null
copy "artifacts\bin\$($WinArch)$($WinConfig)\fnmpapi.lib" $DstPath\lib
copy "artifacts\bin\$($WinArch)$($WinConfig)\fnmpapi.pdb" $DstPath\lib
copy "artifacts\bin\$($WinArch)$($WinConfig)\fnlwfapi.lib" $DstPath\lib
copy "artifacts\bin\$($WinArch)$($WinConfig)\fnlwfapi.pdb" $DstPath\lib

Compress-Archive -DestinationPath "$DstPath\$Name.zip" -Path $DstPath\*
