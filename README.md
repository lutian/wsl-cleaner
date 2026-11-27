# WSL Cleaner

Aplicativo leve e **nativo para Windows, em C++17/Win32**, para monitorar WSL2, Docker
Desktop e o espaço dos discos do computador. Não exige .NET, navegador, servidor local
nem transferência de componentes de execução. Usa controles do Windows, fonte Consolas,
fundo preto e texto verde suave.

## Instalar ou executar

1. Baixe [WslCleaner-native-v1.zip](packages/windows-x64/WslCleaner-native-v1.zip).
2. Extraia para uma **pasta local do Windows**, não para um caminho UNC de rede ou do WSL.
3. Abra `WslCleaner.exe` com dois cliques para uso portátil, ou `Install.cmd` para instalar
   somente para o usuário atual.
4. Configure limites na aba `Discos` e salve cada disco explicitamente. Configure opções
   compartilhadas na aba `Configurações gerais`. Fechar com X mantém o monitor na bandeja;
   use `Sair (aguarda manutenção)` para encerrar.

A instalação não abre o aplicativo nem ativa sua inicialização automática. Para iniciar
ao entrar no Windows, execute `Install.cmd -EnableStartup` na primeira instalação.
O instalador recusa destinos existentes, mesmo vazios; não atualiza nem sobrescreve
uma instalação silenciosamente. Para desinstalar, execute o `Uninstall.ps1` instalado
com Windows PowerShell. Configurações, cópias de segurança e arquivos desconhecidos são
preservados. As somas estão em [SHA256SUMS.txt](packages/windows-x64/SHA256SUMS.txt).

### Atualizar uma versão existente

Não há atualização automática nem substituição silenciosa da instalação:

1. Na instância antiga, use `Sair (aguarda manutenção)` — **não X**, que apenas oculta.
   Se a versão antiga estiver em inglês, use `Exit (waits for maintenance)`. Aguarde o
   encerramento completo, inclusive qualquer manutenção ativa.
2. Para instalação por usuário, execute o `Uninstall.ps1` já instalado com Windows
   PowerShell. Configurações e cópias de segurança são preservadas. Se arquivos desconhecidos
   mantiverem a pasta de destino existente, pare e revise; não force remoção nem sobrescrita.
3. Extraia o novo ZIP numa pasta local nova e execute `Install.cmd`. A adesão à inicialização
   com Windows continua explícita, por `Install.cmd -EnableStartup`, se desejada.

Para uso somente portátil, basta encerrar a instância antiga e executar o novo
`WslCleaner.exe` extraído em outra pasta local, sem desinstalar nem instalar.

O pacote `packages/windows-x64/WslCleaner-native-v1.zip` desta revisão inclui o painel
descrito neste guia. O executável tem **551 KiB (564224 bytes)**; o ZIP tem 264491 bytes.
Confira o arquivo baixado usando a soma publicada em `SHA256SUMS.txt` antes de instalar.
O `README.txt` gerado dentro do pacote também está em português, com UTF-8/BOM compatível
com Windows PowerShell 5. A distribuição não tem assinatura digital; o Windows pode avisar
que o editor é desconhecido. Somas verificam integridade, não identidade do editor.
Revise o código e não contorne as políticas de segurança da sua organização.

## O que faz

| Recurso | Comportamento |
|---|---|
| Inventário | Volumes fixos montados do computador (letras ou pastas), registros WSL2 do usuário atual e caminhos conhecidos do Docker Desktop |
| Limites de alerta | Limites indicativos por disco, percentuais de aviso/crítico e reserva livre no computador |
| Monitoramento | Leituras de metadados em segundo plano, alertas na bandeja, intervalo entre alertas e histerese de recuperação |
| Configurações | Persistência validada; arquivos corrompidos não são sobrescritos automaticamente |
| Limpeza do Docker | Adesão explícita para cache antigo, imagens sem referência e contêineres parados com rótulo de descartáveis |
| Limpeza do WSL | `apt-get clean` confirmado manualmente para uma distribuição registrada e nomeada que não seja Docker |
| Compactação | Parada consentida, cópia integral verificada com o disco fora de uso e chamada nativa a `CompactVirtualDisk` |
| Histórico | Registros duráveis de manutenção e manifestos de cópias nos dados do aplicativo / destino das cópias |

