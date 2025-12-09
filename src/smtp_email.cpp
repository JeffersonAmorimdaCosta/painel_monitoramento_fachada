#include "smtp_email.h"
#include <iostream>
#include <sstream>

#ifdef USE_CURL
#include <curl/curl.h>
#endif

SmtpEmailService::SmtpEmailService(const std::string& server, int p, const std::string& fromAddr,
                                   const std::string& toAddr, const std::string& u, const std::string& pass)
    : smtpServer(server), port(p), from(fromAddr), to(toAddr), user(u), password(pass) {}

void SmtpEmailService::atualizar(const DadosAlerta& dados) {
    // Log do alerta (sempre)
    std::cout << "[EmailService] Alerta: User " << dados.userId << " | Consumo: " << dados.consumo
              << " | Msg: " << dados.mensagem << " | Data: " << dados.data << std::endl;

#ifdef USE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[EmailService] Erro ao inicializar CURL" << std::endl;
        return;
    }

    std::string url = "smtp://" + smtpServer + ":" + std::to_string(port);
    std::string payload = "To: " + to + "\r\nFrom: " + from + "\r\nSubject: Alerta SMH\r\n\r\n";
    payload += "ALERTA: Consumo de " + std::to_string(dados.consumo) + " detectado.\n";
    payload += "Mensagem: " + dados.mensagem + "\n";
    payload += "Data: " + dados.data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, to.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

    if (!user.empty() && !password.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERPWD, (user + ":" + password).c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "[EmailService] Erro ao enviar email: " << curl_easy_strerror(res) << std::endl;
    } else {
        std::cout << "[EmailService] Email enviado com sucesso!" << std::endl;
    }

    curl_easy_cleanup(curl);
#else
    std::cout << "[EmailService] CURL não disponível - email não enviado" << std::endl;
#endif
}
