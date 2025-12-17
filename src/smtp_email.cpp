#include "smtp_email.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstring>

#ifdef USE_CURL
#include <curl/curl.h>
#endif

SmtpEmailService::SmtpEmailService(const std::string& server, int p, const std::string& fromAddr,
                                   const std::string& toAddr, const std::string& u, const std::string& pass, SecureMode mode, int targetUid)
    : smtpServer(server), port(p), from(fromAddr), to(toAddr), user(u), password(pass), secure(mode), targetUserId(targetUid) {
        // std::cout << "[DEBUG] SmtpEmailService registrado para User ID: " << targetUserId << std::endl;
    }

// Estrutura para controlar o upload do texto
struct UploadStatus {
    const char* data;
    size_t len;
};

// Função que o CURL chama pedacinho por pedacinho para ler o e-mail
static size_t payload_source(char* ptr, size_t size, size_t nmemb, void* userp) {
    UploadStatus* upload = (UploadStatus*)userp;
    size_t max_copy = size * nmemb;

    if (upload->len < 1) return 0;

    if (upload->len < max_copy) max_copy = upload->len;

    memcpy(ptr, upload->data, max_copy);
    upload->data += max_copy;
    upload->len -= max_copy;

    return max_copy;
}

void SmtpEmailService::atualizar(const DadosAlerta& dados) {
    if (dados.userId != targetUserId) {
        // std::cout << "[DEBUG] Ignorando alerta do User " << dados.userId << " (Sou User " << targetUserId << ")" << std::endl;
        return;
    }

#ifdef USE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[EmailService] Erro ao inicializar CURL" << std::endl;
        return;
    }

    std::stringstream ssConsumo;
    ssConsumo << std::fixed << std::setprecision(2) << dados.consumo * 1.047821;
    std::string consumoFormatado = ssConsumo.str();

    std::string scheme = (secure == SecureMode::SMTPS) ? "smtps://" : "smtp://";
    std::string url = scheme + smtpServer + ":" + std::to_string(port);
    // Pega data formatada para email (RFC 2822)
    std::time_t now = std::time(nullptr);
    char dateStr[100];
    std::strftime(dateStr, sizeof(dateStr), "%a, %d %b %Y %H:%M:%S +0000", std::gmtime(&now));

    std::string payload = "Date: " + std::string(dateStr) + "\r\n";
    payload += "To: " + to + "\r\n";
    payload += "From: " + from + "\r\n";
    payload += "Subject: Alerta SMH - Consumo Alto\r\n";
    payload += "Message-ID: <" + std::to_string(now) + "@smh.local>\r\n";

    payload += "MIME-Version: 1.0\r\n";
    payload += "Content-Type: text/html; charset=UTF-8\r\n";

    payload += "\r\n"; // Linha em branco obrigatória entre header e corpo


    // --- CORPO DO EMAIL (HTML) ---
    payload += "<html><body style='font-family: Arial, sans-serif;'>";
    payload += "<div style='border: 1px solid #ccc; padding: 20px; border-radius: 10px;'>";
    payload += "<h2 style='color: #d9534f;'>⚠️ Alerta de Consumo Crítico</h2>";
    
    payload += "<p>O sistema detectou um consumo elevado.</p>";
    
    payload += "<table style='width: 100%; border-collapse: collapse; margin-top: 15px;'>";
    payload += "  <tr style='background-color: #f2f2f2;'>";
    payload += "    <td style='padding: 10px; border: 1px solid #ddd;'><b>Nome do Usuário</b></td>";
    // Mostra o Nome (Login)
    payload += "    <td style='padding: 10px; border: 1px solid #ddd;'>" + dados.nomeUser + "</td>";
    payload += "  </tr>";
    payload += "  <tr>";
    payload += "  <tr style='background-color: #f2f2f2;'>";
    payload += "    <td style='padding: 10px; border: 1px solid #ddd;'><b>Id do Usuário</b></td>";
    payload += "    <td style='padding: 10px; border: 1px solid #ddd;'>" + std::to_string(dados.userId) + "</td>";
    payload += "  </tr>";
    payload += "  <tr>";
    payload += "    <td style='padding: 10px; border: 1px solid #ddd;'><b>Consumo Medido</b></td>";
    // Mostra o Consumo Formatado
    payload += "    <td style='padding: 10px; border: 1px solid #ddd; color: red; font-weight: bold; font-size: 18px;'>" + consumoFormatado + " m³</td>";
    payload += "  </tr>";
    payload += "  <tr style='background-color: #f2f2f2;'>";
    payload += "    <td style='padding: 10px; border: 1px solid #ddd;'><b>Data/Hora</b></td>";
    payload += "    <td style='padding: 10px; border: 1px solid #ddd;'>" + dados.data + "</td>";
    payload += "  </tr>";
    payload += "</table>";
    
    payload += "<br><p style='color: #777; font-size: 12px;'>Este é um e-mail automático do Sistema de Monitoramento Hídrico (SMH).</p>";
    payload += "</div></body></html>";
    payload += "\r\n";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());
    struct curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, to.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    // Prepara os dados para envio
    UploadStatus upload_ctx;
    upload_ctx.data = payload.c_str();
    upload_ctx.len = payload.length();

    // Configura a função de leitura que criamos acima
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
    
    // ATIVA O MODO DE ENVIO (Isso impede o VRFY)
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    if (secure == SecureMode::STARTTLS) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    }

    if (!user.empty() && !password.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERPWD, (user + ":" + password).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (recipients) curl_slist_free_all(recipients);
    if (res != CURLE_OK) {
        std::cerr << "[EmailService] Erro ao enviar email: " << curl_easy_strerror(res) << std::endl;
        // Fallback: salva em disco
        curl_easy_cleanup(curl);
        goto fallback_file;
    } else {
        std::cout << "[EmailService] Email enviado com sucesso!" << std::endl;
    }

    curl_easy_cleanup(curl);
#else
    std::cout << "[EmailService] CURL não disponível - email não enviado" << std::endl;
    goto fallback_file;
#endif
    return;

fallback_file:
    try {
        namespace fs = std::filesystem;
        fs::create_directories("./data/email_outbox");
        std::ostringstream fn;
        fn << "./data/email_outbox/alert_user" << dados.userId << "_" << std::chrono::system_clock::now().time_since_epoch().count() << ".eml";
        std::ofstream out(fn.str(), std::ios::out | std::ios::trunc);
        if (out) {
            out << "To: " << to << "\r\n";
            out << "From: " << from << "\r\n";
            out << "Subject: Alerta SMH\r\n\r\n";
            out << "ALERTA: Consumo de " << dados.consumo << " detectado.\n";
            out << "Mensagem: " << dados.mensagem << "\n";
            out << "Data: " << dados.data << "\n";
            out.close();
            std::cout << "[EmailService] (fallback) Email salvo em arquivo: " << fn.str() << std::endl;
        } else {
            std::cerr << "[EmailService] (fallback) Falha ao salvar email em arquivo" << std::endl;
        }
    } catch (const std::exception& ex) {
        std::cerr << "[EmailService] (fallback) Erro ao salvar email: " << ex.what() << std::endl;
    }
}
