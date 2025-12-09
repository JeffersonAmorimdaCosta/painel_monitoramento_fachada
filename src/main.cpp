#include "core.h"
#ifdef USE_SQLITE3
#include "sqlite_repository.h"
#endif
#include "smtp_email.h"
#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <regex>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstring>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>

namespace fs = std::filesystem;

class UsuarioRepositoryMemory : public IUsuarioRepository {
public:
    Usuario salvar(const Usuario& user) override { return user; }
    std::optional<Usuario> buscarPorLogin(const std::string&) override { return std::nullopt; }
    std::optional<Usuario> buscarPorId(int) override { return std::nullopt; }
    void deletar(int) override {}
    void vincularHidrometro(int, const std::string&) override {}
    void desvincularHidrometro(int, const std::string&) override {}
    std::vector<Usuario> listarTodosUsuarios() override { return {}; }
};

class HistoricoRepositoryMemory : public IHistoricoRepository {
public:
    void salvarLeitura(const Leitura&) override {}
    void salvarAlerta(const AlertaRecord&) override {}
    std::vector<AlertaRecord> listarAlertasPorUsuario(int) override { return {}; }
    int salvarRegra(int, const std::string&, double, int) override { return 0; }
    std::vector<std::tuple<int, int, std::string, double, int>> listarRegrasPorUsuario(int) override { return {}; }
    std::vector<Leitura> listarLeiturasPorUsuario(int, int) override { return {}; }
};

// ==================== LOGGER ====================
class LogManager {
private:
    static LogManager* instance;
    static std::mutex instanceMutex;
    std::mutex logMutex;
    LogManager() = default;
public:
    static LogManager& getInstance() {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (!instance) instance = new LogManager();
        return *instance;
    }
    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::cout << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "] " << message << std::endl;
    }
};
LogManager* LogManager::instance = nullptr;
std::mutex LogManager::instanceMutex;

// ==================== ADAPTER ====================
class ISimuladorAdapter {
public:
    virtual ~ISimuladorAdapter() = default;
    virtual std::string obterCaminhoArquivoImagem() = 0;
};

class AdapterSimuladorArquivo : public ISimuladorAdapter {
private:
    std::string caminhoBase;
    std::string idSHA;
public:
    AdapterSimuladorArquivo(const std::string& caminho, const std::string& sha = "") 
        : caminhoBase(caminho), idSHA(sha) {}

    std::string obterCaminhoArquivoImagem() override {
        fs::path dir(caminhoBase);
        if (!fs::exists(dir)) throw std::runtime_error("Diretório não existe: " + caminhoBase);

        std::vector<fs::path> arquivos;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".txt") {
                    arquivos.push_back(entry.path());
                }
            }
        }

        if (arquivos.empty()) throw std::runtime_error("Nenhuma imagem encontrada em: " + caminhoBase);

        std::sort(arquivos.begin(), arquivos.end(), [](const fs::path& a, const fs::path& b) {
            return fs::last_write_time(a) > fs::last_write_time(b);
        });

        return arquivos[0].string();
    }
};

// ==================== FACTORY ====================
class SimuladorFactory {
public:
    static std::shared_ptr<ISimuladorAdapter> criarAdapter(const std::map<std::string, std::string>& params) {
        auto tipo = params.find("tipo");
        if (tipo == params.end() || tipo->second != "arquivo") throw std::runtime_error("Tipo não suportado");
        auto caminho = params.find("caminho");
        if (caminho == params.end()) throw std::runtime_error("Parâmetro 'caminho' obrigatório");
        auto idSHA = params.find("idSHA");
        std::string sha = (idSHA != params.end()) ? idSHA->second : "";
        return std::make_shared<AdapterSimuladorArquivo>(caminho->second, sha);
    }
};

// ==================== OCR STRATEGY ====================
class IOcrStrategy {
public:
    virtual ~IOcrStrategy() = default;
    virtual double extrairLeitura(const std::string& caminhoImagem) = 0;
};

