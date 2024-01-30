<#

.SYNOPSIS
This script installs or uninstalls various FNMP components.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER Install
    Specifies an FNMP component to install.

.PARAMETER Uninstall
    Attempts to uninstall all FNMP components.

.PARAMETER ArtifactsDir
    Supplies an optional directory containing FNMP component artifacts.
    Supply this if you are running this script outside of the FNMP repo.

.PARAMETER LogsDir
    Supplies an optional directory to output logs.

.PARAMETER ToolsDir
    Supplies an optional directory to tools (devcon.exe, dswdevice.exe).
    Supply this if you are running this script outside of the FNMP repo.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fnmp")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fnmp")]
    [string]$Uninstall = "",

    [Parameter(Mandatory = $false)]
    [string]$ArtifactsDir = "",

    [Parameter(Mandatory = $false)]
    [string]$LogsDir = "",

    [Parameter(Mandatory = $false)]
    [string]$ToolsDir = ""
)

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

$scripterror = $vardoesnotexist
$RootDir = Split-Path $PSScriptRoot -Parent

# Important paths.
if ([string]::IsNullOrEmpty($ArtifactsDir)) {
    . $RootDir\tools\common.ps1
    Generate-WinConfig $Arch $Config
    $ArtifactsDir = "$RootDir\artifacts\bin\$($WinArch)$($WinConfig)"
}
if ([string]::IsNullOrEmpty($LogsDir)) {
    $LogsDir = "$RootDir\artifacts\logs"
}
if ([string]::IsNullOrEmpty($ToolsDir)) {
    . $RootDir\tools\common.ps1
    $DevCon = Get-CoreNetCiArtifactPath -Name "devcon.exe"
    $DswDevice = Get-CoreNetCiArtifactPath -Name "dswdevice.exe"
} else {
    $DevCon = "$ToolsDir\devcon.exe"
    $DswDevice = "$ToolsDir\dswdevice.exe"
}

# File paths.
$FnMpSys = "$ArtifactsDir\fnmp\fnmp.sys"
$FnMpInf = "$ArtifactsDir\fnmp\fnmp.inf"
$FnMpComponentId = "ms_fnmp"
$FnMpDeviceId0 = "fnmp0"
$FnMpServiceName = "FNMP"

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

# Helper to capture failure diagnostics and trigger CI agent reboot
function Uninstall-Failure($FileName) {
    . $RootDir\tools\common.ps1
    Collect-LiveKD -OutFile $LogsDir\$FileName

    Write-Host "##vso[task.setvariable variable=NeedsReboot]true"
    Write-Error "Uninstall failed"
}

# Helper to start (with retry) a service.
function Start-Service-With-Retry($Name) {
    Write-Verbose "Start-Service $Name"
    $StartSuccess = $false
    for ($i=0; $i -lt 100; $i++) {
        try {
            Start-Sleep -Milliseconds 10
            Start-Service $Name
            $StartSuccess = $true
            break
        } catch { }
    }
    if ($StartSuccess -eq $false) {
        Write-Error "Failed to start $Name"
    }
}

