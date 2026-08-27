#ifndef DATABASECONNECTIONPOOL_H
#define DATABASECONNECTIONPOOL_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

struct st_mysql;
typedef struct st_mysql MYSQL;

struct DatabaseConfig
{
    DatabaseConfig();

    std::string host;
    unsigned int port;
    std::string database;
    std::string user;
    std::string password;
    std::size_t poolSize;
    unsigned int connectTimeoutSec;
    unsigned int acquireTimeoutMs;
};

class DatabaseConnectionPool
{
private:
    struct ConnectionSlot;

public:
    class ConnectionLease
    {
    public:
        ConnectionLease();
        ~ConnectionLease();
        ConnectionLease(ConnectionLease &&other);
        ConnectionLease &operator=(ConnectionLease &&other);

        MYSQL *get() const;
        explicit operator bool() const;

        ConnectionLease(const ConnectionLease &) = delete;
        ConnectionLease &operator=(const ConnectionLease &) = delete;

    private:
        friend class DatabaseConnectionPool;
        ConnectionLease(DatabaseConnectionPool *pool, ConnectionSlot *slot);
        void release();

        DatabaseConnectionPool *m_pool;
        ConnectionSlot *m_slot;
    };

    DatabaseConnectionPool();
    ~DatabaseConnectionPool();

    bool initialize(const std::string &configPath, std::string *error);
    void shutdown();
    ConnectionLease acquire(std::string *error);
    bool initialized() const;
    const DatabaseConfig &config() const;

    DatabaseConnectionPool(const DatabaseConnectionPool &) = delete;
    DatabaseConnectionPool &operator=(const DatabaseConnectionPool &) = delete;

private:
    bool loadConfig(const std::string &path, DatabaseConfig *config,
                    std::string *error) const;
    MYSQL *openConnection(std::string *error) const;
    bool ensureConnected(ConnectionSlot *slot, std::string *error) const;
    void release(ConnectionSlot *slot);

    struct ConnectionSlot
    {
        ConnectionSlot();
        MYSQL *connection;
    };

    DatabaseConfig m_config;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<std::unique_ptr<ConnectionSlot> > m_slots;
    std::queue<ConnectionSlot *> m_available;
    bool m_initialized;
    bool m_libraryInitialized;
    bool m_stopping;
};

#endif // DATABASECONNECTIONPOOL_H
