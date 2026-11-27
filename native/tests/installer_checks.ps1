<#
.SYNOPSIS
Exercise native installation only inside a newly created Windows temp profile.
.DESCRIPTION
Does not enable startup, launch the app, elevate, or access WSL/Docker.
Keeps scratch on failure for diagnosis. Never targets the real installation directory.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string] $Executable,
    [Parameter(Mandatory=$true)][string] $PackagingDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('wsl-cleaner-install-check-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$package=Join-Path $root 'package'
$profile=Join-Path $root 'profile'
$null=New-Item -ItemType Directory -Path $package
$null=New-Item -ItemType Directory -Path $profile
Copy-Item -LiteralPath $Executable -Destination (Join-Path $package 'WslCleaner.exe')
foreach($name in @('Install.ps1','Uninstall.ps1','Packaging.Common.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PackagingDirectory $name) -Destination (Join-Path $package $name)
}
$previous=$env:LOCALAPPDATA
$passed=0
function Require([bool]$condition,[string]$message) { if(-not $condition){throw $message} }
try {
    $env:LOCALAPPDATA=$profile
    & (Join-Path $package 'Install.ps1') -ExecutablePath (Join-Path $package 'WslCleaner.exe')
    $installed=Join-Path $profile 'Programs\WslCleanerNative'
    $exe=Join-Path $installed 'WslCleaner.exe'
    $manifestPath=Join-Path $installed 'install-manifest.json'
    Require (Test-Path -LiteralPath $exe) 'Native executable not installed.'
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Require (-not $manifest.startup) 'Startup unexpectedly enabled.'
    $before=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    Require ($before -ceq (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash) 'Installed bytes changed.'
    $passed++; Write-Output 'PASS install in isolated profile; no startup; exact executable bytes'

    $rejected=$false
    try { & (Join-Path $package 'Install.ps1') -ExecutablePath (Join-Path $package 'WslCleaner.exe') }
    catch { $rejected=$_.Exception.Message -like '*Destination already exists; refusing overwrite*' }
    Require $rejected 'Repeated installation did not refuse the existing directory.'
    Require ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ceq $before) 'Rejected installation modified executable.'
    $passed++; Write-Output 'PASS rejected reinstall preserves installed bytes'

    $originalManifest=Get-Content -LiteralPath $manifestPath -Raw
    $manifest.product='UnrelatedProduct'
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $rejected=$false
    try { & (Join-Path $installed 'Uninstall.ps1') }
    catch { $rejected=$true }
    Require $rejected 'Uninstall accepted a foreign manifest.'
    Require ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ceq $before) 'Rejected uninstall changed executable.'
    Set-Content -LiteralPath $manifestPath -Value $originalManifest -Encoding UTF8
    $passed++; Write-Output 'PASS foreign manifest rejected without executable deletion'

    $state=Join-Path $profile 'WslCleaner'
    $null=New-Item -ItemType Directory -Path $state
    $settings=Join-Path $state 'settings-sentinel'
    $backup=Join-Path $state 'backup-sentinel'
    Set-Content -LiteralPath $settings -Value 'preserve-settings'
    Set-Content -LiteralPath $backup -Value 'preserve-backup'
    $unknown=Join-Path $installed 'unrelated-sentinel.txt'
    Set-Content -LiteralPath $unknown -Value 'preserve-unrelated'
    & (Join-Path $installed 'Uninstall.ps1')
    Require (-not (Test-Path -LiteralPath $exe)) 'Owned executable not removed.'
    Require ((Get-Content -LiteralPath $unknown -Raw).Trim() -ceq 'preserve-unrelated') 'Unrelated installation file lost.'
    Require ((Get-Content -LiteralPath $settings -Raw).Trim() -ceq 'preserve-settings') 'Settings lost.'
    Require ((Get-Content -LiteralPath $backup -Raw).Trim() -ceq 'preserve-backup') 'Backup lost.'
    $passed++; Write-Output 'PASS uninstall removes owned files and preserves unrelated files/settings/backups'
    Write-Output "$passed installer checks passed; scratch=$root"
} finally {
    $env:LOCALAPPDATA=$previous
}
