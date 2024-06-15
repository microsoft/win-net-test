param (
    [string] $InputFile,
    [string] $OutputFile
)

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

try {
    $Version = &"$PSScriptRoot\Get-Version.ps1"
    $Content = Get-Content $InputFile
    $Content = $Content.Replace("{version}", $Version)
    Set-Content $OutputFile $Content
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