class FilenameOcrStrategy : public IOcrStrategy {
public:
    double extrairLeitura(const std::string& caminhoImagem) override {
        fs::path p(caminhoImagem);
        std::string filename = p.stem().string();
        std::regex numRegex("(\\d+(\\.\\d+)?)");
        std::smatch match;
        if (std::regex_search(filename, match, numRegex)) {
            try { return std::stod(match[1].str()); } catch(...) { return 0.0; }
        }
        return 0.0;
    }
};

class TesseractOcrStrategy : public IOcrStrategy {
public:
    double extrairLeitura(const std::string& caminhoImagem) override {
        return FilenameOcrStrategy().extrairLeitura(caminhoImagem);
    }
};

// ==================== COMPOSITE ====================
class ConsumoComponent {
public:
    virtual ~ConsumoComponent() = default;
    virtual double obterConsumo() = 0;
};

class HidrometroLeaf : public ConsumoComponent {
private:
    std::string idSHA;
    std::shared_ptr<ISimuladorAdapter> adapter;
    std::shared_ptr<IOcrStrategy> ocrStrategy;
    std::shared_ptr<IHistoricoRepository> historicoRepo;
    int userId;
public:
    HidrometroLeaf(const std::string& sha, std::shared_ptr<ISimuladorAdapter> adp,
                   std::shared_ptr<IOcrStrategy> ocr, std::shared_ptr<IHistoricoRepository> repo, int uid)
        : idSHA(sha), adapter(adp), ocrStrategy(ocr), historicoRepo(repo), userId(uid) {}

    double obterConsumo() override {
        try {
            std::string caminhoImagem = adapter->obterCaminhoArquivoImagem();
            double valor = ocrStrategy->extrairLeitura(caminhoImagem);
            if (historicoRepo) {
                Leitura leitura;
                leitura.userId = userId;
                leitura.idSHA = idSHA;
                leitura.data = nowIso();
                leitura.valor = valor;
                leitura.caminhoImagem = caminhoImagem;
                historicoRepo->salvarLeitura(leitura);
            }
            return valor;
        } catch (const std::exception& e) {
            return 0.0;
        }
    }
private:
    static std::string nowIso() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ss;
        ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }
};

class UsuarioComposite : public ConsumoComponent {
private:
    std::vector<std::shared_ptr<ConsumoComponent>> componentes;
public:
    void adicionarComponente(std::shared_ptr<ConsumoComponent> comp) {
        componentes.push_back(comp);
    }
    double obterConsumo() override {
        double total = 0.0;
        for (auto& comp : componentes) total += comp->obterConsumo();
        return total;
    }
};

// ==================== STRATEGIES & OBSERVER ====================
class IStrategiaAnalise {
public:
    virtual ~IStrategiaAnalise() = default;
    virtual bool analisar(double consumo, std::shared_ptr<IHistoricoRepository> repo, int userId) = 0;
    virtual std::string obterMensagem(double consumo) = 0;
};

class RegraLimiteFixo : public IStrategiaAnalise {
    double limite;
public:
    explicit RegraLimiteFixo(double lim) : limite(lim) {}
    bool analisar(double consumo, std::shared_ptr<IHistoricoRepository>, int) override { return consumo > limite; }
    std::string obterMensagem(double consumo) override { return "Consumo " + std::to_string(consumo) + " > Limite " + std::to_string(limite); }
};

class RegraMediaMovel : public IStrategiaAnalise {
    int janela;
    double mult;
public:
    RegraMediaMovel(int j, double m = 1.2) : janela(j), mult(m) {}
    bool analisar(double consumo, std::shared_ptr<IHistoricoRepository> repo, int userId) override {
        if (!repo) return false;
        auto leituras = repo->listarLeiturasPorUsuario(userId, janela);
        if (leituras.empty()) return false;
        double soma = 0.0;
        for (const auto& l : leituras) soma += l.valor;
        double media = soma / leituras.size();
        return consumo > (media * mult);
    }
    std::string obterMensagem(double consumo) override { return "Consumo " + std::to_string(consumo) + " > Media Movel"; }
};

class PainelObserver : public IEventoObserver {
public:
    void atualizar(const DadosAlerta& dados) override {
        std::cout << "\n[ALERTA EM TEMPO REAL] User: " << dados.userId 
                  << " | Consumo: " << dados.consumo 
                  << " | Msg: " << dados.mensagem << std::endl;
    }
};

