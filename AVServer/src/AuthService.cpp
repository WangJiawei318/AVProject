#include "AuthService.h"

#include "av_protocol.h"

#include <mysql/mysql.h>
#include <sodium.h>

#include <cstring>
#include <memory>

namespace {

struct StatementCloser
{
    void operator()(MYSQL_STMT *statement) const
    {
        if (statement)
            mysql_stmt_close(statement);
    }
};

typedef std::unique_ptr<MYSQL_STMT, StatementCloser> StatementPtr;

StatementPtr prepare(MYSQL *connection, const char *sql, std::string *error)
{
    StatementPtr statement(mysql_stmt_init(connection));
    if (!statement) {
        *error = "database statement allocation failed";
        return StatementPtr();
    }
    if (mysql_stmt_prepare(statement.get(), sql,
                           static_cast<unsigned long>(std::strlen(sql))) != 0) {
        *error = "database statement preparation failed";
        return StatementPtr();
    }
    return statement;
}

} // namespace

AuthResult::AuthResult()
    : success(false), errorCode(AV_ERROR_INVALID_REQUEST), userId(0)
{
}

AuthService::AuthService(DatabaseConnectionPool &pool)
    : m_pool(pool)
{
}

bool AuthService::initialize(std::string *error)
{
    if (sodium_init() < 0) {
        if (error)
            *error = "failed to initialize libsodium";
        return false;
    }
    return true;
}

