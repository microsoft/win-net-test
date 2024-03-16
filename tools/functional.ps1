<#

.SYNOPSIS
This script runs the FNMP/FNLWF functional tests.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER TestCaseFilter
    The test case filter passed to VSTest.

.PARAMETER ListTestCases
    Lists all available test cases.

.PARAMETER Iterations
    The number of times to run the test suite.

.PARAMETER Timeout
    Timeout in minutes. If multiple iterations are specified, the timeout provided is divided by the
    iterations and a watchdog is armed for each iteration.

.PARAMETER TestBinaryPath
    Path to the functional test binary (the binary passed to VSTest).

.PARAMETER KernelMode
    Run kernel mode tests.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [string]$TestCaseFilter = "",

    [Parameter(Mandatory = $false)]
    [switch]$ListTestCases = $false,

    [Parameter(Mandatory = $false)]
    [int]$Iterations = 1,

    [Parameter(Mandatory = $false)]
    [int]$Timeout = 0,

    [Parameter(Mandatory = $false)]
    [string]$TestBinaryPath = "",

    [Parameter(Mandatory = $false)]
    [switch]$KernelMode = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

function CleanupKernelMode {
    sc.exe stop fnfunctionaltestdrv | Write-Verbose
    sc.exe delete fnfunctionaltestdrv | Write-Verbose
    Remove-Item -Path "$SystemDriversPath\fnfunctionaltestdrv.sys" -ErrorAction SilentlyContinue
    & "$RootDir\tools\setup.ps1" -Uninstall invokesystemrelay -Config $Config -Arch $Arch -ErrorAction 'Continue'
    [Environment]::SetEnvironmentVariable("fnfunctionaltests::KernelModeEnabled", $null)
    [Environment]::SetEnvironmentVariable("fnfunctionaltests::KernelModeDriverPath", $null)
}

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
Generate-WinConfig $Arch $Config
$ArtifactsDir = "$RootDir\artifacts\bin\$($WinArch)$($WinConfig)"
$LogsDir = "$RootDir\artifacts\logs"
$IterationFailureCount = 0
$IterationTimeout = 0
$SystemDriversPath = Join-Path $([Environment]::GetEnvironmentVariable("SystemRoot")) "System32\drivers"

$VsTestPath = Get-VsTestPath
if ($VsTestPath -eq $null) {
    Write-Error "Could not find VSTest path"
}

if ($Timeout -gt 0) {
    $WatchdogReservedMinutes = 2
    $IterationTimeout = $Timeout / $Iterations - $WatchdogReservedMinutes

    if ($IterationTimeout -le 0) {
        Write-Error "Timeout must allow at least $WatchdogReservedMinutes minutes per iteration"
    }
}

if ($KernelMode) {
    # Ensure clean slate.
    CleanupKernelMode
    [System.Environment]::SetEnvironmentVariable('fnfunctionaltests::KernelModeEnabled', '1')
    [System.Environment]::SetEnvironmentVariable('fnfunctionaltests::KernelModeDriverPath', "$SystemDriversPath\")
    Copy-Item -Path "$ArtifactsDir\fnfunctionaltestdrv.sys" $SystemDriversPath
}

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

for ($i = 1; $i -le $Iterations; $i++) {
    try {
        $Watchdog = $null
        $LogName = "fnfunc"
        if ($Iterations -gt 1) {
            $LogName += "-$i"
        }

        & "$RootDir\tools\log.ps1" -Start -Name $LogName -Profile FnFunctional.Verbose -Config $Config -Arch $Arch

        if ($KernelMode) {
            Write-Verbose "installing invokesystemrelay..."
            & "$RootDir\tools\setup.ps1" -Install invokesystemrelay -Config $Config -Arch $Arch
            Write-Verbose "installed invokesystemrelay."
        }

        Write-Verbose "installing fnmp..."
        & "$RootDir\tools\setup.ps1" -Install fnmp -Config $Config -Arch $Arch
        Write-Verbose "installed fnmp."

        Write-Verbose "installing fnlwf..."
        & "$RootDir\tools\setup.ps1" -Install fnlwf -Config $Config -Arch $Arch
        Write-Verbose "installed fnlwf."

        $TestArgs = @()
        if (![string]::IsNullOrEmpty($TestBinaryPath)) {
            $TestArgs += $TestBinaryPath
        } else {
            $TestArgs += "$ArtifactsDir\fnfunctionaltests.dll"
        }
        if (![string]::IsNullOrEmpty($TestCaseFilter)) {
            $TestArgs += "/TestCaseFilter:$TestCaseFilter"
        }
        if ($ListTestCases) {
            $TestArgs += "/lt"
        }
        $TestArgs += "/logger:trx"
        $TestArgs += "/ResultsDirectory:$LogsDir"

        if ($IterationTimeout -gt 0) {
            $Watchdog = Start-Job -ScriptBlock {
                Start-Sleep -Seconds (60 * $Using:IterationTimeout)

                . $Using:RootDir\tools\common.ps1
                Collect-LiveKD -OutFile "$Using:LogsDir\$Using:LogName-livekd.dmp"
                Collect-ProcessDump -ProcessName "testhost.exe" -OutFile "$Using:LogsDir\$Using:LogName-testhost.dmp"
                Stop-Process -Name "vstest.console" -Force
            }
        }

        Write-Verbose "$VsTestPath\vstest.console.exe $TestArgs"
        & $VsTestPath\vstest.console.exe $TestArgs

        if ($LastExitCode -ne 0) {
            Write-Error "[$i/$Iterations] fnfunctionaltests failed with $LastExitCode" -ErrorAction Continue
            $IterationFailureCount++
        }
    } finally {
        if ($Watchdog -ne $null) {
            Remove-Job -Job $Watchdog -Force
        }
        & "$RootDir\tools\setup.ps1" -Uninstall fnlwf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall fnmp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        if ($KernelMode) {
            CleanupKernelMode
        }
        & "$RootDir\tools\log.ps1" -Stop -Name $LogName -Config $Config -Arch $Arch -ErrorAction 'Continue'
    }
}

if ($IterationFailureCount -gt 0) {
    Write-Error "$IterationFailureCount of $Iterations test iterations failed"
}
