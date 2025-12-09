#ifndef SMTP_EMAIL_H
#define SMTP_EMAIL_H

#include "core.h"
#include <string>
#include <memory>

class SmtpEmailService : public IEventoObserver {
private:
    std::string smtpServer;
    int port;
    std::string from;
    std::string to;
    std::string user;
    std::string password;

public:
    SmtpEmailService(const std::string& server, int p, const std::string& fromAddr,
                     const std::string& toAddr, const std::string& u, const std::string& pass);
    ~SmtpEmailService() = default;

    void atualizar(const DadosAlerta& dados) override;
};

#endif // SMTP_EMAIL_H
