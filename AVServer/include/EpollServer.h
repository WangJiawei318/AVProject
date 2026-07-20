#ifndef EPOLLSERVER_H
#define EPOLLSERVER_H

#include <stdint.h>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ConnectionContext.h"
#include "ProtocolDispatcher.h"

class EpollServer
{
public:
    EpollServer();
    ~EpollServer();

    bool start(uint16_t port);

private:
    bool createListenSocket(uint16_t port);
    bool setNonBlocking(int fd) const;
    bool addToEpoll(int fd, uint32_t events);
    bool modifyClientEvents(int fd, bool wantWrite);
    void acceptClients();
    bool handleRead(int clientFd);
    bool parseFrames(ConnectionContext &connection);
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
        kEpollWaitTimeoutMs = 60 * 1000,
        kMaintenanceIntervalSeconds = 5 * 60
    };

    int m_listenFd;
    int m_epollFd;
    std::time_t m_lastMaintenance;
    std::map<int, std::unique_ptr<ConnectionContext> > m_connections;
    ProtocolDispatcher m_dispatcher;
};

#endif // EPOLLSERVER_H