// ==================== ALERT SERVICE ====================
class AlertaService {
    std::vector<std::shared_ptr<IEventoObserver>> observadores;
    std::vector<std::pair<int, std::shared_ptr<IStrategiaAnalise>>> regras;
    std::shared_ptr<IHistoricoRepository> historicoRepo;
    mutable std::shared_mutex obsM, regrasM;
public:
    void setHistoricoRepository(std::shared_ptr<IHistoricoRepository> repo) { historicoRepo = repo; }
    void registrarObservador(std::shared_ptr<IEventoObserver> obs) {
        std::lock_guard<std::shared_mutex> lock(obsM);
        observadores.push_back(obs);
    }
    void adicionarRegra(int userId, std::shared_ptr<IStrategiaAnalise> st) {
        std::lock_guard<std::shared_mutex> lock(regrasM);
        regras.push_back({userId, st});
    }
    void verificarAlertas(int userId, double consumo) {
        std::shared_lock<std::shared_mutex> lockRegras(regrasM);
        auto regrasLocais = regras;
        lockRegras.unlock();

        for (const auto& [uid, strategy] : regrasLocais) {
            if (uid == userId && strategy->analisar(consumo, historicoRepo, userId)) {
                DadosAlerta dados{userId, consumo, strategy->obterMensagem(consumo), "agora"};
                if (historicoRepo) {
                    AlertaRecord rec{0, userId, consumo, dados.mensagem, dados.data};
                    historicoRepo->salvarAlerta(rec);
                }
                std::shared_lock<std::shared_mutex> lockObs(obsM);
                for (auto& obs : observadores) obs->atualizar(dados);
            }
        }
    }
};

// ==================== FACADE ====================
class FachadaSMH {
private:
    static FachadaSMH* instance;
    static std::mutex instanceMutex;
    
    std::shared_ptr<IUsuarioRepository> usuarioRepo;
    std::shared_ptr<IHistoricoRepository> historicoRepo;
    AlertaService alertaService;
    std::map<std::string, std::shared_ptr<ISimuladorAdapter>> simuladoresById;
    std::vector<std::shared_ptr<ISimuladorAdapter>> simuladoresFallback;
    std::shared_ptr<IOcrStrategy> ocrStrategy;
    mutable std::shared_mutex acessoM;

    FachadaSMH() : ocrStrategy(std::make_shared<FilenameOcrStrategy>()) {
        alertaService.setHistoricoRepository(historicoRepo);
    }

public:
    static FachadaSMH& getInstance() {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (!instance) instance = new FachadaSMH();
        return *instance;
    }

    void setRepository(std::shared_ptr<IUsuarioRepository> repo) {
        std::lock_guard<std::shared_mutex> lock(acessoM);
        usuarioRepo = repo;
    }
    void setHistoricoRepository(std::shared_ptr<IHistoricoRepository> repo) {
        std::lock_guard<std::shared_mutex> lock(acessoM);
        historicoRepo = repo;
        alertaService.setHistoricoRepository(repo);
    }
    void setOcrStrategy(std::shared_ptr<IOcrStrategy> st) {
        std::lock_guard<std::shared_mutex> lock(acessoM);
        ocrStrategy = st;
    }

    Usuario criarUsuario(const std::string& login, const std::string& senha, const std::string& email, Perfil perfil, const Token& token) {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        if (!token.valido() || token.perfil != Perfil::ADMIN) throw std::runtime_error("Acesso Negado: Requer Admin");
        if (!usuarioRepo) throw std::runtime_error("Repositorio Off");
        
        Usuario user;
        user.login = login;
        user.senhaHash = senha;
        user.email = email;
        user.perfil = perfil;
        return usuarioRepo->salvar(user);
    }

    std::vector<Usuario> listarTodosUsuarios(const Token& token) {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        if (!token.valido() || token.perfil != Perfil::ADMIN) throw std::runtime_error("Acesso Negado");
        return usuarioRepo ? usuarioRepo->listarTodosUsuarios() : std::vector<Usuario>{};
    }

    void deletarUsuario(int id, const Token& token) {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        if (!token.valido() || token.perfil != Perfil::ADMIN) throw std::runtime_error("Acesso Negado");
        if (usuarioRepo) usuarioRepo->deletar(id);
    }

