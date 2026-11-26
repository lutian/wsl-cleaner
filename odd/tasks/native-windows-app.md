# Native Windows application
Objective: deliver the lightweight Windows application as C++/Win32, without managed runtime dependencies. Branch: feat/native-windows-app.
Delivery: verified work-unit commits to authorized origin; no PR/release creation authorized. Build: portable native compiler and native Windows checks; dangerous workflows use isolated fake/disposable state.

## Specs
S1. "pode continuar até entregar o projeto completo"
S2. "aplicativo nativo"
S3. "atualiza o repositorio"
S4. "o app deve ser o mais simples e leve possível, deve rodar em Windows e ficar ativo para detectar quando um disco vai estourar."
S5. "com uma tela bem simples (tipo DOS ou semelhante) que apresente o tamanho do disco (ou discos se tiver mais) que identifique o wsl e o Docker e o tamanho de cada um deles e poder definir um tamanho limite para cada um (wsl e Docker)"
S6. "quando o disco de algum deles está perto de estourar, o aplicativo deve apresentar uma alarme para o usuário abrir a tela com as informações relevantes (discos, tamanhos, limites, etc.)"
S7. "o aplicativo por se só faz o analise e apaga os containers órfãos, imagens antigas e tudo o que puder para liberar espaço e depois reduzir o tamanho do disco (wsl ou Docker) ao mínimo possível."
S8. "tem que apagar do repo tudo o que tiver a ver com o dotnet da ideia antiga errada"
S9. Accepted safety boundary: budgets are advisory, not hard filesystem quotas. Only explicitly enabled cleanup categories; never automatically delete volumes or ordinary stopped containers. Offline compaction requires fresh consent, disclosed all-WSL shutdown, consistent verified backup, target identity checks and exclusive access. Never perform maintenance on the working user environment during development. Remove abandoned implementation from current tree; do not rewrite Git history.

## Tasks
T1 | S2-S3 | parent | done | ea20c78 published/verified origin/main with native product direction.
T2 | S4-S6 | native worker | pending | native inventory/config/alerts with deterministic policy checks; verified portable compiler ready; commit pending.
T3 | S7,S9 | native worker | pending | allowlisted cleanup/offline compaction with isolated checks; own risk-bearing commits.
T4 | S4-S6 | native worker | pending | Win32 tray, information/settings panel and alerts; commit pending.
T5 | S1-S9 | parent + independent verifier | pending | native build/package, per-user installer, isolated installation and safety verification.
T6 | S3 | parent | pending | publish verified native source/package and verify remote.
T7 | S8 | parent | in_progress | remove abandoned source/build/tests/scripts/docs and local generated drafts, verify no leftovers, publish cleanup; no history rewrite.

## Log
L1. Original product request verbatim:
agora quero que crie um plano para criar um aplicativo de Windows, o mais simples possível, para manter sem estourar os discos wsl e Docker do computador, (para evitar o trabalho que tivemos que fazer no meu computador).
A ideia é poder instalar no computador, e com uma tela bem simples (tipo DOS ou semelhante) que apresente o tamanho do disco (ou discos se tiver mais) que identifique o wsl e o Docker e o tamanho de cada um deles e poder definir um tamanho limite para cada um (wsl e Docker), quando o disco de algum deles está perto de estourar, o aplicativo deve apresentar uma alarme para o usuário abrir a tela com as informações relevantes (discos, tamanhos, limites, etc.) e o aplicativo por se só faz o analise e apaga os containers órfãos, imagens antigas e tudo o que puder para liberar espaço e depois reduzir o tamanho do disco (wsl ou Docker) ao mínimo possível.
o app deve ser o mais simples e leve possível, deve rodar em Windows e ficar ativo para detectar quando um disco vai estourar.
L2. Remote authorization verbatim: ok, pode começar, crie a pasta wsl-cleaner e o projeto git local, e depois sobe para o remoto https://github.com/lutian/wsl-cleaner.git
L3. Subsequent authorization verbatim: pode continuar até entregar o projeto completo
L4. Native decision verbatim: aplicativo nativo
L5. Repository instruction verbatim: atualiza o repositorio
L6. Removal instruction verbatim: tem que apagar do repo tudo o que tiver a ver com o dotnet da ideia antiga errada
L7. Shared C++17 interface native/include/wsl_cleaner.hpp is parent-owned. Core implements config/inventory/AlertEngine; maintenance implements MaintenanceService/QuoteArgument; UI owns wWinMain and tray/panels. UTF-16 paths, no extra runtime in final exe. Native writes are not yet a functioning app. No real host cleanup, shutdown, compaction or installation performed.
L8. Portable Zig0.14.1 verified in session temp, no system installation: /tmp/gpc7micS/wsl-cleaner-native-tools-986eqmfl/zig-x86_64-linux-0.14.1/zig; SHA256 24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c. Linux g++ available for portable policy tests; parent executes cross-builds in background. Compiler is a build tool, not a shipped dependency.
L9. Explicitly authorized parent-only removal: all abandoned source/test projects, generated outputs under those project directories, solution/build/SDK pins, old publishing/install scripts, old prototype docs and old feature documents. All are proven session-created; native files and Git history preserved. README and ignore rules now describe only native direction.
