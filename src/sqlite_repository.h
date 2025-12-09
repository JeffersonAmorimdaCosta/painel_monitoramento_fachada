#ifndef SQLITE_REPOSITORY_H
#define SQLITE_REPOSITORY_H

#include "core.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

class UsuarioRepositorySQLite : public IUsuarioRepository {
private:
    sqlite3* db = nullptr;
    std::string dbPath;
    mutable std::mutex dbMutex;

    void initSchema();
    std::optional<Usuario> carregarUsuarioComHidrometros(int id);

public:
    explicit UsuarioRepositorySQLite(const std::string& path);
    ~UsuarioRepositorySQLite();

    Usuario salvar(const Usuario& user) override;
    std::optional<Usuario> buscarPorLogin(const std::string& login) override;
    std::optional<Usuario> buscarPorId(int id) override;
    void deletar(int id) override;
    void vincularHidrometro(int userId, const std::string& idSHA) override;
    void desvincularHidrometro(int userId, const std::string& idSHA) override;
    std::vector<Usuario> listarTodosUsuarios() override;
};

class HistoricoRepositorySQLite : public IHistoricoRepository {
private:
    sqlite3* db = nullptr;
    std::string dbPath;
    mutable std::mutex dbMutex;

    void initSchema();

public:
    explicit HistoricoRepositorySQLite(const std::string& path);
    ~HistoricoRepositorySQLite();

    void salvarLeitura(const Leitura& leitura) override;
    void salvarAlerta(const AlertaRecord& alerta) override;
    std::vector<AlertaRecord> listarAlertasPorUsuario(int userId) override;
    int salvarRegra(int userId, const std::string& tipo, double valor, int extra = 0) override;
    std::vector<std::tuple<int, int, std::string, double, int>> listarRegrasPorUsuario(int userId) override;
    std::vector<Leitura> listarLeiturasPorUsuario(int userId, int limit = 10) override;
};

#endif // SQLITE_REPOSITORY_H