    void vincularHidrometro(int uid, const std::string& sha, const Token& token) {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        
        // 1. Validação de Permissão
        if (!token.valido()) throw std::runtime_error("Acesso Negado");

        if (simuladoresById.find(sha) == simuladoresById.end()) {
            throw std::runtime_error("Erro: O Hidrometro '" + sha + "' nao foi detectado na rede/pasta.");
        }

        // 3. Validação: O Usuário existe?
        if (usuarioRepo) {
            auto usuarioExiste = usuarioRepo->buscarPorId(uid);
            if (!usuarioExiste.has_value()) {
                throw std::runtime_error("Erro: Nao existe usuario com ID " + std::to_string(uid));
            }

            // Se passou por todas as travas, executa.
            usuarioRepo->vincularHidrometro(uid, sha);
        }
    }

    void desvincularHidrometro(int uid, const std::string& sha, const Token& token) {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        // Regra de segurança: Apenas ADMIN pode desvincular
        if (!token.valido() || token.perfil != Perfil::ADMIN) {
            throw std::runtime_error("Acesso Negado: Apenas Admin pode desvincular");
        }
        if (usuarioRepo) {
            usuarioRepo->desvincularHidrometro(uid, sha);
        }
    }

    // Retorna a lista de todos os SHAs que o sistema detectou fisicamente
    std::vector<std::string> listarSimuladoresDetectados() {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        std::vector<std::string> lista;
        for (const auto& pair : simuladoresById) {
            lista.push_back(pair.first);
        }
        return lista;
    }

    void conectarSimulador(const std::map<std::string, std::string>& params, const Token& token) {
        std::lock_guard<std::shared_mutex> lock(acessoM);
        auto adapter = SimuladorFactory::criarAdapter(params);
        auto idSHA = params.find("idSHA");
        if (idSHA != params.end()) {
            simuladoresById[idSHA->second] = adapter;
        }
    }

    double obterLeituraAtual(const std::string& idSHA, const Token& token) {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        if (!token.valido()) throw std::runtime_error("Acesso negado");

        auto it = simuladoresById.find(idSHA);
        if (it == simuladoresById.end()) throw std::runtime_error("Simulador desconectado (Offline): " + idSHA);
        
        return ocrStrategy->extrairLeitura(it->second->obterCaminhoArquivoImagem());
    }

    void monitorarConsumo(int userId) {
        std::shared_lock<std::shared_mutex> lock(acessoM);
        if (!usuarioRepo) return;
        auto user = usuarioRepo->buscarPorId(userId);
        if (!user) return;

        auto composite = std::make_shared<UsuarioComposite>();
        for (const auto& sha : user->hidrometros) {
            auto it = simuladoresById.find(sha);
            if (it != simuladoresById.end()) {
                composite->adicionarComponente(std::make_shared<HidrometroLeaf>(
                    sha, it->second, ocrStrategy, historicoRepo, userId));
            }
        }
        double consumo = composite->obterConsumo();
        alertaService.verificarAlertas(userId, consumo);
    }

    void configurarRegraAlerta(int userId, const std::string& tipo, double valor) {
        std::lock_guard<std::shared_mutex> lock(acessoM);
        std::shared_ptr<IStrategiaAnalise> st;
        int extra = 0;
        if (tipo == "limite") st = std::make_shared<RegraLimiteFixo>(valor);
        else if (tipo == "media") { st = std::make_shared<RegraMediaMovel>((int)valor); extra = (int)valor; }
        
        if (st) {
            alertaService.adicionarRegra(userId, st);
            if (historicoRepo) historicoRepo->salvarRegra(userId, tipo, valor, extra);
        }
    }
    
    void registrarObservador(std::shared_ptr<IEventoObserver> obs) {
        std::lock_guard<std::shared_mutex> lock(acessoM);
        alertaService.registrarObservador(obs);
    }
};

FachadaSMH* FachadaSMH::instance = nullptr;
std::mutex FachadaSMH::instanceMutex;

// ==================== UI & MAIN ====================

