<#
.SYNOPSIS
Parse/structural checks only; optionally exercise documented non-maintenance CLI modes.
.DESCRIPTION
Never invokes installers, inventory, the resident UI, Docker, WSL, or maintenance.
No temporary files or settings are created. -Executable opts into child processes
for --help, --version, invalid arguments, and --smoke-test only.
#>
[CmdletBinding()]
param([string] $Executable)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$native = Split-Path -Parent $PSScriptRoot
$script:passed = 0
$script:failed = 0
function Test-Rule([string] $Name, [scriptblock] $Body) {
    try { & $Body; $script:passed++; Write-Output "PASS $Name" }
    catch { $script:failed++; Write-Output "FAIL ${Name}: $($_.Exception.Message)" }
}
function Require([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}
$scripts = @('Install.ps1', 'Uninstall.ps1', 'Packaging.Common.ps1', 'Package.ps1')
foreach ($name in $scripts) {
    $path = Join-Path $native "packaging\$name"
    Test-Rule "PowerShell parser: $name" {
        $tokens = $null; $errors = $null
        $null = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
        Require ($errors.Count -eq 0) ($errors | Out-String)
    }
}
$install = Get-Content -LiteralPath (Join-Path $native 'packaging\Install.ps1') -Raw -Encoding UTF8
$uninstall = Get-Content -LiteralPath (Join-Path $native 'packaging\Uninstall.ps1') -Raw -Encoding UTF8
$common = Get-Content -LiteralPath (Join-Path $native 'packaging\Packaging.Common.ps1') -Raw -Encoding UTF8
$app = Get-Content -LiteralPath (Join-Path $native 'src\app.cpp') -Raw -Encoding UTF8
Test-Rule 'Installer refuses existing destinations and startup is explicit' {
    Require ($install.Contains('[switch] $EnableStartup')) 'Missing opt-in startup switch.'
    Require ($install.Contains('Destination already exists; refusing overwrite')) 'Missing destination refusal.'
    Require ($install.Contains('Startup shortcut already exists; refusing overwrite.')) 'Missing startup collision refusal.'
    Require ($install -notmatch '(?i)Start-Process|Invoke-Expression|DownloadString|DownloadFile|runas') 'Unexpected process/network/elevation operation.'
}
Test-Rule 'Fixed manifest ownership, no runtime payload' {
    foreach ($name in @('WslCleaner.exe', 'Uninstall.ps1', 'Packaging.Common.ps1', 'install-manifest.json')) {
        Require ($common.Contains("'$name'")) "Missing owned file: $name"
    }
    Require ($install.Contains('files = @(Get-OwnedNames)')) 'Installer does not reuse ownership helper.'
    Require ($uninstall.Contains('$expected = @(Get-OwnedNames)')) 'Uninstaller does not reuse ownership helper.'
    Require ($uninstall.Contains('Invalid ownership list; no files removed.')) 'Manifest ownership validation missing.'
}
Test-Rule 'Reparse guards and nonrecursive literal-only uninstall' {
    Require ($common.Contains('[IO.FileAttributes]::ReparsePoint')) 'Missing shared reparse guard.'
    foreach ($source in @($install, $uninstall)) {
        Require ($source.Contains('Reparse package path refused.')) 'Helper loading path unguarded.'
        Require ($source.Contains('Assert-NoReparsePath $root')) 'Root unguarded.'
    }
    $tokens = $null; $errors = $null
    $tree = [Management.Automation.Language.Parser]::ParseInput($uninstall, [ref]$tokens, [ref]$errors)
    $commands = $tree.FindAll({ param($n) $n -is [Management.Automation.Language.CommandAst] }, $true)
    foreach ($command in $commands) {
        if ($command.GetCommandName() -eq 'Remove-Item') {
            $text = $command.Extent.Text
            Require ($text -match '-LiteralPath') 'Removal without LiteralPath.'
            Require ($text -notmatch '-Recurse|-Force|\*') 'Broad removal operation.'
        }
    }
    Require ($uninstall.Contains('[IO.Directory]::Delete($root, $false)')) 'Directory deletion must be nonrecursive.'
    Require ($uninstall.Contains('Startup shortcut no longer belongs')) 'Missing startup ownership check.'
    Require ($uninstall.Contains('[IO.FileShare]::None')) 'Missing running-image fail-closed check.'
}
Test-Rule 'WSL apt plan uses distribution name, not budget path identity' {
    $prepare = [regex]::Match($app, '(?s)void Prepare\(wc::Action action\).*?(?=void ExecutePlan\()').Value
    Require (-not [string]::IsNullOrEmpty($prepare)) 'Cannot locate UI planning function.'
    Require (-not $prepare.Contains('std::wstring target=action==wc::Action::DockerCleanup?L"docker":SelectedDisk();')) 'WSL cleanup is routed a VHDX budget identity instead of registered distro name.'
    Require ($prepare.Contains('target=disk->name;')) 'WSL cleanup must use the registered name.'
    Require ($prepare.Contains('target=disk->path;')) 'Compaction must retain the selected VHDX path.'
}
Test-Rule 'WSL classification agrees between inventory and apt UI' {
    $core = Get-Content -LiteralPath (Join-Path $native 'src\core.cpp') -Raw -Encoding UTF8
    $producer = [regex]::Match($core, 'docker\s*\?\s*L"Docker"\s*:\s*L"([^"]+)"')
    $consumer = [regex]::Match($app, 'disk->kind\s*!=\s*L"([^"]+)"')
    Require ($producer.Success -and $consumer.Success) 'Cannot locate producer/consumer WSL classification.'
    Require ($producer.Groups[1].Value -ceq $consumer.Groups[1].Value) 'UI rejects the WSL classification emitted by inventory.'
}
Test-Rule 'Native UI uses shared API and explicit consent' {
    foreach ($needle in @('wWinMain', 'wc::SaveConfig', 'wc::ValidateConfig', '.Evaluate(', 'app.maintenance.Prepare(', 'app.maintenance.Execute(', 'L"CLEAN"', 'L"COMPACT"', 'Checked(Shutdown)', 'L"docker"')) {
        Require ($app.Contains($needle)) "Missing UI contract: $needle"
    }
    Require ($app -notmatch '#using|System::|WebView|ShellExecute') 'Unexpected managed/browser/auto-elevation dependency.'
    Require ($app.Contains('alocação física e uso no convidado desconhecidos')) 'Measurement uncertainty undisclosed.'
}
Test-Rule 'Background work, lifetime and automation guards are explicit' {
    foreach ($needle in @('std::thread', 'PostMessageW(window,Finished', 'app.worker.join()', 'if(app.busy || app.closing) return;', 'app.auto_attempted=true; app.auto_last=now;', 'std::max(config.cooldown_seconds,60U)', 'automation && config.auto_cleanup && docker_alert', 'if(!app.valid)', 'ERROR_ALREADY_EXISTS', 'GetTokenInformation')) {
        Require ($app.Contains($needle)) "Missing guard: $needle"
    }
    Require ($app.Contains('O arquivo existente não será sobrescrito automaticamente.')) 'Missing corrupt-store disclosure.'
}
Test-Rule 'Public smoke path excludes resident initialization' {
    Require ($app.Contains('app.smoke=option==L"--smoke-test"')) 'Smoke mode not public.'
    Require ($app.Contains('if(!app.smoke)')) 'Smoke settings/mutex guard missing.'
    Require ($app.Contains('if(app.smoke) { HiddenPanelChecks(); Print(')) 'Smoke must validate hidden controls and finish before resident branch.'
    Require ($app.Contains('no tray, polling or maintenance.')) 'Smoke result missing.'
}
if (-not [string]::IsNullOrEmpty($Executable)) {
    $exe = (Get-Item -LiteralPath $Executable).FullName
    function Run-Public([string] $Argument) {
        $info = New-Object Diagnostics.ProcessStartInfo
        $info.FileName = $exe; $info.Arguments = $Argument
        $info.UseShellExecute = $false; $info.CreateNoWindow = $true
        $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
        $process = New-Object Diagnostics.Process
        $process.StartInfo = $info
        try {
            Require ($process.Start()) 'Process did not start.'
            # Both streams read asynchronously; never block one behind the other.
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
            if (-not $process.WaitForExit(30000)) { throw 'Public mode did not exit within 30 seconds; child deliberately not terminated.' }
            return @{ Code = $process.ExitCode; Out = $stdout.Result; Err = $stderr.Result }
        } finally { $process.Dispose() }
    }
    Test-Rule 'Public help' {
        $r = Run-Public '--help'
        Require ($r.Code -eq 0 -and $r.Err -ceq '' -and $r.Out.Contains('--smoke-test') -and $r.Out.Contains('No flags bypass maintenance consent.')) 'Help contract failed.'
    }
    Test-Rule 'Public version' {
        $r = Run-Public '--version'
        Require ($r.Code -eq 0 -and $r.Err -ceq '' -and $r.Out -ceq "WSL Cleaner native 1`r`n") 'Version contract failed.'
    }
    Test-Rule 'Unknown argument rejected' {
        $r = Run-Public '--unknown'
        Require ($r.Code -eq 2 -and $r.Err -ceq '' -and $r.Out -ceq "Unknown arguments. Use --help.`r`n") 'Argument rejection contract failed.'
    }
    Test-Rule 'Public native controls smoke' {
        $r = Run-Public '--smoke-test'
        Require ($r.Code -eq 0 -and $r.Err -ceq '' -and $r.Out -ceq "SMOKE OK: native controls initialized; no tray, polling or maintenance.`r`n") 'Native controls smoke contract failed.'
    }
} else { Write-Output 'Native executable checks not requested; Windows smoke verification remains pending.' }
Write-Output "$script:passed passed; $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
exit 0
