# Aplicativo nativo para Windows

Interface C++17/Win32, sem .NET, navegador, transferência de componentes de execução
nem intermediário residente de elevação. Usa controles nativos, ícone do sistema,
Consolas, preto e verde suave. A referência HTML em `design/panel-usability/01-tabs.html`
é somente referência de desenho, não parte da execução.

## Integração da compilação

Compile `native/src/app.cpp` com as fontes compartilhadas de núcleo/manutenção e os
cabeçalhos de `native/include`. Exige C++17, suporte a threads, subsistema GUI Windows
e ponto de entrada Unicode `wWinMain`. O CMake já fornece `/utf-8` no MSVC, incluindo
os literais acentuados, e biblioteca C++ estática. A inicialização dos controles comuns
solicita somente `ICC_TAB_CLASSES`, mantendo a verificação de retorno de
`InitCommonControlsEx`. EDIT, BUTTON, STATIC e COMBOBOX são classes padrão do USER32;
não precisam de registro adicional pela comctl32.

Bibliotecas da interface:

```text
user32 shell32 comctl32 ole32 advapi32 comdlg32 gdi32
```

Para ferramentas GNU compatíveis, a interface utiliza:

```text
-municode -mwindows -luser32 -lshell32 -lcomctl32 -lole32 -ladvapi32 -lcomdlg32 -lgdi32
```

O núcleo/manutenção pode exigir outras bibliotecas; o CMake contém a lista completa.
Verifique as importações reais do executável antes de publicar. Não distribua instalador
de compilador/ambiente de execução. O nome de distribuição é `WslCleaner.exe`.

## Painel e configuração

Janela redimensionável com área cliente mínima de 760 × 720 unidades lógicas e quatro
abas: `Discos`, `Configurações gerais`, `Manutenção` e `Diagnóstico`. Somente os controles
da aba ativa ficam visíveis. O seletor ocupa a largura útil; o caminho fica num campo
multilinha separado, somente leitura, com quebra e cópia Unicode. Os resumos distinguem
comprimento VHDX de alocação física, uso no convidado e espaço recuperável desconhecidos.

- `Salvar limite deste disco` usa `DiskBudgetCandidate` sobre a configuração atual e
  altera somente o limite do ID selecionado. `0` remove somente esse limite.
- `Salvar configurações gerais` usa `GlobalSettingsCandidate`, preservando todos os
  limites, inclusive IDs não vistos. Não carrega nem descarta a edição do limite.
- Ambos validam o candidato completo com `ValidateConfig` antes da persistência por
  `SaveConfig`; só um salvamento bem-sucedido substitui a configuração em memória.
  Qualquer salvamento bem-sucedido invalida o plano preparado e limpa consentimentos.
- Limite e reserva usam GiB com conversão inteira verificada: ponto ou vírgula decimal,
  sem separador de milhar, sinal, espaços, expoente ou fração de byte. A representação
  editável é exata; texto inalterado preserva os bytes originais. Os resumos mostram
  uma casa decimal truncada e bytes exatos. O formato persistido continua em bytes.
- Percentuais, intervalos e idade são inteiros. `ValidateConfig` continua autoridade
  dos intervalos: atualização 10–86400 s; aviso < crítico, 1–100%; intervalo entre
  alertas 0–604800 s; idade 0–36500 dias. Erros técnicos originais podem estar em inglês.
- Reserva vale para cada volume fixo do computador, inclusive pequenos volumes de
  sistema; não aloca espaço. `0` desativa somente alertas de reserva. Limites são
  indicativos, nunca cotas rígidas.

`RefreshDiskChoices` lista somente `VirtualDisk.measured`, com dados de item que mapeiam
para IDs separados dos índices do inventário. Mantém a identidade selecionada ao reordenar.
Se o alvo ficar indisponível, sua identidade, caminho e edição permanecem explicitamente
retidos; não é mostrado como disco medido. Candidatos sem medição aparecem em Diagnóstico
com a ressalva de que indisponível não prova ausência, lixo ou exclusão segura. Atualização
não apaga limites de IDs não vistos, não preenche novamente campos globais e não invalida
nem sobrescreve o plano revisado. Troca de disco com limite alterado pergunta antes de
descartar; recusar preserva edição/plano, confirmar carrega o novo limite e invalida o plano.

A configuração fica em `settings.conf` sob `DataDirectory()`. Iniciar e atualizar não
gravarão configurações. Arquivo ausente/corrompido/ilegível é informado e desativa automação
e manutenção até salvamento explícito válido; não é sobrescrito automaticamente. Salvar
explicitamente pode substituí-lo: preserve uma cópia antes se precisar recuperar ou analisar.
Categorias destrutivas continuam desativadas por padrão.

