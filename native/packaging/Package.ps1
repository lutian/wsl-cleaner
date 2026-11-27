<#
.SYNOPSIS
Build a native-only portable/installable distribution from a verified executable.
.DESCRIPTION
Refuses existing destinations. Does not install, start the app, enable startup, or download.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string] $Executable,
    [Parameter(Mandatory=$true)][string] $OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$exe=Get-Item -LiteralPath $Executable
if($exe -is [IO.DirectoryInfo] -or $exe.Extension -ine '.exe') { throw 'Expected a native executable.' }
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out) { throw 'Output directory already exists; refusing overwrite.' }
$zip=$out+'.zip'
if(Test-Path -LiteralPath $zip) { throw 'Archive already exists; refusing overwrite.' }
$null=New-Item -ItemType Directory -Path $out
Copy-Item -LiteralPath $exe.FullName -Destination (Join-Path $out 'WslCleaner.exe')
foreach($name in @('Install.ps1','Uninstall.ps1','Packaging.Common.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $out $name)
}
@'
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -ExecutablePath "%~dp0WslCleaner.exe" %*
if errorlevel 1 pause
'@ | Set-Content -LiteralPath (Join-Path $out 'Install.cmd') -Encoding ASCII
@'
WSL Cleaner native Windows application

Portable: double-click WslCleaner.exe. Close hides to tray; use Exit to quit.
Install: double-click Install.cmd (per user, no administrator required).
Optional startup: run Install.cmd -EnableStartup during first installation.
Uninstall: PowerShell -ExecutionPolicy Bypass -File Uninstall.ps1 in the installed folder.

No extra runtime installation is needed. Maintenance is opt-in. Budgets are advisory.
Do not run compaction until you understand its all-WSL/Docker shutdown and backup requirements.
Compaction requires explicit administrator launch and a backup on another fixed volume.
Ordinary stopped containers and volumes are not automatically deleted.
No guaranteed amount of recovered disk space is promised.

Extract this archive into a local Windows directory before installation.
Do not run the installer from a network/WSL UNC path. Existing installs are never overwritten.
Settings and backups are preserved during uninstall. See the source repository for details:
https://github.com/lutian/wsl-cleaner
'@ | Set-Content -LiteralPath (Join-Path $out 'README.txt') -Encoding ASCII
Get-ChildItem -LiteralPath $out -File | ForEach-Object {
    $hash=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ASCII
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
Write-Output "PACKAGE=$zip"
Write-Output ('ZIP_SHA256='+(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)
