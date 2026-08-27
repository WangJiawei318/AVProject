#ifndef EPOLLSERVER_H
#define EPOLLSERVER_H

#include <stdint.h>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "ConnectionContext.h"
#include "ProtocolDispatcher.h"
#include "ThreadPool.h"

class EpollServer
{
public:
    EpollServer();
    ~EpollServer();

    bool start(uint16_t port);

private:
    struct CompletedTask
    {
        CompletedTask();

        uint64_t connectionId;
        int clientFd;
        int32_t protocolType;
        std::vector<std::vector<char> > responses;
        bool closeConnection;
        bool maintenanceTask;
        bool hasAuthUpdate;
        bool authSuccess;
        uint64_t authenticatedUserId;
        std::string authenticatedUsername;
        std::string error;
    };

    bool createListenSocket(uint16_t port);
    bool createCompletionEvent();
    bool setNonBlocking(int fd) const;
    bool addToEpoll(int fd, uint32_t events);
    bool modifyClientEvents(int fd, bool wantWrite);
    void acceptClients();
    bool handleRead(int clientFd);
    bool handleCompletionEvent();
    bool parseFrames(ConnectionContext &connection);
    bool submitBusinessTask(ConnectionContext &connection,
                            const std::vector<char> &packet,
                            int32_t protocolType);
    void pushCompletion(CompletedTask task);
    void scheduleMaintenance(std::time_t now);
    bool queueResponse(ConnectionContext &connection,
                       const std::vector<char> &packetBody);
    bool flushSendQueue(int clientFd);
    void closeConnection(int clientFd, const std::string &reason);
    void closeAll();

private:
    enum
    {
        kMaxEvents = 64,
        kReadBufferSize = 16 * 1024,
        kMaxPacketLength = 256 * 1024,
        kMaxQueuedBytesPerConnection = 4 * 1024 * 1024,
        kMaxReceiveBufferPerConnection = 4 * 1024 * 1024,
        kEpollWaitTimeoutMs = 60 * 1000,
        kMaintenanceIntervalSeconds = 5 * 60,
        kCoreWorkerCount = 4,
        kMaxWorkerCount = 8,
        kTaskQueueCapacity = 256,
        kNonCoreIdleTimeoutSeconds = 60
    };

    int m_listenFd;
    int m_epollFd;
    int m_completionEventFd;
    std::time_t m_lastMaintenance;
    uint64_t m_nextConnectionId;
    bool m_running;
    bool m_maintenanceTaskInFlight;
    std::map<int, std::unique_ptr<ConnectionContext> > m_connections;
    std::map<uint64_t, int> m_connectionFds;
    ProtocolDispatcher m_dispatcher;
    ThreadPool m_threadPool;
    std::mutex m_completionMutex;
    std::queue<CompletedTask> m_completionQueue;
};

#endif // EPOLLSERVER_H