void exibirMenu() {
    std::cout << "\n========================================\n";
    std::cout << "      PAINEL DE CONTROLE SMH (CLI)      \n";
    std::cout << "========================================\n";
    std::cout << "1. [CRUD] Criar Novo Usuario\n";
    std::cout << "2. [CRUD] Listar Usuarios (com Email)\n";
    std::cout << "3. [CRUD] Remover Usuario\n";
    std::cout << "4. [MONITOR] Ver SHAs Ativos e Status\n";
    std::cout << "5. [ADMIN] Vincular Hidrometro a Usuario\n";
    std::cout << "6. [ADMIN] Desvincular Hidrometro\n"; // <--- NOVO
    std::cout << "0. Sair\n";
    std::cout << "Escolha uma opcao: ";
}

int main() {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    fs::create_directories("./data");
    
    std::shared_ptr<IUsuarioRepository> usuarioRepo = std::make_shared<UsuarioRepositorySQLite>("./data/smh.db");
    std::shared_ptr<IHistoricoRepository> historicoRepo = std::make_shared<HistoricoRepositorySQLite>("./data/smh.db");

    auto& fachada = FachadaSMH::getInstance();
    fachada.setRepository(usuarioRepo);
    fachada.setHistoricoRepository(historicoRepo);
    fachada.setOcrStrategy(std::make_shared<FilenameOcrStrategy>());
    fachada.registrarObservador(std::make_shared<PainelObserver>());

    Token tokenAdmin;
    auto adminUser = usuarioRepo->buscarPorLogin("admin");
    if (!adminUser.has_value()) {
        Usuario admin{0, "admin", "admin", "admin@smh.local", Perfil::ADMIN, {}};
        auto u = usuarioRepo->salvar(admin);
        tokenAdmin = Token{u.id, Perfil::ADMIN};
        LogManager::getInstance().log("Usuario ADMIN criado.");
    } else {
        tokenAdmin = Token{adminUser->id, Perfil::ADMIN};
    }

    std::atomic<bool> running(true);
    std::thread monitorThread([&]() {
        std::string caminhoDoSHA = "C:/Users/Jefferson/Projetos/padroes_projetos/simulador-hidrometro-analogico-v2";

        while(running.load()) {
            try {
                if (fs::exists(caminhoDoSHA)) {
                    std::vector<fs::path> pastas;
                    for (const auto& entry : fs::directory_iterator(caminhoDoSHA)) {
                        if (entry.is_directory() && entry.path().filename().string().find("Medicoes_") == 0) {
                            pastas.push_back(entry.path());
                        }
                    }
                    if (!pastas.empty()) {
                        std::sort(pastas.begin(), pastas.end(), [](const fs::path& a, const fs::path& b) {
                            return fs::last_write_time(a) > fs::last_write_time(b);
                        });
                        std::string medicoesPath = pastas[0].string();

                        for (const auto& entry : fs::directory_iterator(medicoesPath)) {
                            if (entry.is_directory()) {
                                std::string nomeSHA = entry.path().filename().string();
                                std::map<std::string, std::string> params;
                                params["tipo"] = "arquivo";
                                params["caminho"] = entry.path().string();
                                params["idSHA"] = nomeSHA;
                                FachadaSMH::getInstance().conectarSimulador(params, Token{0, Perfil::ADMIN});
                            }
                        }
                    }
                }
            } catch (...) {}
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    });

    int opcao = -1;
    while (opcao != 0) {
        exibirMenu();
        if (!(std::cin >> opcao)) {
            std::cin.clear(); std::cin.ignore(10000, '\n'); continue;
        }

        try {
            switch(opcao) {
                case 1: {
                    std::string l, s, e; int p;
                    std::cout << "Login: "; std::cin >> l;
                    std::cout << "Senha: "; std::cin >> s;
                    std::cout << "Email: "; std::cin >> e;
                    std::cout << "Perfil (0=Admin, 1=Leitor): "; std::cin >> p;
                    auto u = fachada.criarUsuario(l, s, e, static_cast<Perfil>(p), tokenAdmin);
                    fachada.configurarRegraAlerta(u.id, "limite", 50.0);
                    std::cout << ">> Usuario criado (ID " << u.id << ")\n";
                    break;
                }
                case 2: {
                    auto users = fachada.listarTodosUsuarios(tokenAdmin);
                    std::cout << "\n--- USUARIOS ---\n";
                    for(const auto& u : users) {
                        std::cout << "ID: " << u.id << " | " << u.login << " (" << u.email << ")\n";
                    }
                    break;
                }
                case 3: {
                    int id; std::cout << "ID: "; std::cin >> id;
                    fachada.deletarUsuario(id, tokenAdmin);
                    std::cout << ">> Removido.\n";
                    break;
                }
                case 4: { // STATUS REAL-TIME + NÃO VINCULADOS
                    auto users = fachada.listarTodosUsuarios(tokenAdmin);
                    // Pega todos os SHAs que a thread achou na pasta fisica
                    auto detectados = fachada.listarSimuladoresDetectados();
                    std::vector<std::string> shasVinculados; // Para controle

                    std::cout << "\n==============================================================\n";
                    std::cout << "               STATUS DOS HIDROMETROS (REAL-TIME)             \n";
                    std::cout << "==============================================================\n";
                    
                    // 1. MOSTRA OS VINCULADOS
                    bool temVinculo = false;
                    for (const auto& u : users) {
                        fachada.monitorarConsumo(u.id); 
                        for (const auto& sha : u.hidrometros) {
                            temVinculo = true;
                            shasVinculados.push_back(sha); // Marca como usado

                            std::cout << "SHA: " << std::left << std::setw(15) << sha 
                                      << " | Dono: " << std::left << std::setw(10) << u.login;
                            try {
                                double valor = fachada.obterLeituraAtual(sha, tokenAdmin);
                                std::cout << " | [ONLINE]  (Leitura: " << valor << ")\n";
                            } catch (...) {
                                std::cout << " | [OFFLINE] (Pasta nao encontrada)\n";
                            }
                        }
                    }

                    if (!temVinculo) std::cout << "   (Nenhum hidrometro vinculado a usuarios)\n";
                    std::cout << "--------------------------------------------------------------\n";

                    // 2. MOSTRA OS DISPONÍVEIS (SEM DONO)
                    std::cout << "   HIDROMETROS DISPONIVEIS (SEM VINCULO):\n";
                    bool temDisponivel = false;
                    for (const auto& shaFisico : detectados) {
                        // Se não estiver na lista de vinculados, mostra aqui
                        bool jaMostrado = false;
                        for(const auto& vinculado : shasVinculados) {
                            if(vinculado == shaFisico) jaMostrado = true;
                        }

                        if (!jaMostrado) {
                            temDisponivel = true;
                            // Se está na lista de detectados, com certeza está ONLINE
                            try {
                                double valor = fachada.obterLeituraAtual(shaFisico, tokenAdmin);
                                std::cout << "SHA: " << std::left << std::setw(15) << shaFisico 
                                          << " | [LIVRE]   (Leitura: " << valor << ")\n";
                            } catch(...) {}
                        }
                    }
                    if (!temDisponivel) std::cout << "   (Nenhum hidrometro extra encontrado na pasta)\n";

                    std::cout << "==============================================================\n";
                    break;
                }
                case 5: {
                    int uid; std::string sha;
                    std::cout << "ID User: "; std::cin >> uid;
                    std::cout << "Nome SHA (ex: hidrometro1): "; std::cin >> sha;
                    fachada.vincularHidrometro(uid, sha, tokenAdmin);
                    std::cout << ">> Vinculado!\n";
                    break;
                }

                case 6: { // DESVINCULAR
                    int uid; std::string sha;
                    std::cout << "--- DESVINCULAR HIDROMETRO ---\n";
                    std::cout << "ID do Usuario dono: "; std::cin >> uid;
                    std::cout << "Nome do SHA para remover (ex: hidrometro1): "; std::cin >> sha;
                    
                    fachada.desvincularHidrometro(uid, sha, tokenAdmin);
                    
                    std::cout << ">> Hidrometro desvinculado com sucesso (se existia)!\n";
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cout << "ERRO: " << e.what() << "\n";
        }
    }

    running.store(false);
    if (monitorThread.joinable()) monitorThread.join();
    return 0;
}