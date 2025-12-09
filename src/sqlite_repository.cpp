#include "sqlite_repository.h"
#include <iostream>
#include <stdexcept>
#include <sstream>

// ==================== UsuarioRepositorySQLite ====================

UsuarioRepositorySQLite::UsuarioRepositorySQLite(const std::string& path) : dbPath(path) {
    int ret = sqlite3_open(path.c_str(), &db);
    if (ret != SQLITE_OK) {
        throw std::runtime_error("Não foi possível abrir banco: " + path);
    }
    initSchema();
}

UsuarioRepositorySQLite::~UsuarioRepositorySQLite() {
    if (db) sqlite3_close(db);
}

void UsuarioRepositorySQLite::initSchema() {
    std::lock_guard<std::mutex> lock(dbMutex);
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS TB_USUARIO (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            login TEXT UNIQUE NOT NULL,
            senhaHash TEXT NOT NULL,
            email TEXT,
            perfil INTEGER NOT NULL
        );
        
        CREATE TABLE IF NOT EXISTS TB_VINCULO (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            idSHA TEXT NOT NULL,
            FOREIGN KEY(user_id) REFERENCES TB_USUARIO(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS TB_LEITURAS (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            idSHA TEXT NOT NULL,
            data TEXT NOT NULL,
            valor REAL NOT NULL,
            caminhoImagem TEXT,
            FOREIGN KEY(user_id) REFERENCES TB_USUARIO(id)
        );
        CREATE TABLE IF NOT EXISTS TB_ALERTAS (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            consumo REAL NOT NULL,
            mensagem TEXT NOT NULL,
            data TEXT NOT NULL,
            FOREIGN KEY(user_id) REFERENCES TB_USUARIO(id)
        );
        CREATE TABLE IF NOT EXISTS TB_REGRAS (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            tipo TEXT NOT NULL,
            valor REAL NOT NULL,
            extra INTEGER,
            FOREIGN KEY(user_id) REFERENCES TB_USUARIO(id)
        );
    )";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("Erro ao criar schema: " + error);
    }
}

std::optional<Usuario> UsuarioRepositorySQLite::carregarUsuarioComHidrometros(int id) {
    std::string query = "SELECT id, login, senhaHash, perfil, email FROM TB_USUARIO WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    Usuario user;
    user.id = sqlite3_column_int(stmt, 0);
    user.login = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    user.senhaHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    user.perfil = static_cast<Perfil>(sqlite3_column_int(stmt, 3));

    const char* emailText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    user.email = emailText ? emailText : "";

    sqlite3_finalize(stmt);

    // Carregar hidrometros
    query = "SELECT idSHA FROM TB_VINCULO WHERE user_id = ?";
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            user.hidrometros.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    return user;
}

Usuario UsuarioRepositorySQLite::salvar(const Usuario& user) {
    std::lock_guard<std::mutex> lock(dbMutex);
    Usuario result = user;

    if (user.id == 0) {
        // INSERT
        std::string query = "INSERT INTO TB_USUARIO (login, senhaHash, perfil, email) VALUES (?, ?, ?, ?)";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Erro ao preparar INSERT usuario");
        }
        sqlite3_bind_text(stmt, 1, user.login.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, user.senhaHash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, static_cast<int>(user.perfil));
        sqlite3_bind_text(stmt, 4, user.email.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Erro ao inserir usuario");
        }
        sqlite3_finalize(stmt);
        result.id = static_cast<int>(sqlite3_last_insert_rowid(db));
    } else {
        // UPDATE
        std::string query = "UPDATE TB_USUARIO SET senhaHash = ?, perfil = ?, email = ? WHERE id = ?";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Erro ao preparar UPDATE usuario");
        }
        sqlite3_bind_text(stmt, 1, user.senhaHash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, static_cast<int>(user.perfil));
        sqlite3_bind_int(stmt, 3, user.id);
        sqlite3_bind_text(stmt, 3, user.email.c_str(), -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Erro ao atualizar usuario");
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

std::optional<Usuario> UsuarioRepositorySQLite::buscarPorLogin(const std::string& login) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string query = "SELECT id FROM TB_USUARIO WHERE login = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    int id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return carregarUsuarioComHidrometros(id);
}

std::optional<Usuario> UsuarioRepositorySQLite::buscarPorId(int id) {
    std::lock_guard<std::mutex> lock(dbMutex);
    return carregarUsuarioComHidrometros(id);
}

void UsuarioRepositorySQLite::deletar(int id) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string query = "DELETE FROM TB_USUARIO WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void UsuarioRepositorySQLite::vincularHidrometro(int userId, const std::string& idSHA) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string query = "INSERT INTO TB_VINCULO (user_id, idSHA) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Erro ao preparar vinculo");
    }
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, idSHA.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Erro ao vincular hidrometro");
    }
    sqlite3_finalize(stmt);
}