AuthResult AuthService::registerUser(const std::string &username,
                                     const std::string &password)
{
    AuthResult result;
    if (!validCredentials(username, password)) {
        result.errorCode = AV_ERROR_INVALID_REQUEST;
        result.message = "username must be 3-32 bytes and password 6-64 bytes";
        return result;
    }

    std::string poolError;
    DatabaseConnectionPool::ConnectionLease lease = m_pool.acquire(&poolError);
    if (!lease) {
        result.errorCode = AV_ERROR_DATABASE_UNAVAILABLE;
        result.message = "database temporarily unavailable";
        return result;
    }

    bool found = false;
    uint64_t existingId = 0;
    std::string ignoredHash;
    int ignoredStatus = 0;
    std::string queryError;
    if (!findUser(lease.get(), username, &found, &existingId,
                  &ignoredHash, &ignoredStatus, &queryError)) {
        result.errorCode = AV_ERROR_DATABASE_UNAVAILABLE;
        result.message = "database temporarily unavailable";
        return result;
    }
    if (found) {
        result.errorCode = AV_ERROR_USERNAME_EXISTS;
        result.message = "username already exists";
        return result;
    }

    char passwordHash[crypto_pwhash_STRBYTES] = {0};
    if (crypto_pwhash_str(passwordHash,
                          password.data(),
                          static_cast<unsigned long long>(password.size()),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        result.errorCode = AV_ERROR_INTERNAL;
        result.message = "password hashing failed";
        return result;
    }

    std::string statementError;
    StatementPtr statement = prepare(
                lease.get(),
                "INSERT INTO users(username,password_hash,status) VALUES(?,?,1)",
                &statementError);
    if (!statement) {
        sodium_memzero(passwordHash, sizeof(passwordHash));
        result.errorCode = AV_ERROR_DATABASE_UNAVAILABLE;
        result.message = "database temporarily unavailable";
        return result;
    }

    MYSQL_BIND parameters[2];
    std::memset(parameters, 0, sizeof(parameters));
    unsigned long usernameLength = static_cast<unsigned long>(username.size());
    unsigned long hashLength = static_cast<unsigned long>(std::strlen(passwordHash));
    parameters[0].buffer_type = MYSQL_TYPE_STRING;
    parameters[0].buffer = const_cast<char *>(username.data());
    parameters[0].buffer_length = usernameLength;
    parameters[0].length = &usernameLength;
    parameters[1].buffer_type = MYSQL_TYPE_STRING;
    parameters[1].buffer = passwordHash;
    parameters[1].buffer_length = sizeof(passwordHash);
    parameters[1].length = &hashLength;

    const bool bindOk = mysql_stmt_bind_param(statement.get(), parameters) == 0;
    const bool executeOk = bindOk && mysql_stmt_execute(statement.get()) == 0;
    const unsigned int statementErrorCode = mysql_stmt_errno(statement.get());
    sodium_memzero(passwordHash, sizeof(passwordHash));
    if (!executeOk) {
        result.errorCode = statementErrorCode == 1062
                ? AV_ERROR_USERNAME_EXISTS : AV_ERROR_DATABASE_UNAVAILABLE;
        result.message = statementErrorCode == 1062
                ? "username already exists" : "database temporarily unavailable";
        return result;
    }

    result.success = true;
    result.errorCode = AV_ERROR_SUCCESS;
    result.userId = static_cast<uint64_t>(mysql_stmt_insert_id(statement.get()));
    result.username = username;
    result.message = "registration successful; please log in";
    return result;
}

AuthResult AuthService::login(const std::string &username,
                              const std::string &password)
{
    AuthResult result;
    if (!validCredentials(username, password)) {
        result.errorCode = AV_ERROR_INVALID_USERNAME_OR_PASSWORD;
        result.message = "invalid username or password";
        return result;
    }

    std::string poolError;
    DatabaseConnectionPool::ConnectionLease lease = m_pool.acquire(&poolError);
    if (!lease) {
        result.errorCode = AV_ERROR_DATABASE_UNAVAILABLE;
        result.message = "database temporarily unavailable";
        return result;
    }

    bool found = false;
    uint64_t userId = 0;
    std::string passwordHash;
    int status = 0;
    std::string queryError;
    if (!findUser(lease.get(), username, &found, &userId,
                  &passwordHash, &status, &queryError)) {
        result.errorCode = AV_ERROR_DATABASE_UNAVAILABLE;
        result.message = "database temporarily unavailable";
        return result;
    }
    if (!found || crypto_pwhash_str_verify(passwordHash.c_str(),
                                            password.data(),
                                            static_cast<unsigned long long>(password.size())) != 0) {
        result.errorCode = AV_ERROR_INVALID_USERNAME_OR_PASSWORD;
        result.message = "invalid username or password";
        return result;
    }
    if (status != 1) {
        result.errorCode = AV_ERROR_USER_DISABLED;
        result.message = "user is disabled";
        return result;
    }

    result.success = true;
    result.errorCode = AV_ERROR_SUCCESS;
    result.userId = userId;
    result.username = username;
    result.message = "login successful";
    return result;
}

bool AuthService::validCredentials(const std::string &username,
                                   const std::string &password) const
{
    if (username.size() < 3 || username.size() > 32 ||
            password.size() < 6 || password.size() > 64)
        return false;
    return username.find('\0') == std::string::npos &&
            password.find('\0') == std::string::npos;
}

bool AuthService::findUser(MYSQL *connection,
                           const std::string &username,
                           bool *found,
                           uint64_t *userId,
                           std::string *passwordHash,
                           int *status,
                           std::string *error) const
{
    *found = false;
    StatementPtr statement = prepare(
                connection,
                "SELECT id,password_hash,status FROM users WHERE username=? LIMIT 1",
                error);
    if (!statement)
        return false;

    MYSQL_BIND parameter;
    std::memset(&parameter, 0, sizeof(parameter));
    unsigned long usernameLength = static_cast<unsigned long>(username.size());
    parameter.buffer_type = MYSQL_TYPE_STRING;
    parameter.buffer = const_cast<char *>(username.data());
    parameter.buffer_length = usernameLength;
    parameter.length = &usernameLength;
    if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 ||
            mysql_stmt_execute(statement.get()) != 0) {
        *error = "database user query failed";
        return false;
    }

    unsigned long long databaseUserId = 0;
    char hashBuffer[256] = {0};
    unsigned long hashLength = 0;
    signed char databaseStatus = 0;
    MYSQL_BIND output[3];
    std::memset(output, 0, sizeof(output));
    output[0].buffer_type = MYSQL_TYPE_LONGLONG;
    output[0].buffer = &databaseUserId;
    output[0].is_unsigned = 1;
    output[1].buffer_type = MYSQL_TYPE_STRING;
    output[1].buffer = hashBuffer;
    output[1].buffer_length = sizeof(hashBuffer) - 1;
    output[1].length = &hashLength;
    output[2].buffer_type = MYSQL_TYPE_TINY;
    output[2].buffer = &databaseStatus;
    if (mysql_stmt_bind_result(statement.get(), output) != 0 ||
            mysql_stmt_store_result(statement.get()) != 0) {
        *error = "database user result binding failed";
        return false;
    }

    const int fetchResult = mysql_stmt_fetch(statement.get());
    if (fetchResult == MYSQL_NO_DATA)
        return true;
    if (fetchResult != 0 && fetchResult != MYSQL_DATA_TRUNCATED) {
        *error = "database user result fetch failed";
        return false;
    }
    if (hashLength >= sizeof(hashBuffer)) {
        *error = "stored password hash is invalid";
        return false;
    }
    hashBuffer[hashLength] = '\0';
    *found = true;
    *userId = static_cast<uint64_t>(databaseUserId);
    *passwordHash = hashBuffer;
    *status = static_cast<int>(databaseStatus);
    return true;
}

