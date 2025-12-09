# Painel Monitoramento Fachada (C++)

Implementação completa da Fachada do Sistema de Monitoramento de Hidrômetros (SMH) conforme especificação.

## Arquitetura

- **Facade**: FachadaSMH — ponto único de entrada
- **Singleton**: FachadaSMH, LogManager
- **Adapter**: ISimuladorAdapter, AdapterSimuladorArquivo
- **Factory**: SimuladorFactory
- **Composite**: ConsumoComponent, HidrometroLeaf, UsuarioComposite (leitura simultânea)
- **Strategy**: IOcrStrategy (FilenameOcrStrategy, TesseractOcrStrategy), IStrategiaAnalise (RegraLimiteFixo, RegraMediaMovel)
- **Observer**: AlertaService, IEventoObserver, PainelObserver, SmtpEmailService
- **Repository**: IUsuarioRepository, IHistoricoRepository com SQLite

## Funcionalidades

- ✅ CRUD de usuários com autenticação (ADMIN/LEITOR)
- ✅ Vínculo hidrômetro-usuário
- ✅ Monitoramento individual e agregado com leitura concorrente
- ✅ OCR com Tesseract (fallback: stub FilenameOcrStrategy)
- ✅ Sistema de alertas com regras (limite fixo, média móvel)
- ✅ Persistência: usuários, vínculos, leituras, alertas, regras em SQLite
- ✅ Envio de e-mail via SMTP (SmtpEmailService com libcurl)
- ✅ Logger centralizado
- ✅ Carregamento de regras ao iniciar

## Build

**Requisitos:**
- CMake 3.10+
- C++17
- SQLite3 development libraries
- libcurl (opcional, para SMTP)

**Windows PowerShell (com vcpkg):**
```powershell
# instalar sqlite3 e curl
.\vcpkg\vcpkg.exe install sqlite3 curl

mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

## Execução

```powershell
# do diretório build
.\Release\painel.exe
```

O programa:
- Cria/abre `./data/smh.db` (SQLite)
- Carrega usuários e regras persistidas
- Cria usuário demo se banco vazio
- Conecta simulador de arquivos em `./simulators/sha1/`
- Executa demo com alertas e monitoramento

## Estrutura de Arquivos

```
src/
├── main.cpp                  - Implementação completa (Fachada + todas as classes)
├── core.h                    - Tipos e interfaces compartilhadas
├── sqlite_repository.h/.cpp  - Persistência SQLite
└── smtp_email.h/.cpp         - SMTP email service
```
