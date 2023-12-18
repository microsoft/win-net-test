<#

.SYNOPSIS
This signs and packages the drivers.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
Generate-WinConfig $Arch $Config

function Get-WindowsKitTool {
    param (
        [string]$Arch = "x86",
        [Parameter(Mandatory = $true)]
        [string]$Tool
    )

    $KitBinRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (!(Test-Path $KitBinRoot)) {
        Write-Error "Windows Kit Binary Folder not Found"
        return $null
    }


    $Subfolders = Get-ChildItem -Path $KitBinRoot -Directory | Sort-Object -Descending
    foreach ($Subfolder in $Subfolders) {
        $ToolPath = Join-Path $Subfolder.FullName "$Arch\$Tool"
        if (Test-Path $ToolPath) {
            return $ToolPath
        }
    }

    Write-Error "Failed to find tool"
    return $null
}

# Tool paths.
$SignToolPath = Get-WindowsKitTool -Tool "signtool.exe"
if (!(Test-Path $SignToolPath)) { Write-Error "$SignToolPath does not exist!" }
$Inf2CatToolPath = Get-WindowsKitTool -Tool "inf2cat.exe"
if (!(Test-Path $Inf2CatToolPath)) { Write-Error "$Inf2CatToolPath does not exist!" }

# Artifact paths.
$RootDir = (Split-Path $PSScriptRoot -Parent)
$ArtifactsDir = Join-Path $RootDir "artifacts\bin\$($WinArch)$($WinConfig)"

# Certificate paths.
$CodeSignCertPath = Get-CoreNetCiArtifactPath -Name "CoreNetSignRoot.cer"
if (!(Test-Path $CodeSignCertPath)) { Write-Error "$CodeSignCertPath does not exist!" }
$CertPath = Get-CoreNetCiArtifactPath -Name "CoreNetSign.pfx"
if (!(Test-Path $CertPath)) { Write-Error "$CertPath does not exist!" }

# All the file paths.
$FnMpDir = Join-Path $ArtifactsDir "fnmp"
$FnMpSys = Join-Path $FnMpDir "fnmp.sys"
$FnMpInf = Join-Path $FnMpDir "fnmp.inf"
$FnMpCat = Join-Path $FnMpDir "fnmp.cat"

# Verify all the files are present.
if (!(Test-Path $FnMpSys)) { Write-Error "$FnMpSys does not exist!" }
if (!(Test-Path $FnMpInf)) { Write-Error "$FnMpInf does not exist!" }

# Sign the driver files.
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 $FnMpSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }

# Build up the catalogs.
& $Inf2CatToolPath /driver:$FnMpDir /os:10_x64
if ($LastExitCode) { Write-Error "inf2cat.exe exit code: $LastExitCode" }

# Sign the catalogs.
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 $FnMpCat
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }

# Copy the cert to the artifacts dir.
Copy-Item $CodeSignCertPath $ArtifactsDir
