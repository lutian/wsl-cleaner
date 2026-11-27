<#
.SYNOPSIS
Removes only this per-user native installation's fixed manifest-owned files.
.DESCRIPTION
Exit the app first. No process termination, elevation, recursive deletion, settings
removal, backup removal or wildcard deletion. Unknown files prevent folder removal.
#>
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Packaging.Common.ps1'
$probe = Get-Item -LiteralPath $common -Force
while ($null -ne $probe) {
    if (($probe.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse package path refused.' }
    if ($probe -is [IO.DirectoryInfo]) { $probe = $probe.Parent } else { $probe = $probe.Directory }
}
. $common
$root = Get-NativeInstallRoot
if ([IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\') -ine $root.TrimEnd('\')) {
    throw 'Run the installed uninstaller from its original per-user directory.'
}
Assert-NoReparsePath $root
$manifestPath = Join-Path $root 'install-manifest.json'
Assert-NoReparsePath $manifestPath
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$expected = @(Get-OwnedNames)
if ($manifest.version -ne 1 -or $manifest.product -cne 'WslCleanerNative' -or
    $manifest.root -ine $root -or $manifest.startup -isnot [bool] -or
    @($manifest.files).Count -ne $expected.Count) { throw 'Invalid installation manifest; no files removed.' }
foreach ($name in $expected) {
    if (@($manifest.files | Where-Object { $_ -ceq $name }).Count -ne 1) { throw 'Invalid ownership list; no files removed.' }
    Assert-NoReparsePath (Join-Path $root $name)
    if (-not (Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf)) { throw 'Incomplete installation; no files removed.' }
}
# Open the executable exclusively before any deletion, so a running image fails closed.
$exe = Join-Path $root 'WslCleaner.exe'
$handle = [IO.File]::Open($exe, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
$handle.Dispose()
$shortcut = $null
if ($manifest.startup) {
    $shortcut = Get-NativeStartupPath
    Assert-NoReparsePath $shortcut
    if (Test-Path -LiteralPath $shortcut) {
        $shell = New-Object -ComObject WScript.Shell
        try {
            $link = $shell.CreateShortcut($shortcut)
            if ($link.TargetPath -ine $exe -or $link.Arguments -cne '--tray') {
                throw 'Startup shortcut no longer belongs to this installation; no files removed.'
            }
        } finally {
            if ($null -ne $shell) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
        }
    }
}
if ($null -ne $shortcut -and (Test-Path -LiteralPath $shortcut)) {
    Assert-NoReparsePath $shortcut
    Remove-Item -LiteralPath $shortcut -ErrorAction Stop
}
# Literal fixed filenames only, with the manifest last. Never recurse or force-delete.
foreach ($name in @('WslCleaner.exe', 'Uninstall.ps1', 'Packaging.Common.ps1', 'install-manifest.json')) {
    $path = Join-Path $root $name
    Assert-NoReparsePath $path
    Remove-Item -LiteralPath $path -ErrorAction Stop
}
Assert-NoReparsePath $root
if (@(Get-ChildItem -LiteralPath $root -Force).Count -eq 0) {
    # Directory.Delete(false) cannot recursively remove newly arrived foreign files.
    [IO.Directory]::Delete($root, $false)
} else { Write-Warning 'Unknown files retained; installation folder was not removed.' }
Write-Output 'Native installation removed. Settings and backups were not touched.'