**Somente o comprimento do arquivo VHDX é medido.** Não é alocação física, uso dentro do
Linux, capacidade virtual máxima nem espaço recuperável. Limites são alertas, **não cotas
rígidas** nem espaço reservado. Medida indisponível não é zero. Monitorar não inicia WSL
nem Docker. Os relatórios técnicos e erros originais do núcleo podem estar em inglês;
a interface apresenta em português os controles e os resumos críticos de consentimento.
As saídas públicas de linha de comando permanecem em inglês por compatibilidade dos
contratos de máquina; isso não altera o idioma dos controles nem os tokens `CLEAN`/`COMPACT`.

## Guia do painel: para que serve cada controle

A janela pode ser redimensionada. As abas nativas separam quatro tarefas: `Discos`,
`Configurações gerais`, `Manutenção` e `Diagnóstico`. Use Tab para percorrer os controles
da aba visível e as setas no seletor de abas para trocar de área. Campos e relatórios
somente leitura permitem selecionar texto. O painel usa as cores de foco e seleção dos
controles do sistema; a qualidade interativa com teclado/DPI ainda precisa ser verificada.

### Configuração inicial: monitorar sem apagar nada

1. Na aba `Configurações gerais`, deixe as quatro caixas Docker desmarcadas. Não é
   necessário consentimento de manutenção para monitorar.
2. Na aba `Discos`, selecione um disco efetivamente medido. Confira nome e caminho completo.
3. Digite o limite de alerta em GiB e use `Salvar limite deste disco`. Repita para cada
   disco desejado. Um limite abaixo do comprimento atual pode alertar imediatamente.
4. Para começar, mantenha aviso em `80`, crítico em `90`, atualização em `120` segundos e
   intervalo entre alertas em `1800` segundos. Leia a reserva do computador antes de alterá-la.
   Use `Salvar configurações gerais` para gravar somente essas opções compartilhadas.
5. Feche com X para monitorar na bandeja, ou use `Sair (aguarda manutenção)` para encerrar.

### Discos: seleção, caminho, medidas e limite

| Controle / informação | Escopo e significado |
|---|---|
| `Disco selecionado — somente discos com medição disponível` | Ocupa a largura útil da janela; lista somente discos com comprimento medido. A identidade não depende da posição na lista. Escolhe o limite a editar e os alvos WSL/compactação, **não** restringe a limpeza Docker. |
| `Caminho completo (somente leitura; Ctrl+C também copia a seleção)` | Campo separado, multilinha com quebra, seleção de texto e rolagem. Não altera o caminho do disco. |
| `Copiar caminho` | Copia o caminho completo do alvo para a área de transferência como texto Unicode. Não copia apenas a parte visível. Se estiver ocupada, selecione o texto e use Ctrl+C. |
| `Tamanho do arquivo` | GiB e bytes exatos do comprimento VHDX; a casa decimal da apresentação resumida é truncada, não uma medição adicional. |
| `Limite salvo` | Limite individual persistido; sem limite significa que este disco não tem alerta individual. |
| `Estado` | Normal, aviso, crítico, sem limite ou medição indisponível. Também informa se há edição do limite ainda não salva. Não se baseia somente em cor. |
| `Limite de alerta (GiB; 0 = sem limite)` | Somente o disco selecionado. `0` remove seu limite, não apaga, redimensiona nem desativa o disco. |
| `Salvar limite deste disco` | Mescla **somente** esse limite sobre a configuração salva atual. Preserva campos globais e todos os outros limites. Não aplica edições globais ainda não salvas nem executa manutenção. |

