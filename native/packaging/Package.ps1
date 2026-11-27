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
WSL Cleaner — aplicativo nativo para Windows

Sem instalar: abra WslCleaner.exe. Fechar no X oculta na bandeja; use Sair para encerrar.
Para instalar: abra Install.cmd (instalação por usuário, sem exigir administrador).
Inicialização opcional: execute Install.cmd -EnableStartup na primeira instalação.
Para desinstalar: PowerShell -ExecutionPolicy Bypass -File Uninstall.ps1 na pasta instalada.

Extraia o ZIP em uma pasta local do Windows. Não execute o instalador por um caminho
UNC de rede ou do WSL. Uma instalação existente nunca é sobrescrita automaticamente.
Para atualizar, saia do aplicativo, desinstale a versão anterior e instale a nova.
Configurações, backups e arquivos desconhecidos são preservados na desinstalação.

Aba Discos: escolha o disco, confira o caminho completo e salve somente seu limite de alerta.
Aba Configurações gerais: salve separadamente os intervalos, percentuais e opções globais.
Aba Manutenção: prepare e revise um plano antes de confirmar qualquer operação.
Aba Diagnóstico: detalhes técnicos e caminhos candidatos indisponíveis, não discos extras confirmados.

Não é necessário instalar runtime adicional. Os limites são alertas, não cotas rígidas.
A limpeza automática vem desligada. Containers comuns e volumes não são apagados automaticamente.
Compactar exige abrir explicitamente como administrador, autorizar a parada de todo o WSL/Docker
e ter backup verificado em outro volume fixo. Não interrompa a operação nem reinicie os serviços.
Não há promessa de tamanho final mínimo ou de quantidade de espaço recuperado.

O README completo, em português, explica cada controle e as proteções:
https://github.com/lutian/wsl-cleaner
'@ | Set-Content -LiteralPath (Join-Path $out 'README.txt') -Encoding UTF8
Get-ChildItem -LiteralPath $out -File | ForEach-Object {
    $hash=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ASCII
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
Write-Output "PACKAGE=$zip"
Write-Output ('ZIP_SHA256='+(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)
