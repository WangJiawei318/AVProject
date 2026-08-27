#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <stdint.h>
#include <string>

#include "DatabaseConnectionPool.h"

struct AuthResult
{
    AuthResult();

    bool success;
    int32_t errorCode;
    uint64_t userId;
    std::string username;
    std::string message;
};

class AuthService
{
public:
    explicit AuthService(DatabaseConnectionPool &pool);

    bool initialize(std::string *error);
    AuthResult registerUser(const std::string &username,
                            const std::string &password);
    AuthResult login(const std::string &username,
                     const std::string &password);

private:
    bool validCredentials(const std::string &username,
                          const std::string &password) const;
    bool findUser(MYSQL *connection,
                  const std::string &username,
                  bool *found,
                  uint64_t *userId,
                  std::string *passwordHash,
                  int *status,
                  std::string *error) const;

    DatabaseConnectionPool &m_pool;
};

#endif // AUTHSERVICE_H