Atualizações do inventário preservam edições de limite, campos globais e o plano revisado.
Se o disco selecionado ficar indisponível, o seletor não inventa uma entrada medida: o
caminho, o alvo e o texto editado permanecem, com estado indisponível explícito. É possível
salvar o limite desse alvo retido; preparar WSL/compactação exige medição disponível.
Trocar de disco com limite alterado pede confirmação **antes de descartar**. Recusar mantém
o alvo, a edição e o plano. Confirmar carrega o limite do novo alvo e invalida o plano.
Se o destino ficar indisponível enquanto a confirmação estiver aberta, a troca não é
aplicada: a seleção anterior, a edição e o plano permanecem. Reordenação da lista durante
a confirmação é resolvida pelo ID atualizado, nunca pelo índice antigo.
Limites de **IDs não vistos** são mantidos; falta de medição não os apaga.

#### Unidades GiB sem alterar o formato salvo

1 GiB = `1073741824` bytes. Exemplos: `20`, `50`, `100` ou `1,5` / `1.5` GiB.
Use dígitos e no máximo um separador decimal (vírgula ou ponto), sem separadores de milhar,
espaços, sinal nem sufixos. A conversão usa inteiros, verifica estouro de 64 bits e recusa
frações de byte: `0.1` GiB não corresponde a bytes inteiros, mas `0.5` GiB corresponde.
O campo editável exibe uma representação decimal exata, que pode ser longa para valores
antigos não múltiplos de GiB. Se o texto exibido não mudar, os **bytes originais** são
preservados exatamente. O arquivo de configuração continua em bytes, sem mudança de formato.

Com limite de 100 GiB, aviso em 80% e crítico em 90%, os limiares são 80 e 90 GiB de
**comprimento do arquivo**, não percentuais usados dentro do Linux.

### Configurações gerais: todos os discos e o computador

| Campo | Significado e valores válidos |
|---|---|
| `Reserva livre (GiB; 0 desativa)` | Espaço livre mínimo desejado em **cada volume fixo montado** monitorado, por letra ou pasta. Volumes sem ponto de montagem não entram no inventário; volumes pequenos ou de sistema continuam incluídos se montados. Não aloca espaço nem é reserva exclusiva do VHDX escolhido. Padrão: 20 GiB. `0` desativa somente alertas de reserva. Avisa abaixo da reserva; crítico na metade ou menos. Usa a mesma conversão GiB exata do limite individual. |
| `Aviso (%)` | Percentual do limite salvo: 1–99, estritamente menor que crítico. Padrão: `80`. |
| `Crítico (%)` | Maior que aviso e no máximo 100. Padrão: `90`. |
| `Atualização (segundos)` | Entre leituras: 10–86400. Padrão: `120`. Trabalho já ativo não se sobrepõe ao próximo. |
| `Intervalo entre alertas (s)` | 0–604800; padrão `1800`. Elevação de gravidade pode alertar antes; a histerese reduz oscilações de aviso/recuperação. Tentativas automáticas Docker têm intervalo efetivo mínimo de 60 segundos. |
| `Idade mínima Docker (dias)` | 0–36500; padrão `30`. Idade de objetos elegíveis combinada aos filtros do serviço Docker, não idade do VHDX nem regra para APT. Não garante apagar todo objeto daquela idade. |
| `Salvar configurações gerais` | Mescla **somente os campos e caixas globais**. Preserva todos os limites individuais, inclusive IDs não vistos, e a edição de limite ainda não salva. Não executa manutenção manual. |

As quatro caixas abaixo afetam o **serviço Docker local inteiro**, não o disco selecionado.
Categorias também valem para planos manuais; marcar categoria sozinha não ativa automação.

