#
# Assembles a runtime kit for FNMP/FNLWF execution.
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

$Name = "fn-runtime-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\kit\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

New-Item -Path $DstPath\bin -ItemType Directory > $null
New-Item -Path $DstPath\bin\isr -ItemType Directory > $null
New-Item -Path $DstPath\bin\fnsock -ItemType Directory > $null
copy -Recurse "artifacts\bin\$($Platform)_$($Config)\fnmp\" $DstPath\bin
copy -Recurse "artifacts\bin\$($Platform)_$($Config)\fnlwf\" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Config)\isrdrv.sys" $DstPath\bin\isr
copy "artifacts\bin\$($Platform)_$($Config)\isrsvc.exe" $DstPath\bin\isr
copy "artifacts\bin\$($Platform)_$($Config)\fnsock_um.dll" $DstPath\bin\fnsock
copy "artifacts\bin\$($Platform)_$($Config)\fnsock_km.sys" $DstPath\bin\fnsock

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\fnmp.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\fnlwf.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\isrdrv.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\isrsvc.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\fnsock_um.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\fnsock_km.pdb" $DstPath\symbols

New-Item -Path $DstPath\tools -ItemType Directory > $null
copy ".\tools\common.ps1" $DstPath\tools
copy ".\tools\prepare-machine.ps1" $DstPath\tools
copy ".\tools\setup.ps1" $DstPath\tools

Compress-Archive -DestinationPath "$DstPath\$Name.zip" -Path $DstPath\*
