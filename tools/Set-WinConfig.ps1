param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [string] $InputFile,
    [string] $OutputFile
)

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

try {
    $WinConfig = &"$PSScriptRoot\Get-WinConfig.ps1" -Config $Config -Arch $Arch
    $Content = Get-Content $InputFile
    $Content = $Content.Replace("{winconfig}", $WinConfig)
    Set-Content $OutputFile $Content
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