| Caixa | O que permite |
|---|---|
| `Permitir limpeza automática após alerta de limite Docker` | Tentativa automática posterior somente após alerta de limite Docker, com configuração válida e ao menos uma categoria autorizada. Reserva do computador sozinha não aciona. Não há limpeza WSL nem compactação automática. |
| `Cache de compilação antigo e sem uso` | Limpar cache elegível. Compilações futuras podem precisar recriar/baixar dados. |
| `Imagens antigas sem referência e sem uso` | Limpar imagens elegíveis sem referência; não todas as imagens antigas nem toda imagem sem contêiner em execução. |
| `Somente contêineres parados com rótulo wsl-cleaner.disposable=true` | Apagar somente contêineres antigos e elegíveis com esse rótulo exato. Volumes e contêineres parados comuns ficam excluídos. |

**Limpeza é irreversível e pode terminar parcialmente.** Não ative automação sem entender
as categorias e aceitar suas consequências. Cada salvamento valida a configuração completa
antes de usar a persistência compartilhada. Erros de validação não gravam o candidato.
As configurações ficam em `%LOCALAPPDATA%\WslCleaner\settings.conf`. Arquivo ausente,
corrompido ou ilegível desativa automação/manutenção até salvamento explícito válido;
não é sobrescrito ao iniciar ou atualizar. Salvar explicitamente pode substituí-lo:
guarde uma cópia antes se precisar de diagnóstico ou recuperação.

### Manutenção: preparar, revisar e só então consentir

| Controle | Alvo e efeito |
|---|---|
| `Preparar limpeza Docker` | Plano para o serviço Docker local validado com categorias/idade **salvas**; independe da seleção e não compacta VHDX. |
| `Preparar cache WSL` | `apt-get clean` para a distribuição WSL registrada e selecionada, que não seja Docker, pelo **nome**, não pelo caminho/ID do disco. Pode iniciar essa distribuição como root. Remove arquivos de pacotes baixados, não aplicativos instalados nem arquivos pessoais. |
| `Preparar compactação` | Caminho VHDX selecionado. Exige administrador, pasta de cópia existente em outro volume fixo e todas as verificações de identidade, disco fora de uso, espaço e cópia integral verificada. Não promete redução mínima. |
| Área do plano | Resumo crítico em português, alvo, validade, destino da cópia e etapas técnicas originais, inclusive se estiverem em inglês. Área própria com rolagem; atualizações de inventário não a substituem pelo relatório. |
| `Confirmação exata: CLEAN (limpeza) ou COMPACT (compactação)` | Digite exatamente `CLEAN` ou `COMPACT`, em maiúsculas, conforme o plano. Não é configuração permanente nem dispensa verificações do serviço. |
| `Autorizar parada de TODAS as distribuições WSL e cargas Docker (somente compactação)` | Consentimento adicional exclusivo para compactação. Pode parar **todos** os ambientes WSL/Docker, não somente o alvo. Não é necessário para monitorar. |
| `Executar plano revisado` | Somente plano preparado válido e consentimentos exigidos. Plano de uso único, expira após cinco minutos. Salvamento ou troca de seleção confirmada invalida; simples atualização do inventário não. Uma execução falha exige preparar novamente. |
| `Sair (aguarda manutenção)` | Encerra; se há trabalho ativo, aguarda com segurança em vez de interromper cópia/limpeza/compactação. |

Preparar **não** executa. Configurações ainda não salvas não entram no plano. Enquanto há
trabalho, edição, salvamento e ações de manutenção ficam desabilitados; Sair continua
acessível e aguarda o trabalhador. A leitura do caminho e as abas continuam disponíveis.

Para compactar, saia explicitamente da instância comum antes de abrir como administrador.
Não há elevação automática. Não reinicie WSL/Docker, suspenda nem interrompa a máquina até
terminar. A operação é síncrona, sem cancelamento forçado ou nova tentativa automática.
Cópias de segurança não são apagadas nem restauradas automaticamente.

