#
# Build script.
#

param (
    [ValidateSet("x64", "arm64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Config = "Debug",

    [Parameter(Mandatory=$false)]
    [string]$Project = "",

    [Parameter(Mandatory = $false)]
    [switch]$NoClean = $false,

    [Parameter(Mandatory = $false)]
    [switch]$UpdateDeps = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent

$Tasks = @()
if ([string]::IsNullOrEmpty($Project)) {
    $Tasks += "Build"

    if (!$NoClean) {
        $Tasks = @("Clean") + $Tasks
    }
} else {
    $Clean = ""
    if (!$NoClean) {
        $Clean = ":Rebuild"
    }
    $Tasks += "$Project$Clean"
}

& $RootDir\tools\prepare-machine.ps1 -ForBuild -Force:$UpdateDeps

msbuild.exe $RootDir\wnt.sln `
    /t:restore `
    /p:RestorePackagesConfig=true `
    /p:Configuration=$Config `
    /p:Platform=$Platform
if (!$?) {
    Write-Error "Restoring NuGet packages failed: $LastExitCode"
}

# Unfortunately, global state cached by MsBuild.exe combined with WDK bugs
# causes unreliable builds. Specifically, the Telemetry task implemented by
# WDK's Microsoft.DriverKit.Build.Tasks.17.0.dll has breaking API changes
# that are not invalidated by loading different WDKs. Therefore we disable
# MsBuild.exe reuse with /nodeReuse:false.

msbuild.exe $RootDir\wnt.sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /p:SignMode=TestSign `
    /t:$($Tasks -join ",") `
    /nodeReuse:false `
    /maxCpuCount
if (!$?) {
    Write-Error "Build failed: $LastExitCode"
}
