#
# Assembles a dev kit for FNMP client development.
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

$Name = "fnmp-devkit-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\kit\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\fnmp.pdb"   $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\fnmpapi.pdb" $DstPath\symbols

New-Item -Path $DstPath\include -ItemType Directory > $null
copy -Recurse inc\* $DstPath\include

New-Item -Path $DstPath\lib -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\fnmpapi.lib" $DstPath\lib

$VersionString = Get-ProjectBuildVersionString

if (!(Is-ReleaseBuild)) {
    $VersionString += "-prerelease+" + (git.exe describe --long --always --dirty --exclude=* --abbrev=8)

    copy "artifacts\bin\$($Platform)_$($Config)\CoreNetSignRoot.cer" $DstPath\bin
}

Compress-Archive -DestinationPath "$DstPath\$Name-$VersionString.zip" -Path $DstPath\*
