#
# Preparation script that runs before a OneBranch build.
#
# The preparation done in this script involves copying public include files to
# the vpack directories so they get packaged in the vpack.
#

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
$BinDir = Join-Path $RootDir "artifacts\bin"
$IncludeDir = Join-Path $RootDir "inc"

$Configs = @("chk", "fre")
$Platforms = @("amd64", "arm64")

foreach ($Config in $Configs) {
    foreach ($Platform in $Platforms) {
        $VpackDir = Join-Path $BinDir "$($Platform)$($Config)"
        New-Item -Path $VpackDir -ItemType Directory -Force | Out-Null
        Copy-Item $IncludeDir\* $VpackDir -Force | Out-Null
        Copy-Item $RootDir\tools\setup.ps1 $VpackDir -Force | Out-Null
    }
}
