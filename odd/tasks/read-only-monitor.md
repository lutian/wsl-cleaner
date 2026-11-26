# Read-only Windows disk monitor
Objective: deliver the first read-only increment of WSL Cleaner. Branch: feat/read-only-monitor.
Delivery: initial repository push, no PR; forecast ~350 authored lines, target one coherent monitor slice. Runner: .NET 10 build and dependency-free executable checks.

## Specs
S1. "crie a pasta wsl-cleaner e o projeto git local, e depois sobe para o remoto https://github.com/lutian/wsl-cleaner.git"
S2. "o app deve ser o mais simples e leve possível, deve rodar em Windows"
S3. "com uma tela bem simples (tipo DOS ou semelhante) que apresente o tamanho do disco (ou discos se tiver mais) que identifique o wsl e o Docker e o tamanho de cada um deles"
S4. "poder definir um tamanho limite para cada um (wsl e Docker)"
S5. "deve rodar em Windows e ficar ativo para detectar quando um disco vai estourar."
S6. Accepted delivery boundary: the preceding proposal explicitly starts with "etapa 1 — ainda sem exclusões ou compactação." This increment implements read-only disk inventory and a text panel only. Limits, resident alerts, cleanup, compaction, installer and tray remain future increments, not completion claims for S4-S5.

## Tasks
T1 | S1-S3,S6 | route gentle-ai-worker (context backstop) | done | implemented console inventory/tests/README; parent build exit0, zero warnings, five Linux public checks PASS. Commit pending.
T2 | S1-S3,S6 | route parent; independent verifier only for named high risk | in_progress | Linux GREEN observed; native Windows checks launched; assess/staging/commit pending.
T3 | S1 | route parent | pending | publish initial branch to authorized remote without force; verify remote ref.

## Log
L1. Original request:
agora quero que crie um plano para criar um aplicativo de Windows, o mais simples possível, para manter sem estourar os discos wsl e Docker do computador, (para evitar o trabalho que tivemos que fazer no meu computador).
A ideia é poder instalar no computador, e com uma tela bem simples (tipo DOS ou semelhante) que apresente o tamanho do disco (ou discos se tiver mais) que identifique o wsl e o Docker e o tamanho de cada um deles e poder definir um tamanho limite para cada um (wsl e Docker), quando o disco de algum deles está perto de estourar, o aplicativo deve apresentar uma alarme para o usuário abrir a tela com as informações relevantes (discos, tamanhos, limites, etc.) e o aplicativo por se só faz o analise e apaga os containers órfãos, imagens antigas e tudo o que puder para liberar espaço e depois reduzir o tamanho do disco (wsl ou Docker) ao mínimo possível.
o app deve ser o mais simples e leve possível, deve rodar em Windows e ficar ativo para detectar quando um disco vai estourar.

L2. Authorization:
ok, pode começar, crie a pasta wsl-cleaner e o projeto git local, e depois sobe para o remoto https://github.com/lutian/wsl-cleaner.git

L3. Environment: empty destination path, .NET SDK 10.0.112 on Ubuntu WSL; gh authenticated as lutian. git ls-remote succeeded with zero refs (empty remote). Created local main and feat/read-only-monitor. No Windows SDK/runtime verified. Current scope is the explicitly proposed first increment; no destructive commands, host migration or privileged maintenance. Prefer no added NuGet dependencies. Show file length versus physical allocation accurately; never label file length as allocated-on-disk bytes. Discovery must not start WSL/Docker or run recursive host scans. Known unsupported/unavailable measurements must be shown, not guessed. Installer/resident monitor are roadmap, not present functionality.
