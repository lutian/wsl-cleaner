# Native Windows application
Objective: deliver the accepted lightweight Windows app as C++/Win32, without .NET. Work branch: feat/native-windows-app.
Delivery: verified work-unit commits to the authorized origin; no PR/release creation authorized. Build: native Windows toolchain to be established; portable policy checks where meaningful.

## Specs
S1. "pode continuar até entregar o projeto completo"
S2. "aplicativo nativo"
S3. "atualiza o repositorio"
S4. "o app deve ser o mais simples e leve possível, deve rodar em Windows e ficar ativo para detectar quando um disco vai estourar."
S5. "com uma tela bem simples (tipo DOS ou semelhante) que apresente o tamanho do disco (ou discos se tiver mais) que identifique o wsl e o Docker e o tamanho de cada um deles e poder definir um tamanho limite para cada um (wsl e Docker)"
S6. "quando o disco de algum deles está perto de estourar, o aplicativo deve apresentar uma alarme para o usuário abrir a tela com as informações relevantes (discos, tamanhos, limites, etc.)"
S7. "o aplicativo por se só faz o analise e apaga os containers órfãos, imagens antigas e tudo o que puder para liberar espaço e depois reduzir o tamanho do disco (wsl ou Docker) ao mínimo possível."
S8. Accepted safety boundary: limits are advisory; do not equate stopped containers or unreferenced volumes with disposable data. Cleanup categories explicitly enabled; never delete volumes/ordinary stopped containers automatically. Offline compaction requires fresh authorization, shutdown disclosure, verified consistent backup, identity checks and exclusive access. No real maintenance on the working user environment during development. Preserve existing prototype and incomplete draft work; do not publish drafts as a completed product.

## Tasks
T1 | S2-S3 | parent | in_progress | update public repository direction, preserve historical prototype documentation, publish native status honestly; commit pending.
T2 | S4-S6 | native worker | pending | native disk inventory, validated configuration, thresholds and deterministic policy checks; commit pending.
T3 | S7-S8 | native worker | pending | allowlisted cleanup and guarded offline compaction with isolated tests; own risk-bearing work-unit commits.
T4 | S4-S6 | native worker | pending | Win32 tray, information/settings window, alerts and lifetime management; commit pending.
T5 | S1-S8 | parent + independent verifier | pending | native compile/package, per-user installer, isolated installation and safety verification; commit pending.
T6 | S3 | parent | pending | publish verified source and Windows package, verify remote refs and document remaining limitations.

## Log
L1. Full original product request and remote authorization are preserved verbatim in odd/tasks/read-only-monitor.md Log L1-L2.
L2. Subsequent authorization: pode continuar até entregar o projeto completo
L3. User correction: mas a ideia era ter um app leve, você criou um projeto para instalar .net 10 e para executar tem que instalar dotnet??
L4. User decision: aplicativo nativo
L5. Current instruction: atualiza o repositorio
L6. C++/Win32 replaces the .NET target. Existing published C# CLI remains historical; incomplete Core/Maintenance/Tray .NET drafts stay local and are excluded from the native delivery. No real cleanup, WSL shutdown, compaction or installation performed. Current host has Linux g++ and CMake; no Windows compiler found on PATH, so native toolchain remains a prerequisite. Windows .NET is not a runtime/build dependency of the native deliverable. Technical defaults remain conservative and editable, not hard quotas; startup and unattended cleanup opt-in.
