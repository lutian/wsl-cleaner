<#
.SYNOPSIS
Installs only the native executable and manifest-owned uninstaller for this user.
.DESCRIPTION
Refuses any existing destination. Does not launch the app, elevate, install runtimes,
or touch settings/backups. Startup is opt-in. No upgrade or overwrite mode.
.EXAMPLE
.\Install.ps1 -ExecutablePath .\WslCleaner.exe -EnableStartup
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $ExecutablePath,
    [switch] $EnableStartup
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Packaging.Common.ps1'
# Inspect the helper before loading code from it, including each parent.
$probe = Get-Item -LiteralPath $common -Force
while ($null -ne $probe) {
    if (($probe.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse package path refused.' }
    if ($probe -is [IO.DirectoryInfo]) { $probe = $probe.Parent } else { $probe = $probe.Directory }
}
. $common

$root = Get-NativeInstallRoot
Assert-NoReparsePath $root
Assert-NoReparsePath $ExecutablePath
$source = Get-Item -LiteralPath $ExecutablePath -Force
if ($source -is [IO.DirectoryInfo] -or $source.Extension -ine '.exe') { throw 'ExecutablePath must name the native .exe file.' }
foreach ($name in @('Uninstall.ps1', 'Packaging.Common.ps1')) {
    Assert-NoReparsePath (Join-Path $PSScriptRoot $name)
}
if (Test-Path -LiteralPath $root) { throw "Destination already exists; refusing overwrite: $root" }
$shortcut = $null
if ($EnableStartup) {
    $shortcut = Get-NativeStartupPath
    Assert-NoReparsePath $shortcut
    if (Test-Path -LiteralPath $shortcut) { throw 'Startup shortcut already exists; refusing overwrite.' }
}
# New-Item without Force also refuses an existing directory at creation time.
$null = New-Item -ItemType Directory -Path $root -ErrorAction Stop
Assert-NoReparsePath $root
Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $root 'WslCleaner.exe') -ErrorAction Stop
foreach ($name in @('Uninstall.ps1', 'Packaging.Common.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $root $name) -ErrorAction Stop
}
# Fixed ownership set; settings and backups deliberately never appear here.
$manifest = [ordered]@{ version = 1; product = 'WslCleanerNative'; root = $root; files = @(Get-OwnedNames); startup = [bool]$EnableStartup }
$manifestPath = Join-Path $root 'install-manifest.json'
Assert-NoReparsePath $manifestPath
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
if ($EnableStartup) {
    Assert-NoReparsePath $shortcut
    if (Test-Path -LiteralPath $shortcut) { throw 'Startup shortcut appeared during installation; refusing overwrite.' }
    $shell = New-Object -ComObject WScript.Shell
    try {
        $link = $shell.CreateShortcut($shortcut)
        $link.TargetPath = Join-Path $root 'WslCleaner.exe'
        $link.Arguments = '--tray'
        $link.WorkingDirectory = $root
        $link.Description = 'WSL Cleaner native advisory disk monitor'
        $link.Save()
    } finally {
        if ($null -ne $shell) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
    }
}
Write-Output "Installed native app at $root. Not launched. Settings/backups preserved."