## Monitoramento, concorrência e vida útil

A primeira coleta e as atualizações seguintes rodam num trabalhador, fora da fila de
mensagens. Só uma operação (inventário, preparação, limpeza automática ou execução manual)
fica ativa; atualizações enquanto ocupado são ignoradas, não enfileiradas. Durante o
trabalho, edição, seleção, salvamentos e manutenção ficam desabilitados; abas e leitura/
cópia do caminho continuam disponíveis. Sair continua disponível e espera o trabalhador.

`AlertEngine::Evaluate` continua a autoridade para alertas, escalada, intervalo e histerese.
A avaliação usa configuração salva, nunca campos ainda em edição. Monitoramento não inicia
WSL nem Docker. Alertas exibem notificação e solicitam abertura do painel, mesmo quando o
Windows suprime notificações. Clique no ícone/notificação abre; botão direito oferece
`Mostrar painel` e `Sair (aguarda manutenção)`. Fechar oculta na bandeja quando disponível;
se o ícone falhar, a janela continua acessível.

O mutex global qualificado pelo SID permite somente uma instância por usuário, inclusive
entre sessões. Uma segunda instância avisa e sai; não envia comandos nem substitui a
existente. Falha de mutex recusa inicialização. Iniciar com Windows é opção explícita do
instalador, nunca consequência de salvar configurações.

O resultado passa por um slot protegido por mutex, sinalizado com `WM_APP`. O temporizador
também drena o slot se a postagem falhar, inclusive durante saída pendente. Trabalhadores
são unidos, nunca destacados. Sair aguarda a conclusão antes de destruir o estado da GUI;
não interrompe cópia, limpeza ou compactação. Encerramento normal de sessão é recusado
durante trabalho ativo. Término forçado ou falta de energia não podem ser tornados seguros.

## Consentimento e manutenção

Os botões **preparam**, não executam, usando configurações **salvas**:

1. Docker usa `Action::DockerCleanup` e alvo `docker`, independente da seleção. Exige serviço
   Desktop local validado; categorias e idade seguem a política compartilhada.
2. WSL usa `Action::WslAptCache` e **nome da distribuição**, com classificação `WSL`, não
   caminho VHDX nem ID de limite. Exige alvo medido registrado que não seja Docker.
   `apt-get clean` pode iniciar essa distribuição como root, conforme divulgado no plano.
3. Compactação usa o **caminho VHDX selecionado**. Exige administrador; saia da instância
   comum e abra explicitamente como administrador. Não existe elevação automática.
   A escolha deve ser uma pasta existente em outro volume fixo.
4. Revise a área própria com rolagem: resumo crítico em português, alvo, validade e
   destino da cópia, seguidos das **etapas técnicas originais inalteradas**, inclusive
   em inglês. Atualizar inventário não troca esse plano pelo relatório técnico.
5. Digite exatamente `CLEAN` para limpeza ou `COMPACT` para compactação. Esta última exige
   ainda a caixa de parada de **TODAS** as distribuições WSL e cargas Docker. Identidade,
   acesso exclusivo, espaço, disco fora de uso e cópia consistente verificada continuam
   obrigatórios no serviço; palavra digitada e privilégio não os dispensam.

Planos são de uso único e expiram em cinco minutos. Trocar seleção com confirmação ou
salvar invalida; simples atualização não. A execução consome a autorização antes de chamar
o trabalhador. Exclusão é irreversível e pode terminar parcialmente. Não há garantia de
redução mínima do VHDX, restauração/apagamento automático de cópias nem cancelamento forçado.
Não reinicie WSL/Docker, suspenda ou interrompa a máquina durante compactação.

Automação continua exclusiva do Docker: configuração válida, alerta de limite Docker,
adesão automática e ao menos uma categoria autorizada. Não é acionada somente por reserva
do computador. Usa os mesmos Prepare/Execute e filtros de idade/categoria; tentativa é
registrada **antes** de preparar, inclusive falha/exceção, com intervalo de pelo menos
60 segundos ou o intervalo salvo maior. WSL e compactação nunca são automáticos.

## Interface pública de linha de comando

```text
WslCleaner.exe --help
WslCleaner.exe --version
WslCleaner.exe --inventory
WslCleaner.exe --smoke-test
WslCleaner.exe --tray
```

