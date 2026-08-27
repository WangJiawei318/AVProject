#include "DatabaseConnectionPool.h"

#include <mysql/mysql.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace {

std::string trim(const std::string &value)
{
    std::string::size_type begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    std::string::size_type end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(begin, end - begin);
}

bool parseUnsigned(const std::string &text, unsigned long *value)
{
    if (!value || text.empty())
        return false;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0')
        return false;
    *value = parsed;
    return true;
}

class MysqlThreadGuard
{
public:
    MysqlThreadGuard() : m_initialized(mysql_thread_init() == 0) {}
    ~MysqlThreadGuard()
    {
        if (m_initialized)
            mysql_thread_end();
    }

    bool initialized() const { return m_initialized; }

private:
    bool m_initialized;
};

} // namespace

DatabaseConfig::DatabaseConfig()
    : host("127.0.0.1"),
      port(3306),
      poolSize(4),
      connectTimeoutSec(3),
      acquireTimeoutMs(2500)
{
}

DatabaseConnectionPool::ConnectionSlot::ConnectionSlot()
    : connection(nullptr)
{
}

DatabaseConnectionPool::ConnectionLease::ConnectionLease()
    : m_pool(nullptr), m_slot(nullptr)
{
}

DatabaseConnectionPool::ConnectionLease::ConnectionLease(
        DatabaseConnectionPool *pool, ConnectionSlot *slot)
    : m_pool(pool), m_slot(slot)
{
}

DatabaseConnectionPool::ConnectionLease::~ConnectionLease()
{
    release();
}

DatabaseConnectionPool::ConnectionLease::ConnectionLease(ConnectionLease &&other)
    : m_pool(other.m_pool), m_slot(other.m_slot)
{
    other.m_pool = nullptr;
    other.m_slot = nullptr;
}

DatabaseConnectionPool::ConnectionLease &
DatabaseConnectionPool::ConnectionLease::operator=(ConnectionLease &&other)
{
    if (this != &other) {
        release();
        m_pool = other.m_pool;
        m_slot = other.m_slot;
        other.m_pool = nullptr;
        other.m_slot = nullptr;
    }
    return *this;
}

MYSQL *DatabaseConnectionPool::ConnectionLease::get() const
{
    return m_slot ? m_slot->connection : nullptr;
}

DatabaseConnectionPool::ConnectionLease::operator bool() const
{
    return get() != nullptr;
}

void DatabaseConnectionPool::ConnectionLease::release()
{
    if (m_pool && m_slot)
        m_pool->release(m_slot);
    m_pool = nullptr;
    m_slot = nullptr;
}

DatabaseConnectionPool::DatabaseConnectionPool()
    : m_initialized(false), m_libraryInitialized(false), m_stopping(false)
{
}

DatabaseConnectionPool::~DatabaseConnectionPool()
{
    shutdown();
}

bool DatabaseConnectionPool::initialize(const std::string &configPath,
                                        std::string *error)
{
    DatabaseConfig loaded;
    if (!loadConfig(configPath, &loaded, error))
        return false;
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        if (error)
            *error = "failed to initialize MySQL client library";
        return false;
    }
    m_libraryInitialized = true;

    m_config = loaded;
    m_stopping = false;
    for (std::size_t index = 0; index < m_config.poolSize; ++index) {
        std::unique_ptr<ConnectionSlot> slot(new ConnectionSlot);
        slot->connection = openConnection(error);
        if (!slot->connection) {
            shutdown();
            return false;
        }
        m_available.push(slot.get());
        m_slots.push_back(std::move(slot));
    }
    m_initialized = true;
    return true;
}

void DatabaseConnectionPool::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stopping = true;
    m_condition.notify_all();
    while (!m_available.empty())
        m_available.pop();
    for (std::size_t index = 0; index < m_slots.size(); ++index) {
        if (m_slots[index]->connection) {
            mysql_close(m_slots[index]->connection);
            m_slots[index]->connection = nullptr;
        }
    }
    m_slots.clear();
    if (m_libraryInitialized) {
        mysql_library_end();
        m_libraryInitialized = false;
    }
    m_initialized = false;
}