### Diagnóstico e várias entradas Docker

A aba `Diagnóstico` separa candidatos com medição indisponível e relatório técnico original.
Quatro entradas Docker no inventário **não** provam quatro discos reais. Podem existir
registros `docker-desktop` / `docker-desktop-data` e estes caminhos candidatos:

| Nome técnico | Caminho / papel |
|---|---|
| `docker-desktop` | Distribuição registrada; caminho vem do registro, não necessariamente disco principal de dados. |
| `Docker data (modern)` | `%LOCALAPPDATA%\Docker\wsl\disk\docker_data.vhdx` |
| `Docker data (legacy)` | `%LOCALAPPDATA%\Docker\wsl\data\ext4.vhdx` |
| `Docker data (alternate)` | `%LOCALAPPDATA%\Docker\wsl\data\docker_data.vhdx` |

Caminhos normalizados iguais são unidos; diferentes permanecem no diagnóstico mesmo sem
medição. **Indisponível não prova ausência, lixo, cópia duplicada nem exclusão segura**:
pode haver arquivo ausente, inacessível ou recusado por segurança. Não apague diretórios
Docker nem registros WSL para reduzir a lista. Locais personalizados podem não ser encontrados.

| Chave técnica no relatório original | Significado |
|---|---|
| `Observed` | Instante da leitura em segundos Unix. |
| `Host`, `capacity`, `free` | Volume Windows fixo montado, identificado pelo primeiro caminho de montagem (letra ou pasta), capacidade e bytes livres. Volumes sem montagem são omitidos; falha ao consultar montagens gera uma nota, não um alerta de reserva. |
| `Disk`, `kind`, `path` | Nome, classificação WSL/Docker e caminho VHDX. |
| `logical` | Comprimento do arquivo em bytes, não uso no convidado nem espaço recuperável. |
| `budget`, `unset` | Limite salvo ou limite individual não definido. |
| `unknown` | Medição indisponível, nunca zero implícito. |
| `Note` | Limitações e avisos de descoberta/medição. |

O diagnóstico não é um histórico cronológico nem substitui os registros duráveis de manutenção.

### Fechamento e bandeja

- **X:** oculta na bandeja se o ícone existir; caso contrário, mantém a janela acessível.
- **Clique no ícone ou notificação:** abre o painel. Alertas também solicitam sua abertura.
- **Botão direito → `Mostrar painel`:** abre a janela.
- **Botão direito → `Sair (aguarda manutenção)`:** encerra aguardando a operação ativa.
- **Inicialização com Windows:** opção separada do instalador, `Install.cmd -EnableStartup`.
- **Segunda instância:** um mutex por SID do usuário recusa duplicatas inclusive entre
  sessões; não substitui a instância existente. Abra pelo ícone da bandeja.

## Segurança e configurações compatíveis

- Sem apagar volumes, contêineres parados comuns, arquivos pessoais nem executar limpeza ampla.
- Limpeza automática desligada por padrão; exige categorias e idade mínima explícitas.
- Docker precisa ser um serviço Desktop local acessível e verificado; remoto/não verificável é recusado.
- Compactação exige VHDX dinâmico reconhecido, administrador e cópia completa verificada em
  **outro volume fixo**, além de todas as proteções do serviço. Não há promessa de espaço recuperado.
- Verificações Windows/Docker são as proteções finais na transferência documentada de
  identificadores de metadados; outros clientes não são excluídos por uma transação.
  Alvos incompatíveis ou alterados são recusados.
- Encerrar aguarda o trabalhador. A janela recusa encerramento normal de sessão durante
  trabalho ativo; término forçado pelo sistema ou falta de energia não podem ser tornados seguros.
- Cópias nunca são sobrescritas, apagadas ou restauradas silenciosamente.