Use `Start-Process -Wait -PassThru` ou API de processos ao aguardar este executável GUI.
Usa stdout redirecionado herdado ou console pai anexado; não aloca console. Código 0 para
sucesso, 1 para falha de inicialização/inventário/smoke, 2 para argumentos não suportados
ou múltiplos. Os diagnósticos desses modos usam stdout; stderr fica vazio. Contratos
públicos de ajuda, versão, inventário, rejeição e sucesso do smoke permanecem inalterados.
As saídas públicas permanecem em inglês por compatibilidade de máquina; controles e
resumos críticos do painel são em português.

- `--inventory`: leitura **real** com valores padrão; não carrega/grava configuração,
  cria bandeja, atualiza periodicamente ou executa manutenção. Não usar como smoke isolado.
- `--smoke-test`: inicializa COM e os controles reais numa janela **oculta**, verifica
  estado e geometria, destrói e sai. Não inicializa mutex residente, diretório de dados,
  configuração em disco, coleta, temporizador, bandeja ou chamadas ao serviço de manutenção.
  Não escreve na área de transferência. Não constitui consentimento nem dispensa privilégios.
  Sucesso mantém exatamente `SMOKE OK: native controls initialized; no tray, polling or maintenance.`
  seguido de CRLF, stderr vazio e código 0. Falhas usam stdout/código 1 e nunca abrem diálogo.
- `--tray`: monitoramento residente normal, oculto inicialmente somente se há ícone e
  configuração válida. Não suprime erros nem autoriza limpeza. Sem argumento abre o painel.

### O que o smoke oculto verifica na fonte atual

`HiddenPanelChecks` usa apenas fixtures em memória e os mesmos helpers/controles da GUI:

- Redimensiona de fato a janela para áreas clientes 760×720, 980×820 e 1280×980; observa
  retângulos após `WM_SIZE`, largura útil do seletor, campo separado com quebra e cópia,
  limites dos controles e plano sem sobrepor consentimentos.
- Confere quatro títulos PT e mudança de página por notificação nativa; apenas controles
  da página ativa têm estilo visível. Confere capacidade de cópia sem tocar no clipboard.
- Exercita conversão GiB exata, vírgula, limite uint64, rejeição de frações de byte/estouro,
  preservação do valor original, remoção por zero e candidatos de salvamento isolados por
  escopo, incluindo retenção de IDs não vistos e saída intacta em rejeições.
- Alimenta a lista com candidato indisponível seguido de discos medidos, reordena/remove/
  devolve o selecionado e verifica IDs, caminho, edição de limite, campos globais, plano e
  consentimento preservados. Simula recusa/aceite do descarte sem abrir diálogo.
- Inclui regressão de reentrância: na mesma posição da espera por resposta do diálogo,
  depois de capturar o próximo ID, injeta atualização/reordenação ou desaparecimento
  do destino e só então aplica o descarte. Verifica coerência entre seletor, caminho,
  identidade de salvamento e consentimentos. Restaura a fixture antes de relatar falha.
  O responsável observou RED runtime com código 1, stderr vazio e a falha
  `SMOKE FAILED: reentrant selection reordered target keeps combo path and save identity aligned`.
  A fonte agora resolve o ID capturado no inventário atualizado e sincroniza o seletor
  ao confirmar a troca. Se o destino desapareceu ou perdeu medição durante o prompt,
  mantém a seleção anterior, a edição e o plano. O responsável observou GREEN no job35:
  o smoke oculto passou, incluindo ambos os cenários de reentrância e a limpeza dos
  consentimentos na troca confirmada. O acompanhamento estático independente também
  encerrou esse bloqueio.
- Confere bloqueio de mutações durante trabalho e disponibilidade de Sair, sem iniciar thread.

Isso **não** prova renderização visual, navegação/foco real por teclado, DPI, bandeja,
notificações, segurança de backup, comportamento do armazenamento ou manutenção real.
Não há nova opção pública para fixtures nem qualquer caminho de aprovação falsa no serviço.

## Instalação nativa por usuário

Distribua `WslCleaner.exe`, `Install.ps1`, `Uninstall.ps1` e `Packaging.Common.ps1`.
Inspecione/confie na origem; os scripts não fazem promessa de assinatura/autenticidade.

```powershell
# Não inicia, eleva nem instala dependências:
.\Install.ps1 -ExecutablePath .\WslCleaner.exe
# Inicialização opcional com --tray no próximo acesso ao Windows:
.\Install.ps1 -ExecutablePath .\WslCleaner.exe -EnableStartup
# Depois de sair explicitamente:
& "$env:LOCALAPPDATA\Programs\WslCleanerNative\Uninstall.ps1"
```

