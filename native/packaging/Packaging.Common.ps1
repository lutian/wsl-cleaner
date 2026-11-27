Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-NativeInstallRoot {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { throw 'LOCALAPPDATA is unavailable.' }
    return [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\WslCleanerNative'))
}

function Assert-NoReparsePath([string] $Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.StartsWith('\\')) { throw 'Network paths are not supported.' }
    $part = [IO.Path]::GetPathRoot($full)
    foreach ($segment in $full.Substring($part.Length).Split([char[]]'\/', [StringSplitOptions]::RemoveEmptyEntries)) {
        $part = Join-Path $part $segment
        if (Test-Path -LiteralPath $part) {
            $item = Get-Item -LiteralPath $part -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse point refused: $part"
            }
        } elseif ([IO.File]::Exists($part) -or [IO.Directory]::Exists($part)) {
            throw "Cannot inspect path: $part"
        }
    }
}

function Get-OwnedNames {
    return @('WslCleaner.exe', 'Uninstall.ps1', 'Packaging.Common.ps1', 'install-manifest.json')
}

function Get-NativeStartupPath {
    $startup = [Environment]::GetFolderPath([Environment+SpecialFolder]::Startup)
    if ([string]::IsNullOrWhiteSpace($startup)) { throw 'Per-user Startup folder is unavailable.' }
    return Join-Path $startup 'WslCleanerNative.lnk'
}
