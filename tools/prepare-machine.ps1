<#

.SYNOPSIS
This prepares a machine for running FNMP/FNLWF.

.PARAMETER ForBuild
    Installs all the build-time dependencies.

.PARAMETER ForTest
    Installs all the run-time dependencies.

.PARAMETER ForFunctionalTest
    Installs all the run-time dependencies and configures machine for
    functional tests.

.PARAMETER ForLogging
    Installs all the logging dependencies.

.PARAMETER NoReboot
    Does not reboot the machine.

.PARAMETER Force
    Forces the installation of the latest dependencies.

#>

param (
    [Parameter(Mandatory = $false)]
    [switch]$ForBuild = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForTest = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForFunctionalTest = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForLogging = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoReboot = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Force = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Cleanup = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = "$RootDir\artifacts"

if (!$ForBuild -and !$ForTest -and !$ForFunctionalTest -and !$ForLogging) {
    Write-Error 'Must one of -ForBuild, -ForTest, -ForFunctionalTest, or -ForLogging'
}

# Flag that indicates something required a reboot.
$Reboot = $false

function Download-CoreNet-Deps {
    $CoreNetCiCommit = Get-CoreNetCiCommit

    # Download and extract https://github.com/microsoft/corenet-ci.
    if (!(Test-Path $ArtifactsDir)) { mkdir $ArtifactsDir }
    if ($Force -and (Test-Path "$ArtifactsDir/corenet-ci-$CoreNetCiCommit")) {
        Remove-Item -Recurse -Force "$ArtifactsDir/corenet-ci-$CoreNetCiCommit"
    }
    if (!(Test-Path "$ArtifactsDir/corenet-ci-$CoreNetCiCommit")) {
        Remove-Item -Recurse -Force "$ArtifactsDir/corenet-ci-*"
        Invoke-WebRequest-WithRetry -Uri "https://github.com/microsoft/corenet-ci/archive/$CoreNetCiCommit.zip" -OutFile "$ArtifactsDir\corenet-ci.zip"
        Expand-Archive -Path "$ArtifactsDir\corenet-ci.zip" -DestinationPath $ArtifactsDir -Force
        Remove-Item -Path "$ArtifactsDir\corenet-ci.zip"
    }
}

function Setup-TestSigning {
    # Check to see if test signing is enabled.
    $HasTestSigning = $false
    try { $HasTestSigning = ("$(bcdedit)" | Select-String -Pattern "testsigning\s+Yes").Matches.Success } catch { }

    # Enable test signing as necessary.
    if (!$HasTestSigning) {
        # Enable test signing.
        Write-Host "Enabling Test Signing. Reboot required!"
        bcdedit /set testsigning on | Write-Verbose
        $Script:Reboot = $true
    }
}

# Installs the FNMP/FNLWF certificates.
function Install-Certs {
    $CodeSignCertPath = Get-CoreNetCiArtifactPath -Name "CoreNetSignRoot.cer"
    if (!(Test-Path $CodeSignCertPath)) {
        Write-Error "$CodeSignCertPath does not exist!"
    }
    CertUtil.exe -f -addstore Root $CodeSignCertPath | Write-Verbose
    CertUtil.exe -f -addstore trustedpublisher $CodeSignCertPath | Write-Verbose
}

# Uninstalls the FNMP/FNLWF certificates.
function Uninstall-Certs {
    try { CertUtil.exe -delstore Root "CoreNetTestSigning" } catch { }
    try { CertUtil.exe -delstore trustedpublisher "CoreNetTestSigning" } catch { }
}

function Setup-VcRuntime {
    $Installed = $false
    try { $Installed = Get-ChildItem -Path Registry::HKEY_CLASSES_ROOT\Installer\Dependencies | Where-Object { $_.Name -like "*VC,redist*" } } catch {}

    if (!$Installed -or $Force) {
        Write-Host "Installing VC++ runtime"

        if (!(Test-Path $ArtifactsDir)) { mkdir artifacts }
        Remove-Item -Force "$ArtifactsDir\vc_redist.x64.exe" -ErrorAction Ignore

        # Download and install.
        Invoke-WebRequest-WithRetry -Uri "https://aka.ms/vs/16/release/vc_redist.x64.exe" -OutFile "$ArtifactsDir\vc_redist.x64.exe"
        & $ArtifactsDir\vc_redist.x64.exe /install /passive | Write-Verbose
    }
}

function Setup-VsTest {
    if (!(Get-VsTestPath) -or $Force) {
        Write-Host "Installing VsTest"

        if (!(Test-Path $ArtifactsDir)) { mkdir $ArtifactsDir }
        Remove-Item -Recurse -Force "$ArtifactsDir\Microsoft.TestPlatform" -ErrorAction Ignore

        # Download and extract.
        Invoke-WebRequest-WithRetry -Uri "https://www.nuget.org/api/v2/package/Microsoft.TestPlatform/16.11.0" -OutFile "$ArtifactsDir\Microsoft.TestPlatform.zip"
        Expand-Archive -Path "$ArtifactsDir\Microsoft.TestPlatform.zip" -DestinationPath "$ArtifactsDir\Microsoft.TestPlatform" -Force
        Remove-Item -Path "$ArtifactsDir\Microsoft.TestPlatform.zip"

        # Add to PATH.
        $RootDir = Split-Path $PSScriptRoot -Parent
        $Path = [Environment]::GetEnvironmentVariable("Path", "Machine")
        $Path += ";$(Get-VsTestPath)"
        [Environment]::SetEnvironmentVariable("Path", $Path, "Machine")
        $Env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
    }
}

if ($Cleanup) {
    if ($ForTest) {
        Uninstall-Certs
    }
} else {
    if ($ForBuild) {
        Download-CoreNet-Deps
    }

    if ($ForFunctionalTest) {
        $ForTest = $true
        # The NDIS verifier is required, otherwise allocations NDIS makes on
        # behalf of FNMP/FNLWF (e.g. NBLs) will not be verified.
        Write-Verbose "verifier.exe /standard /driver fnmp.sys fnlwf.sys ndis.sys fnfunctionaltestdrv.sys isrdrv.sys fnsock_km.sys"
        verifier.exe /standard /driver fnmp.sys fnlwf.sys ndis.sys fnfunctionaltestdrv.sys isrdrv.sys fnsock_km.sys | Write-Verbose
        if (!$?) {
            $Reboot = $true
        }

        if ((Get-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl).CrashDumpEnabled -ne 1) {
            # Enable complete (kernel + user) system crash dumps
            Write-Verbose "reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f"
            reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f
            $Reboot = $true
        }

        Setup-VcRuntime
        Setup-VsTest
    }

    if ($ForTest) {
        Download-CoreNet-Deps
        Setup-TestSigning
        Install-Certs
    }

    if ($ForLogging) {
        Download-CoreNet-Deps
    }
}

if ($Reboot -and !$NoReboot) {
    # Reboot the machine.
    Write-Host "Rebooting..."
    shutdown.exe /f /r /t 0
} elseif ($Reboot) {
    Write-Verbose "Reboot required."
    return @{"RebootRequired" = $true}
} else {
    return $null
}