Alvo: Windows 10/11 x64; WSL2 registrado/padrão do usuário atual e configurações locais
compatíveis do Docker Desktop. Não se promete descobrir outros usuários, serviços remotos,
locais Docker personalizados nem arranjos incompatíveis. Veja [uso no Windows](native/docs/windows.md),
[monitoramento](native/docs/core.md) e [manutenção](native/docs/maintenance.md).

## Compilar a partir do código-fonte

Somente para desenvolvimento: compilador C++17, CMake e SDK do Windows (por exemplo,
ferramentas C++ de área de trabalho do Visual Studio 2022). O aplicativo instalado não
precisa dessas ferramentas. Em uma cópia local do código no Windows:

```powershell
cmake -S native -B build/native
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File native/tests/panel_checks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File native/tests/shell_checks.ps1 -Executable build/native/Release/WslCleaner.exe
```

MSVC usa biblioteca C++ estática e `/utf-8` para os literais da fonte. CMake também oferece
verificações portáveis das regras no Linux. Para gerar pacote:
`native/packaging/Package.ps1 -Executable <exe> -OutputDirectory <new-directory>`.

## Limites da verificação

O responsável pela integração observou a compilação Windows final com MSVC 19.44 e
resultado de sucesso: CTest **3/3**, verificações estruturais do painel **6/6**, scripts/API
**16/16**, incluindo o smoke com controles nativos ocultos reais, e instalação/desinstalação
isoladas **4/4**. O smoke verificou geometria, estado e a regressão de reentrância de seleção.
A comparação diferencial única contra `b90390f`, para `--help`, `--version`, `--unknown` e
`--smoke-test`, confirmou igualdade exata de código de saída, stdout e stderr.

O RED inicial de fonte (cinco falhas e uma aprovação) foi **estrutural**, não uma prova GUI.
Depois, foram reproduzidas falhas runtime na inicialização de controles e na seleção durante
o diálogo de descarte; ambas foram corrigidas e a execução final passou. O acompanhamento
estático independente encerrou o bloqueio de reentrância. Estes resultados foram observados
pelo responsável pela integração, não por execução do autor deste fechamento documental.

O script `native/tests/panel_checks.ps1` lê somente fonte/documentação em UTF-8; não inicia
o aplicativo nem grava arquivos. Complementa, mas não substitui, o `--smoke-test` público:
este cria controles nativos **ocultos**, verifica geometria em três larguras, abas/caminho,
candidatos de configuração em memória, mapeamento filtrado, retenção de edições/plano e
confirmação de descarte simulada. Não escreve na área de transferência nem acessa configurações,
mutex residente, inventário, temporizadores, bandeja ou manutenção. A saída pública de sucesso
permanece `SMOKE OK: native controls initialized; no tray, polling or maintenance.`.
Falha do smoke usa saída padrão e código 1, sem diálogo que bloqueie a execução.

As verificações portáveis de núcleo **18/18** e manutenção **19/19** também passaram no
ciclo de integração. A geração e análise sintática do empacotador passaram após a tradução
do `README.txt`; somente texto e codificação do pacote foram alterados nessa etapa, sem
mudanças de guardas ou comandos. O responsável verificou os 11 insumos de compilação contra
a cópia final Windows, o executável do arquivo compactado idêntico ao compilado, sete entradas
do ZIP, seis hashes internos, três scripts idênticos e importações x64 somente do sistema
Windows. Núcleo, cabeçalhos, formato de configuração e instaladores permaneceram inalterados.
São verificações delimitadas, não garantia de segurança de ponta a ponta em produção.

A verificação isolada da API cria, compacta e remove seu próprio VHDX vazio Unicode no
temporário do sistema; não conecta disco nem acessa dados reais. Não execute limpeza,
parada ou compactação de dados WSL/Docker reais para validar este painel. Legibilidade,
teclado/DPI, foco, notificações, lentidão do inventário, saída segura, todos os arranjos de
armazenamento e recuperação após falhas ainda exigem verificação apropriada em ambiente isolado.