# Helper to rename (with retry) a network adapter. On WS2022, renames sometimes
# fail with ERROR_TRANSACTION_NOT_ACTIVE.
function Rename-NetAdapter-With-Retry($IfDesc, $Name) {
    Write-Verbose "Rename-NetAdapter $IfDesc $Name"
    $RenameSuccess = $false
    for ($i=0; $i -lt 10; $i++) {
        try {
            Rename-NetAdapter -InterfaceDescription $IfDesc $Name
            $RenameSuccess = $true
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if ($RenameSuccess -eq $false) {
        Write-Error "Failed to rename $Name"
    }
}

# Helper wait for a service to stop and then delete it. Callers are responsible
# making sure the service is already stopped or stopping.
function Cleanup-Service($Name) {
    # Wait for the service to stop.
    $StopSuccess = $false
    try {
        for ($i = 0; $i -lt 100; $i++) {
            if (-not (Get-Service $Name -ErrorAction Ignore) -or
                (Get-Service $Name).Status -eq "Stopped") {
                $StopSuccess = $true
                break;
            }
            Start-Sleep -Milliseconds 100
        }
        if (!$StopSuccess) {
            Write-Verbose "$Name failed to stop"
        }
    } catch {
        Write-Verbose "Exception while waiting for $Name to stop"
    }

    # Delete the service.
    if (Get-Service $Name -ErrorAction Ignore) {
        try { sc.exe delete $Name > $null }
        catch { Write-Verbose "'sc.exe delete $Name' threw exception!" }

        # Wait for the service to be deleted.
        $DeleteSuccess = $false
        for ($i = 0; $i -lt 10; $i++) {
            if (-not (Get-Service $Name -ErrorAction Ignore)) {
                $DeleteSuccess = $true
                break;
            }
            Start-Sleep -Milliseconds 10
        }
        if (!$DeleteSuccess) {
            Write-Verbose "Failed to clean up $Name!"
            Uninstall-Failure "cleanup_service_$Name.dmp"
        }
    }
}

# Helper to wait for an adapter to start.
function Wait-For-Adapters($IfDesc, $Count=1, $WaitForUp=$true) {
    Write-Verbose "Waiting for $Count `"$IfDesc`" adapter(s) to start"
    $StartSuccess = $false
    for ($i = 0; $i -lt 100; $i++) {
        $Result = 0
        $Filter = { $_.InterfaceDescription -like "$IfDesc*" -and (!$WaitForUp -or $_.Status -eq "Up") }
        try { $Result = ((Get-NetAdapter | where $Filter) | Measure-Object).Count } catch {}
        if ($Result -eq $Count) {
            $StartSuccess = $true
            break;
        }
        Start-Sleep -Milliseconds 100
    }
    if ($StartSuccess -eq $false) {
        Get-NetAdapter | Format-Table | Out-String | Write-Verbose
        Write-Error "Failed to start $Count `"$IfDesc`" adapters(s) [$Result/$Count]"
    } else {
        Write-Verbose "Started $Count `"$IfDesc`" adapter(s)"
    }
}

# Helper to uninstall a driver from its inf file.
function Uninstall-Driver($Inf) {
    # Expected pnputil enum output is:
    #   Published Name: oem##.inf
    #   Original Name:  fnmp.inf
    #   ...
    $DriverList = pnputil.exe /enum-drivers
    $StagedDriver = ""
    foreach ($line in $DriverList) {
        if ($line -match "Published Name") {
            $StagedDriver = $($line -split ":")[1]
        }

        if ($line -match "Original Name") {
            $infName = $($line -split ":")[1]
            if ($infName -match $Inf) {
                break
            }

            $StagedDriver = ""
        }
    }

    if ($StagedDriver -eq "") {
        Write-Verbose "Couldn't find $Inf in driver list."
        return
    }

    cmd.exe /c "pnputil.exe /delete-driver $StagedDriver 2>&1" | Write-Verbose
    if (!$?) {
        Write-Verbose "pnputil.exe /delete-driver $Inf ($StagedDriver) exit code: $LastExitCode"
    }
}

# Installs the fnmp driver.
function Install-FnMp {
    if (!(Test-Path $FnMpSys)) {
        Write-Error "$FnMpSys does not exist!"
    }

    Write-Verbose "pnputil.exe /install /add-driver $FnMpInf"
    pnputil.exe /install /add-driver $FnMpInf | Write-Verbose
    if ($LastExitCode) {
        Write-Error "pnputil.exe exit code: $LastExitCode"
    }

    Write-Verbose "dswdevice.exe -i $FnMpDeviceId0 $FnMpComponentId"
    & $DswDevice -i $FnMpDeviceId0 $FnMpComponentId | Write-Verbose
    if ($LastExitCode) {
        Write-Error "dswdevice.exe exit code: $LastExitCode"
    }

    Wait-For-Adapters -IfDesc $FnMpServiceName

    Write-Verbose "Renaming adapters"
    Rename-NetAdapter-With-Retry FNMP FNMP

    Write-Verbose "Get-NetAdapter FNMP"
    Get-NetAdapter FNMP | Format-Table | Out-String | Write-Verbose

    Write-Verbose "Configure fnmp ipv4"
    netsh int ipv4 set int interface=fnmp dadtransmits=0 | Write-Verbose
    netsh int ipv4 add address name=fnmp address=192.168.200.1/24 | Write-Verbose
    netsh int ipv4 add neighbor fnmp address=192.168.200.2 neighbor=22-22-22-22-00-02 | Write-Verbose

    Write-Verbose "Configure fnmp ipv6"
    netsh int ipv6 set int interface=fnmp dadtransmits=0 | Write-Verbose
    netsh int ipv6 add address interface=fnmp address=fc00::200:1/112 | Write-Verbose
    netsh int ipv6 add neighbor fnmp address=fc00::200:2 neighbor=22-22-22-22-00-02 | Write-Verbose

    Write-Verbose "fnmp.sys install complete!"
}

# Uninstalls the fnmp driver.
function Uninstall-FnMp {
    netsh int ipv4 delete address fnmp 192.168.200.1 | Out-Null
    netsh int ipv4 delete neighbors fnmp | Out-Null
    netsh int ipv6 delete address fnmp fc00::200:1 | Out-Null
    netsh int ipv6 delete neighbors fnmp | Out-Null

    Write-Verbose "$DswDevice -u $FnMpDeviceId0"
    cmd.exe /c "$DswDevice -u $FnMpDeviceId0 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Deleting $FnMpDeviceId0 device failed: $LastExitCode"
    }

    Write-Verbose "$DevCon remove @SWD\$FnMpDeviceId0\$FnMpDeviceId0"
    cmd.exe /c "$DevCon remove @SWD\$FnMpDeviceId0\$FnMpDeviceId0 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Removing $FnMpDeviceId0 device failed: $LastExitCode"
    }

    Cleanup-Service $FnMpServiceName

    Uninstall-Driver "fnmp.inf"

    Write-Verbose "fnmp.sys uninstall complete!"
}

try {
    if ($Install -eq "fnmp") {
        Install-FnMp
    }

    if ($Uninstall -eq "fnmp") {
        Uninstall-FnMp
    }
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
