#ifndef CORE_H
#define CORE_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <variant>

// ==================== ENUMS ====================
enum class Perfil { ADMIN, LEITOR };

// ==================== STRUCTS ====================
struct Token {
    int userId;
    Perfil perfil;
    bool valido() const { return userId > 0; }
};

struct Usuario {
    int id = 0;
    std::string login;
    std::string senhaHash;
    std::string email;
    Perfil perfil = Perfil::LEITOR;
    std::vector<std::string> hidrometros; 
};

struct Leitura {
    int id = 0;
    int userId = 0;
    std::string idSHA;
    std::string data; // ISO8601 com millisegundos
    double valor = 0.0;
    std::string caminhoImagem;
};

struct AlertaRecord {
    int id = 0;
    int userId = 0;
    double consumo = 0.0;
    std::string mensagem;
    std::string data; // ISO8601
};

struct DadosAlerta {
    int userId = 0;
    double consumo = 0.0;
    std::string mensagem;
    std::string data;
};

// ==================== INTERFACES ====================

class IUsuarioRepository {
public:
    virtual ~IUsuarioRepository() = default;
    virtual Usuario salvar(const Usuario& user) = 0;
    virtual std::optional<Usuario> buscarPorLogin(const std::string& login) = 0;
    virtual std::optional<Usuario> buscarPorId(int id) = 0;
    virtual void deletar(int id) = 0;
    virtual void vincularHidrometro(int userId, const std::string& idSHA) = 0;
    virtual void desvincularHidrometro(int userId, const std::string& idSHA) = 0;
    virtual std::vector<Usuario> listarTodosUsuarios() = 0;
};

class IHistoricoRepository {
public:
    virtual ~IHistoricoRepository() = default;
    virtual void salvarLeitura(const Leitura& leitura) = 0;
    virtual void salvarAlerta(const AlertaRecord& alerta) = 0;
    virtual std::vector<AlertaRecord> listarAlertasPorUsuario(int userId) = 0;
    virtual int salvarRegra(int userId, const std::string& tipo, double valor, int extra = 0) = 0;
    virtual std::vector<std::tuple<int, int, std::string, double, int>> listarRegrasPorUsuario(int userId) = 0;
    virtual std::vector<Leitura> listarLeiturasPorUsuario(int userId, int limit = 10) = 0;
};

class IEventoObserver {
public:
    virtual ~IEventoObserver() = default;
    virtual void atualizar(const DadosAlerta& dados) = 0;
};

#endif // CORE_H
