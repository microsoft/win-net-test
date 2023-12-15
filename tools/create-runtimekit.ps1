#
# Assembles a runtime kit for FNMP execution.
# Code must be built before running this script.
#

param (
    [ValidateSet("x64")]
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

$Name = "fnmp-runtime-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "build\kit\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

# Fix up arch and config to match build conventions.
$WinArch = $Platform
$WinConfig = $Config
if ($Platform -eq "x64") { $WinArch = "amd64" }
else                     { $WinArch = "arm64" }
if ($Config -eq "Debug") { $WinConfig = "chk" }
else                     { $WinConfig = "fre" }

New-Item -Path $DstPath\bin -ItemType Directory > $null
copy -Recurse "build\bin\$($WinArch)$($WinConfig)\fnmp\" $DstPath\bin

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "build\bin\$($WinArch)$($WinConfig)\fnmp.pdb"   $DstPath\symbols

New-Item -Path $DstPath\tools -ItemType Directory > $null
copy ".\tools\common.ps1" $DstPath\tools
copy ".\tools\prepare-machine.ps1" $DstPath\tools
copy ".\tools\setup.ps1" $DstPath\tools

Compress-Archive -DestinationPath "$DstPath\$Name.zip" -Path $DstPath\*