void UsuarioRepositorySQLite::desvincularHidrometro(int userId, const std::string& idSHA) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string query = "DELETE FROM TB_VINCULO WHERE user_id = ? AND idSHA = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Erro ao preparar desvincular");
    }
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, idSHA.c_str(), -1, SQLITE_STATIC);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<Usuario> UsuarioRepositorySQLite::listarTodosUsuarios() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Usuario> usuarios;
    std::string query = "SELECT id FROM TB_USUARIO";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            if (auto user = carregarUsuarioComHidrometros(id)) {
                usuarios.push_back(*user);
            }
        }
        sqlite3_finalize(stmt);
    }
    return usuarios;
}

// ==================== HistoricoRepositorySQLite ====================

HistoricoRepositorySQLite::HistoricoRepositorySQLite(const std::string& path) : dbPath(path) {
    int ret = sqlite3_open(path.c_str(), &db);
    if (ret != SQLITE_OK) {
        throw std::runtime_error("Não foi possível abrir banco: " + path);
    }
    initSchema();
}

HistoricoRepositorySQLite::~HistoricoRepositorySQLite() {
    if (db) sqlite3_close(db);
}

void HistoricoRepositorySQLite::initSchema() {
    std::lock_guard<std::mutex> lock(dbMutex);
    // Schema já foi criado por UsuarioRepositorySQLite
}

void HistoricoRepositorySQLite::salvarLeitura(const Leitura& leitura) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string query = "INSERT INTO TB_LEITURAS (user_id, idSHA, data, valor, caminhoImagem) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Erro ao preparar salvar leitura");
    }
    sqlite3_bind_int(stmt, 1, leitura.userId);
    sqlite3_bind_text(stmt, 2, leitura.idSHA.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, leitura.data.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 4, leitura.valor);
    sqlite3_bind_text(stmt, 5, leitura.caminhoImagem.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Erro ao inserir leitura");
    }
    sqlite3_finalize(stmt);
}

void HistoricoRepositorySQLite::salvarAlerta(const AlertaRecord& alerta) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string query = "INSERT INTO TB_ALERTAS (user_id, consumo, mensagem, data) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Erro ao preparar salvar alerta");
    }
    sqlite3_bind_int(stmt, 1, alerta.userId);
    sqlite3_bind_double(stmt, 2, alerta.consumo);
    sqlite3_bind_text(stmt, 3, alerta.mensagem.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, alerta.data.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Erro ao inserir alerta");
    }
    sqlite3_finalize(stmt);
}

std::vector<AlertaRecord> HistoricoRepositorySQLite::listarAlertasPorUsuario(int userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<AlertaRecord> alertas;
    std::string query = "SELECT id, user_id, consumo, mensagem, data FROM TB_ALERTAS WHERE user_id = ? ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, userId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AlertaRecord alerta;
            alerta.id = sqlite3_column_int(stmt, 0);
            alerta.userId = sqlite3_column_int(stmt, 1);
            alerta.consumo = sqlite3_column_double(stmt, 2);
            alerta.mensagem = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            alerta.data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            alertas.push_back(alerta);
        }
        sqlite3_finalize(stmt);
    }
    return alertas;
}

int HistoricoRepositorySQLite::salvarRegra(int userId, const std::string& tipo, double valor, int extra) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string query = "INSERT INTO TB_REGRAS (user_id, tipo, valor, extra) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Erro ao preparar salvar regra");
    }
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, tipo.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, valor);
    sqlite3_bind_int(stmt, 4, extra);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Erro ao inserir regra");
    }
    sqlite3_finalize(stmt);
    int ruleId = static_cast<int>(sqlite3_last_insert_rowid(db));
    return ruleId;
}

std::vector<std::tuple<int, int, std::string, double, int>> HistoricoRepositorySQLite::listarRegrasPorUsuario(int userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::tuple<int, int, std::string, double, int>> regras;
    std::string query = "SELECT id, user_id, tipo, valor, extra FROM TB_REGRAS WHERE user_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, userId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            int uid = sqlite3_column_int(stmt, 1);
            std::string tipo(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            double valor = sqlite3_column_double(stmt, 3);
            int extra = sqlite3_column_int(stmt, 4);
            regras.emplace_back(id, uid, tipo, valor, extra);
        }
        sqlite3_finalize(stmt);
    }
    return regras;
}

std::vector<Leitura> HistoricoRepositorySQLite::listarLeiturasPorUsuario(int userId, int limit) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Leitura> leituras;
    std::string query = "SELECT id, user_id, idSHA, data, valor, caminhoImagem FROM TB_LEITURAS WHERE user_id = ? ORDER BY id DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, userId);
        sqlite3_bind_int(stmt, 2, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Leitura leitura;
            leitura.id = sqlite3_column_int(stmt, 0);
            leitura.userId = sqlite3_column_int(stmt, 1);
            leitura.idSHA = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            leitura.data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            leitura.valor = sqlite3_column_double(stmt, 4);
            leitura.caminhoImagem = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            leituras.push_back(leitura);
        }
        sqlite3_finalize(stmt);
    }
    return leituras;
}