Destino fixo `%LOCALAPPDATA%\Programs\WslCleanerNative`. Qualquer destino existente,
mesmo vazio, é recusado. Manifesto possui somente `WslCleaner.exe`, `Uninstall.ps1`,
`Packaging.Common.ps1` e `install-manifest.json`; configurações/cópias não pertencem à
lista e devem ficar fora. Instalar não abre o app; scripts não matam processos.
Desinstalar verifica manifesto completo, propriedade dos arquivos, acesso exclusivo ao
executável e propriedade/argumentos do atalho opcional antes de removê-lo. Arquivos
estranhos permanecem e impedem remover a pasta. Não há remoção recursiva, curinga,
forçada, ampla de registro ou perfil. Caminhos UNC, ancestrais reparse e colisões de
atalhos são recusados. Dados de `DataDirectory()` e cópias do usuário ficam intactos.
Esses guardas não são uma transação contra alterações concorrentes hostis: use pacote
local confiável sem outro manipulador de arquivos na mesma pasta. Falha parcial exige
inspeção; não existe reversão automática nem autorização para apagar amplamente.

### Atualização sem sobrescrita automática

Encerre a instância antiga com `Sair (aguarda manutenção)`, não X. Na interface antiga em
inglês, use `Exit (waits for maintenance)`. Aguarde terminar qualquer trabalho ativo.
Para instalação existente, execute o `Uninstall.ps1` instalado, que preserva configurações,
cópias e arquivos desconhecidos; depois extraia o novo ZIP numa pasta local nova e execute
`Install.cmd`. Se arquivos desconhecidos mantiverem o destino existente, pare e revise:
a recusa continua obrigatória; não force remoção nem sobrescrita. A inicialização com
Windows exige novamente adesão explícita se desejada. Para uso portátil, somente encerre
a instância antiga e execute o novo arquivo extraído em outra pasta local.

## Estado da verificação e comandos seguros

Resultados informados pelo responsável pela integração: compilação final com MSVC 19.44,
CTest Windows **3/3**, estrutura do painel **6/6**, scripts/API **16/16**, incluindo controles
ocultos reais, e instalação/desinstalação isoladas **4/4**, todos aprovados no job35 (código 0).
Núcleo e manutenção portáveis passaram **18/18** e **19/19** no ciclo de integração.
O RED estrutural inicial foi seguido por falha runtime de inicialização de controles (job33)
e RED runtime de reentrância (job34); as correções passaram na execução final. O acompanhamento
estático independente encerrou o bloqueio de seleção. O autor deste fechamento documental
não executou builds, testes nem captura nativa.

Uma única comparação diferencial contra `b90390f` confirmou código, stdout e stderr idênticos
para `--help`, `--version`, `--unknown` e `--smoke-test`. Não foi uma execução de inventário,
monitor residente nem manutenção real.

O pacote candidato local `packages/windows-x64/WslCleaner-native-v1.zip` foi atualizado com
o executável verificado: **564224 bytes (551 KiB)**; ZIP de **264491 bytes**. A publicação
remota ainda não está concluída. O responsável verificou os 11 insumos contra a cópia final
Windows, igualdade do executável compilado/compactado, sete entradas, seis hashes internos,
três scripts e importações x64 restritas ao sistema Windows. A geração e análise sintática
do empacotador passaram no job36 após tradução do `README.txt` para português com UTF-8/BOM;
só texto/codificação mudaram, não guardas nem comandos. Núcleo, cabeçalhos, configuração e
instaladores não foram alterados. Essas verificações não constituem garantia de produção.

Verificações estruturais, sem iniciar executável:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File native\tests\panel_checks.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File native\tests\shell_checks.ps1
```

Após compilação autorizada, verificações públicas sem manutenção:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File native\tests\shell_checks.ps1 -Executable build\native\Release\WslCleaner.exe
```

O último comando executa somente ajuda, versão, rejeição e smoke oculto, com saída
redirecionada e códigos verificados. Mantém 16 regras existentes, com leitura explícita
UTF8 e assertivas ajustadas às divulgações em português. Não instala, coleta inventário
nem inicia monitor residente. Um filho que exceda o prazo é relatado, não terminado;
inspecione especificamente esse processo.

A verificação interativa ainda deve conferir legibilidade, texto longo, DPI/teclado/foco,
rolagem do plano, alertas/bandeja, SID por usuário, arquivo inválido e salvamentos explícitos,
resposta com trabalhador lento e saída segura. Instalação exige perfil/VM descartável.
Nunca use dados WSL/Docker de trabalho para testar limpeza, parada ou compactação.
