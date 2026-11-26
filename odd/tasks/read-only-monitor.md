# Read-only Windows disk monitor
Objective: deliver the first read-only increment of WSL Cleaner. Work branch: feat/read-only-monitor; published branch: main.
Delivery: initial repository push, no PR; forecast ~350 authored lines, target one coherent monitor slice. Runner: .NET 10 build and dependency-free executable checks.

## Specs
S1. "crie a pasta wsl-cleaner e o projeto git local, e depois sobe para o remoto https://github.com/lutian/wsl-cleaner.git"
S2. "o app deve ser o mais simples e leve possível, deve rodar em Windows"
S3. "com uma tela bem simples (tipo DOS ou semelhante) que apresente o tamanho do disco (ou discos se tiver mais) que identifique o wsl e o Docker e o tamanho de cada um deles"
S4. "poder definir um tamanho limite para cada um (wsl e Docker)"
S5. "deve rodar em Windows e ficar ativo para detectar quando um disco vai estourar."
S6. Accepted delivery boundary: the preceding proposal explicitly starts with "etapa 1 — ainda sem exclusões ou compactação." This increment implements read-only disk inventory and a text panel only. Limits, resident alerts, cleanup, compaction, installer and tray remain future increments, not completion claims for S4-S5.

## Tasks
T1 | S1-S3,S6 | route gentle-ai-worker (context backstop) | done | a19c69e: implemented console inventory/tests/README; build exit0, zero warnings; Linux five checks PASS.
T2 | S1-S3,S6 | route parent + independent gentle-ai-verify | done | a19c69e: native Windows three CLI checks PASS/two platform checks SKIP; real JSON inventory exit0; static verifier six checks PASS. Native review approved and acknowledged.
T3 | S1 | route parent | done | origin/main published without force; local/remote SHA equality verified for 8937c3522173e09098a12cb68ce293d07ad31e89. No PR created.

## Log
L1. Original request:
agora quero que crie um plano para criar um aplicativo de Windows, o mais simples possível, para manter sem estourar os discos wsl e Docker do computador, (para evitar o trabalho que tivemos que fazer no meu computador).
A ideia é poder instalar no computador, e com uma tela bem simples (tipo DOS ou semelhante) que apresente o tamanho do disco (ou discos se tiver mais) que identifique o wsl e o Docker e o tamanho de cada um deles e poder definir um tamanho limite para cada um (wsl e Docker), quando o disco de algum deles está perto de estourar, o aplicativo deve apresentar uma alarme para o usuário abrir a tela com as informações relevantes (discos, tamanhos, limites, etc.) e o aplicativo por se só faz o analise e apaga os containers órfãos, imagens antigas e tudo o que puder para liberar espaço e depois reduzir o tamanho do disco (wsl ou Docker) ao mínimo possível.
o app deve ser o mais simples e leve possível, deve rodar em Windows e ficar ativo para detectar quando um disco vai estourar.

L2. Authorization:
ok, pode começar, crie a pasta wsl-cleaner e o projeto git local, e depois sobe para o remoto https://github.com/lutian/wsl-cleaner.git

L3. Environment: empty destination path, .NET SDK 10.0.112 on Ubuntu WSL; gh authenticated as lutian. git ls-remote succeeded with zero refs (empty remote). Created local main and feat/read-only-monitor. No Windows SDK/runtime verified. Current scope is the explicitly proposed first increment; no destructive commands, host migration or privileged maintenance. Prefer no added NuGet dependencies. Show file length versus physical allocation accurately; never label file length as allocated-on-disk bytes. Discovery must not start WSL/Docker or run recursive host scans. Known unsupported/unavailable measurements must be shown, not guessed. Installer/resident monitor are roadmap, not present functionality.

L4. RED job2 exit1: five failures against deliberate stub (exit70, empty stdout, Implementation pending stderr). GREEN job3 Linux build+five checks passed. Windows job4 exposed title codepage mismatch; ASCII heading correction pinned by exact help assertion. Job5 build exit0 zero warnings, Linux five PASS, Windows three PASS/two explicit unsupported-platform SKIP, actual Windows JSON exit0 reporting C/D, Ubuntu and Docker data/engine. No deletion or service start.
L5. Assessment over staged snapshot: native high, process_boundary in test harness; reviewDue high_risk. Native review review-607d02e6e09971ef approved and acknowledged with authority burned. Three nonblocking followups: mounted-volume type lookup, repeatable Windows success/error fixtures, expectation promotion on future dedup discovery-order changes. Candidate-view cleanup deferred due Git worktree cleanup failure; no automatic maintenance attempted. Independent safety verifier found no severe candidate-caused issue; six static checks PASS, no independent runtime rerun. Risk: item 2 (native process-boundary classification; production has no subprocesses). Commit a19c69e contains reviewed implementation. Total initial slice 418 lines, approximately 400; coherent first snapshot includes tests/docs, no PR creation authorized. Windows alternative layouts/access-failure coverage remains pending as future tests; limits/alerts/cleanup/compaction are not implemented.
L6. First push job6 exit0: origin/main equals local 8937c3522173e09098a12cb68ce293d07ad31e89. Evidence-doc slice after a19c69e assessed passive, reviewDue false. This completion record is a documentation-only follow-up.
