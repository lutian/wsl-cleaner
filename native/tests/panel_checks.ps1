<#
.SYNOPSIS
Regressoes estruturais do painel; nao executa o aplicativo.
.DESCRIPTION
Le somente app.cpp e README.md em UTF-8. Sem fixtures, gravacoes, inventario,
instaladores, processos filhos ou interface residente. Cada regra publica PASS/FAIL;
qualquer falha resulta em exit 1. Este RED de fonte NAO comprova comportamento GUI.
Os nomes de funcoes abaixo verificam os pontos de integracao da implementacao.
O --smoke-test publico complementa estes checks com controles ocultos reais;
este script nao o executa.
#>
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$native = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $native
$app = Get-Content -LiteralPath (Join-Path $native 'src\app.cpp') -Raw -Encoding UTF8
$readme = Get-Content -LiteralPath (Join-Path $root 'README.md') -Raw -Encoding UTF8
$script:passed = 0
$script:failed = 0
function Require([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}
function Test-Rule([string] $Name, [scriptblock] $Body) {
    try { & $Body; $script:passed++; Write-Output "PASS $Name" }
    catch { $script:failed++; Write-Output "FAIL ${Name}: $($_.Exception.Message)" }
}
# Bounded function extraction for the current C++ style. Not a C++ parser:
# braces in string literals/comments would require adapting this structural helper.
function Function-Body([string] $Name) {
    $match = [regex]::Match($app, '\b' + [regex]::Escape($Name) + '\s*\([^;{}]*\)\s*\{')
    Require $match.Success "Funcao ausente: $Name"
    $start = $match.Index + $match.Length
    $depth = 1
    for ($i = $start; $i -lt $app.Length; $i++) {
        if ($app[$i] -eq '{') { $depth++ }
        if ($app[$i] -eq '}') { $depth-- }
        if ($depth -eq 0) { return $app.Substring($start, $i - $start) }
    }
    throw "Funcao sem fechamento identificavel: $Name"
}
Test-Rule 'S1/S4: abas nativas em portugues e janela redimensionavel' {
    foreach ($label in @('Discos', 'Configurações gerais', 'Manutenção', 'Diagnóstico')) {
        Require ($app.Contains('L"' + $label + '"')) "Aba ausente: $label"
    }
    Require ($app -match 'WC_TABCONTROL|SysTabControl32') 'Controle nativo de abas ausente.'
    Require ($app.Contains('WM_SIZE') -and $app.Contains('WS_THICKFRAME')) 'Layout redimensionavel ausente.'
}
Test-Rule 'S1/S4: seletor ocupa largura e caminho separado permite quebra e copia' {
    $layout = Function-Body 'Layout'
    Require ($layout -match 'MoveWindow\(Control\(Disks\),[^;]*width\s*-') 'Seletor sem largura derivada da area cliente.'
    Require ($app -match 'Child\(L"EDIT",L"",DiskPath,[^;]*ES_MULTILINE[^;]*ES_READONLY') 'Campo de caminho completo somente leitura/multilinha ausente.'
    Require ($app.Contains('L"Copiar caminho"')) 'Acao explicita de copia ausente.'
    Require ($app.Contains('CF_UNICODETEXT') -and $app.Contains('SetClipboardData')) 'Copia Unicode do caminho ausente.'
}
Test-Rule 'S1/S4: salvamentos de disco e globais com mesclagem de escopo separado' {
    $disk = Function-Body 'SaveDiskBudget'
    $global = Function-Body 'SaveGlobalSettings'
    $diskCandidate = Function-Body 'DiskBudgetCandidate'
    $globalCandidate = Function-Body 'GlobalSettingsCandidate'
    Require ($disk.Contains('DiskBudgetCandidate(') -and $global.Contains('GlobalSettingsCandidate(')) 'Salvamento nao reutiliza os candidatos de escopo testaveis sem persistencia.'
    Require ($diskCandidate -match 'wc::Config\s+\w+\s*=\s*(?:app\.config|current)\s*;') 'Candidato de disco nao parte da configuracao atual.'
    Require ($diskCandidate.Contains('.budgets')) 'Candidato de disco nao atualiza orcamento.'
    Require ($diskCandidate -notmatch '\.(host_reserve|warning_percent|critical_percent|poll_seconds|cooldown_seconds|minimum_age_days|auto_cleanup|clean_build_cache|clean_dangling_images|clean_disposable_containers)\s*=') 'Candidato de disco modifica globais.'
    Require ($globalCandidate -match 'wc::Config\s+\w+\s*=\s*(?:app\.config|current)\s*;') 'Candidato global nao parte da configuracao atual.'
    Require ($globalCandidate -notmatch '\.budgets\s*(?:\[|=|\.)') 'Candidato global modifica orcamentos.'
    Require ($app -notmatch '\bvoid SaveSettings\(') 'Salvamento combinado antigo permanece.'
}
Test-Rule 'S1/S4: atualizacao preserva edicoes e plano; troca de disco confirma descarte' {
    $refresh = Function-Body 'RefreshDiskChoices'
    Require ($refresh -notmatch 'DisplayBudget\(|has_plan\s*=\s*false|Set\(Budget|Set\(Consent') 'Atualizacao apaga edicao ou plano.'
    Require ($refresh.Contains('SelectedDisk(')) 'Atualizacao nao preserva identidade selecionada.'
    $selection = Function-Body 'ChangeDiskSelection'
    Require ($selection.Contains('budget_dirty') -and $selection.Contains('MessageBoxW') -and $selection.Contains('MB_YESNO')) 'Troca sem confirmacao explicita para orcamento alterado.'
    Require ($app -match 'case Finished:[\s\S]*RefreshDiskChoices\(') 'Conclusao do inventario nao usa a atualizacao preservadora.'
}
Test-Rule 'S1/S4: somente discos medidos no seletor; candidatos indisponiveis no diagnostico' {
    $choices = Function-Body 'RefreshDiskChoices'
    Require ($choices -match '\.measured\b') 'Lista nao filtra por medida conhecida.'
    Require ($choices.Contains('CB_SETITEMDATA')) 'Lista filtrada sem mapeamento estavel para o inventario.'
    $selected = Function-Body 'SelectedDisk'
    Require ($selected.Contains('CB_GETITEMDATA')) 'Selecao ainda usa indice da lista como indice do inventario.'
    $diagnostics = Function-Body 'RenderDiagnostics'
    Require ($diagnostics -match '\.measured\b') 'Diagnostico nao separa candidatos sem medicao.'
    Require ($diagnostics.Contains('indispon') -and $diagnostics.Contains('ausente')) 'Falta explicar que indisponivel nao prova ausencia.'
}
Test-Rule 'S2/S5: README portugues alinhado aos controles e limites da verificacao' {
    # Remove only code: exact current UI labels, command tokens and technical keys stay unchanged.
    $prose = [regex]::Replace($readme, '(?s)```.*?```|`[^`\r\n]+`', '')
    foreach ($heading in @('## Instalar ou executar', '## O que faz', '## Guia do painel', '## Segurança e configurações compatíveis', '## Compilar a partir do código-fonte', '## Limites da verificação')) {
        Require ($readme.Contains($heading)) "Secao portuguesa ausente: $heading"
    }
    Require ($prose -notmatch '(?im)^#{1,6} .*(Install|What it does|Panel guide|Quick setup|Reading the|Disk selection|Global monitoring|Docker cleanup|Manual maintenance|Window close|Why several|Safety and supported|Build from|Verification boundary)') 'Titulo antigo em ingles.'
    Require ($prose -notmatch '(?i)\b(The selected disk|This guide describes|Leave all four|These options are global|Preparing a plan|Four Docker entries|No production Docker|The information panel|The build is unsigned|Settings are stored|For optional login startup)\b') 'Explicacao antiga em ingles.'
    foreach ($label in @('`Salvar limite deste disco`', '`Salvar configurações gerais`', '`Copiar caminho`', '`Preparar cache WSL`', '`Executar plano revisado`')) {
        Require ($readme.Contains($label)) "Controle atual nao documentado: $label"
    }
    Require ($readme.Contains('bytes originais') -and $readme.Contains('IDs não vistos')) 'Preservacao de bytes e limites nao documentada.'
    Require ($readme.Contains('controles nativos **ocultos**') -and $readme.Contains('São verificações delimitadas') -and $readme.Contains('teclado/DPI, foco')) 'Falta distinguir verificacoes ocultas delimitadas de teclado, DPI e foco ainda nao verificados.'
    Require (-not $readme.Contains('ainda não foi implementada')) 'README ainda descreve a interface anterior como atual.'
}
Write-Output 'Somente estrutura de fonte; este script nao executa GUI. Consulte separadamente os resultados do smoke nativo no Windows.'
Write-Output "$script:passed passed; $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
exit 0
