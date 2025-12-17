#ifndef SMTP_EMAIL_H
#define SMTP_EMAIL_H

#include "core.h"
#include <string>
#include <memory>

class SmtpEmailService : public IEventoObserver {
public:
    enum class SecureMode { NONE, STARTTLS, SMTPS };

    SmtpEmailService(const std::string& server, int p, const std::string& fromAddr,
                     const std::string& toAddr, const std::string& u, const std::string& pass, SecureMode mode, int targetUid);

    void atualizar(const DadosAlerta& dados) override;

private:
    std::string smtpServer;
    int port;
    std::string from;
    std::string to;
    std::string user;
    std::string password;
    int targetUserId; // ID do usuario dono deste email
    SecureMode secure;
};

#endif // SMTP_EMAIL_H