DatabaseConnectionPool::ConnectionLease
DatabaseConnectionPool::acquire(std::string *error)
{
    thread_local MysqlThreadGuard threadGuard;
    if (!threadGuard.initialized()) {
        if (error)
            *error = "database temporarily unavailable";
        return ConnectionLease();
    }

    ConnectionSlot *slot = nullptr;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool available = m_condition.wait_for(
                    lock,
                    std::chrono::milliseconds(m_config.acquireTimeoutMs),
                    [this]() { return m_stopping || !m_available.empty(); });
        if (!available || m_stopping || m_available.empty()) {
            if (error)
                *error = "database temporarily unavailable";
            return ConnectionLease();
        }
        slot = m_available.front();
        m_available.pop();
    }

    if (!ensureConnected(slot, error)) {
        release(slot);
        return ConnectionLease();
    }
    return ConnectionLease(this, slot);
}

bool DatabaseConnectionPool::initialized() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_initialized;
}

const DatabaseConfig &DatabaseConnectionPool::config() const
{
    return m_config;
}

bool DatabaseConnectionPool::loadConfig(const std::string &path,
                                        DatabaseConfig *config,
                                        std::string *error) const
{
    std::ifstream input(path.c_str());
    if (!input) {
        if (error)
            *error = "database config not found: " + path;
        return false;
    }
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        const std::string::size_type equal = line.find('=');
        if (equal == std::string::npos)
            continue;
        values[trim(line.substr(0, equal))] = trim(line.substr(equal + 1));
    }

    config->host = values["host"];
    config->database = values["database"];
    config->user = values["user"];
    config->password = values["password"];
    unsigned long number = 0;
    if (config->host.empty() || config->database.empty() ||
            config->user.empty() || config->password.empty() ||
            !parseUnsigned(values["port"], &number) || number == 0 ||
            number > 65535) {
        if (error)
            *error = "database config is incomplete or invalid";
        return false;
    }
    config->port = static_cast<unsigned int>(number);
    if (!parseUnsigned(values["pool_size"], &number) || number == 0 || number > 32) {
        if (error)
            *error = "database pool_size must be between 1 and 32";
        return false;
    }
    config->poolSize = static_cast<std::size_t>(number);
    if (!parseUnsigned(values["connect_timeout_sec"], &number) ||
            number == 0 || number > 30) {
        if (error)
            *error = "database connect_timeout_sec must be between 1 and 30";
        return false;
    }
    config->connectTimeoutSec = static_cast<unsigned int>(number);
    if (!parseUnsigned(values["acquire_timeout_ms"], &number) ||
            number < 100 || number > 30000) {
        if (error)
            *error = "database acquire_timeout_ms must be between 100 and 30000";
        return false;
    }
    config->acquireTimeoutMs = static_cast<unsigned int>(number);
    return true;
}

MYSQL *DatabaseConnectionPool::openConnection(std::string *error) const
{
    MYSQL *connection = mysql_init(nullptr);
    if (!connection) {
        if (error)
            *error = "failed to allocate MySQL connection";
        return nullptr;
    }
    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT,
                  &m_config.connectTimeoutSec);
    const char reconnect = 1;
    mysql_options(connection, MYSQL_OPT_RECONNECT, &reconnect);
    mysql_options(connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (!mysql_real_connect(connection,
                            m_config.host.c_str(),
                            m_config.user.c_str(),
                            m_config.password.c_str(),
                            m_config.database.c_str(),
                            m_config.port,
                            nullptr,
                            0)) {
        if (error)
            *error = "cannot connect to MySQL";
        mysql_close(connection);
        return nullptr;
    }
    return connection;
}

bool DatabaseConnectionPool::ensureConnected(ConnectionSlot *slot,
                                             std::string *error) const
{
    if (slot->connection && mysql_ping(slot->connection) == 0)
        return true;
    if (slot->connection) {
        mysql_close(slot->connection);
        slot->connection = nullptr;
    }
    slot->connection = openConnection(error);
    if (!slot->connection && error)
        *error = "database temporarily unavailable";
    return slot->connection != nullptr;
}

void DatabaseConnectionPool::release(ConnectionSlot *slot)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stopping) {
        m_available.push(slot);
        m_condition.notify_one();
    }
}
