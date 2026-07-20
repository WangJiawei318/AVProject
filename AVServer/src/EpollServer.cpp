#include "EpollServer.h"

#include "av_protocol.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace {

volatile sig_atomic_t g_stopRequested = 0;
volatile sig_atomic_t g_completionWakeFd = -1;

void handleStopSignal(int)
{
    g_stopRequested = 1;
    if (g_completionWakeFd >= 0) {
        const uint64_t value = 1;
        const int fd = static_cast<int>(g_completionWakeFd);
        const ssize_t ignored = write(fd, &value, sizeof(value));
        (void)ignored;
    }
}

} // namespace

EpollServer::CompletedTask::CompletedTask()
    : connectionId(0),
      clientFd(-1),
      protocolType(0),
      closeConnection(false),
      maintenanceTask(false)
{
}

EpollServer::EpollServer()
    : m_listenFd(-1),
      m_epollFd(-1),
      m_completionEventFd(-1),
      m_lastMaintenance(0),
      m_nextConnectionId(0),
      m_running(false),
      m_maintenanceTaskInFlight(false),
      m_threadPool(kCoreWorkerCount,
                   kMaxWorkerCount,
                   kTaskQueueCapacity,
                   std::chrono::seconds(kNonCoreIdleTimeoutSeconds))
{
}

EpollServer::~EpollServer()
{
    closeAll();
}

bool EpollServer::start(uint16_t port)
{
    std::printf("AVServer started\n");
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);
    g_stopRequested = 0;

    if (!m_dispatcher.initialize())
        return false;
    if (!createListenSocket(port)) {
        closeAll();
        return false;
    }

    m_epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        std::perror("epoll_create1");
        closeAll();
        return false;
    }
    if (!createCompletionEvent() ||
            !addToEpoll(m_listenFd, EPOLLIN) ||
            !addToEpoll(m_completionEventFd, EPOLLIN)) {
        closeAll();
        return false;
    }
    if (!m_threadPool.start()) {
        std::printf("failed to start business thread pool\n");
        closeAll();
        return false;
    }

    std::printf("epoll LT reactor initialized max_packet=%d max_send_queue=%d\n",
                kMaxPacketLength,
                kMaxQueuedBytesPerConnection);
    std::printf("eventfd initialized fd=%d\n", m_completionEventFd);
    std::printf("core worker count=%d\n", kCoreWorkerCount);
    std::printf("max worker count=%d\n", kMaxWorkerCount);
    std::printf("task queue capacity=%d\n", kTaskQueueCapacity);
    std::printf("non-core idle timeout=%ds\n", kNonCoreIdleTimeoutSeconds);
    std::printf("listening on port %u\n", static_cast<unsigned>(port));
    m_lastMaintenance = std::time(nullptr);
    m_running = true;

    epoll_event events[kMaxEvents];
    while (m_running && !g_stopRequested) {
        const int ready = epoll_wait(m_epollFd,
                                     events,
                                     kMaxEvents,
                                     kEpollWaitTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                if (g_stopRequested)
                    break;
                continue;
            }
            std::perror("epoll_wait");
            closeAll();
            return false;
        }
        if (g_stopRequested)
            break;

        for (int index = 0; index < ready; ++index) {
            const int fd = events[index].data.fd;
            const uint32_t flags = events[index].events;
            if (fd == m_listenFd) {
                acceptClients();
                continue;
            }
            if (fd == m_completionEventFd) {
                if (!handleCompletionEvent()) {
                    closeAll();
                    return false;
                }
                continue;
            }

            if (m_connections.find(fd) == m_connections.end())
                continue;

            if ((flags & EPOLLERR) != 0) {
                int socketError = 0;
                socklen_t length = sizeof(socketError);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length);
                closeConnection(fd,
                                std::string("connection error errno=") +
                                std::to_string(socketError));
                continue;
            }

            if ((flags & EPOLLIN) != 0 && !handleRead(fd)) {
                closeConnection(fd, "peer closed or read failed");
                continue;
            }

            if (m_connections.find(fd) == m_connections.end())
                continue;

            if ((flags & EPOLLOUT) != 0 && !flushSendQueue(fd)) {
                closeConnection(fd, "send failed");
                continue;
            }

            if (m_connections.find(fd) == m_connections.end())
                continue;

            if ((flags & (EPOLLHUP | EPOLLRDHUP)) != 0)
                closeConnection(fd, "peer hangup");
        }

        const std::time_t now = std::time(nullptr);
        if (now >= m_lastMaintenance + kMaintenanceIntervalSeconds) {
            scheduleMaintenance(now);
            m_lastMaintenance = now;
        }
    }

    std::printf("reactor stopping\n");
    closeAll();
    return true;
}

bool EpollServer::createListenSocket(uint16_t port)
{
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        std::perror("socket");
        return false;
    }

    int reuse = 1;
    if (setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::perror("setsockopt SO_REUSEADDR");
        return false;
    }
    if (!setNonBlocking(m_listenFd))
        return false;

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(m_listenFd,
             reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0) {
        std::perror("bind");
        return false;
    }
    if (listen(m_listenFd, 128) < 0) {
        std::perror("listen");
        return false;
    }
    return true;
}

bool EpollServer::createCompletionEvent()
{
    m_completionEventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_completionEventFd < 0) {
        std::perror("eventfd");
        return false;
    }
    g_completionWakeFd = m_completionEventFd;
    return true;
}

bool EpollServer::setNonBlocking(int fd) const
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::perror("fcntl O_NONBLOCK");
        return false;
    }
    return true;
}

bool EpollServer::addToEpoll(int fd, uint32_t events)
{
    epoll_event event;
    std::memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &event) < 0) {
        std::perror("epoll_ctl ADD");
        return false;
    }
    return true;
}

bool EpollServer::modifyClientEvents(int fd, bool wantWrite)
{
    epoll_event event;
    std::memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (wantWrite)
        event.events |= EPOLLOUT;
    if (epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &event) < 0) {
        std::printf("epoll_ctl MOD failed fd=%d errno=%d\n", fd, errno);
        return false;
    }
    return true;
}

void EpollServer::acceptClients()
{
    while (true) {
        sockaddr_in peerAddress;
        socklen_t peerLength = sizeof(peerAddress);
        const int clientFd = accept(m_listenFd,
                                    reinterpret_cast<sockaddr *>(&peerAddress),
                                    &peerLength);
        if (clientFd < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            std::printf("accept failed errno=%d\n", errno);
            return;
        }

        if (!setNonBlocking(clientFd)) {
            close(clientFd);
            continue;
        }

        int noDelay = 1;
        setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

        char ipBuffer[INET_ADDRSTRLEN] = {0};
        const char *ip = inet_ntop(AF_INET,
                                  &peerAddress.sin_addr,
                                  ipBuffer,
                                  sizeof(ipBuffer));
        const std::string peerIp = ip ? ip : "unknown";
        const uint16_t peerPort = ntohs(peerAddress.sin_port);

        const uint64_t connectionId = ++m_nextConnectionId;
        std::unique_ptr<ConnectionContext> connection(
                    new ConnectionContext(clientFd,
                                          connectionId,
                                          peerIp,
                                          peerPort));
        m_connections[clientFd] = std::move(connection);
        m_connectionFds[connectionId] = clientFd;
        if (!addToEpoll(clientFd, EPOLLIN | EPOLLRDHUP)) {
            closeConnection(clientFd, "failed to register epoll events");
            continue;
        }

        std::printf("client connected fd=%d connectionId=%llu ip=%s port=%u active=%zu\n",
                    clientFd,
                    static_cast<unsigned long long>(connectionId),
                    peerIp.c_str(),
                    static_cast<unsigned>(peerPort),
                    m_connections.size());
    }
}

bool EpollServer::handleRead(int clientFd)
{
    std::map<int, std::unique_ptr<ConnectionContext> >::iterator it =
            m_connections.find(clientFd);
    if (it == m_connections.end())
        return false;

    ConnectionContext &connection = *it->second;
    char buffer[kReadBufferSize];
    while (true) {
        const ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
        if (received > 0) {
            connection.lastActivity = std::time(nullptr);
            if (connection.receiveBuffer.size() +
                    static_cast<size_t>(received) >
                    kMaxReceiveBufferPerConnection) {
                std::printf("receive buffer limit exceeded fd=%d connectionId=%llu\n",
                            clientFd,
                            static_cast<unsigned long long>(connection.connectionId));
                return false;
            }
            connection.receiveBuffer.insert(connection.receiveBuffer.end(),
                                            buffer,
                                            buffer + received);
            if (!parseFrames(connection))
                return false;
            continue;
        }
        if (received == 0)
            return false;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        std::printf("recv failed fd=%d errno=%d\n", clientFd, errno);
        return false;
    }

    return flushSendQueue(clientFd);
}

bool EpollServer::handleCompletionEvent()
{
    uint64_t value = 0;
    while (true) {
        const ssize_t readSize = read(m_completionEventFd, &value, sizeof(value));
        if (readSize == static_cast<ssize_t>(sizeof(value)))
            continue;
        if (readSize < 0 && errno == EINTR)
            continue;
        if (readSize < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        std::printf("eventfd read failed errno=%d\n", errno);
        return false;
    }

    std::queue<CompletedTask> completions;
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        completions.swap(m_completionQueue);
    }

    while (!completions.empty()) {
        CompletedTask completed = std::move(completions.front());
        completions.pop();
        if (completed.maintenanceTask) {
            m_maintenanceTaskInFlight = false;
            continue;
        }

        std::map<uint64_t, int>::iterator idIt =
                m_connectionFds.find(completed.connectionId);
        if (idIt == m_connectionFds.end()) {
            std::printf("completion dropped connection closed connectionId=%llu protocol=%d\n",
                        static_cast<unsigned long long>(completed.connectionId),
                        completed.protocolType);
            m_dispatcher.onClientDisconnected(completed.connectionId,
                                              completed.clientFd);
            continue;
        }

        const int clientFd = idIt->second;
        std::map<int, std::unique_ptr<ConnectionContext> >::iterator connectionIt =
                m_connections.find(clientFd);
        if (connectionIt == m_connections.end() ||
                connectionIt->second->connectionId != completed.connectionId) {
            std::printf("completion dropped stale fd connectionId=%llu protocol=%d\n",
                        static_cast<unsigned long long>(completed.connectionId),
                        completed.protocolType);
            m_dispatcher.onClientDisconnected(completed.connectionId,
                                              completed.clientFd);
            continue;
        }

        ConnectionContext &connection = *connectionIt->second;
        connection.businessTaskInFlight = false;
        bool ready = completed.error.empty() && !completed.closeConnection;
        for (size_t index = 0; ready && index < completed.responses.size(); ++index)
            ready = queueResponse(connection, completed.responses[index]);

        if (!ready) {
            closeConnection(clientFd,
                            completed.error.empty()
                            ? "business completion failed"
                            : completed.error);
            continue;
        }

        std::printf("completion delivered connectionId=%llu fd=%d protocol=%d responses=%zu\n",
                    static_cast<unsigned long long>(completed.connectionId),
                    clientFd,
                    completed.protocolType,
                    completed.responses.size());
        if (!parseFrames(connection) || !flushSendQueue(clientFd))
            closeConnection(clientFd, "failed after business completion");
    }
    return true;
}

bool EpollServer::parseFrames(ConnectionContext &connection)
{
    if (connection.businessTaskInFlight)
        return true;

    const size_t lengthSize = sizeof(int32_t);
    while (connection.receiveBuffer.size() >= lengthSize) {
        int32_t packetLength = 0;
        std::memcpy(&packetLength, connection.receiveBuffer.data(), lengthSize);
        if (packetLength < static_cast<int32_t>(sizeof(PackType)) ||
                packetLength > kMaxPacketLength) {
            std::printf("invalid packet length fd=%d length=%d\n",
                        connection.fd,
                        packetLength);
            return false;
        }

        const size_t frameSize = lengthSize + static_cast<size_t>(packetLength);
        if (connection.receiveBuffer.size() < frameSize)
            return true;

        std::vector<char> packet(connection.receiveBuffer.begin() + lengthSize,
                                 connection.receiveBuffer.begin() + frameSize);
        connection.receiveBuffer.erase(connection.receiveBuffer.begin(),
                                       connection.receiveBuffer.begin() + frameSize);

        int32_t protocolType = 0;
        if (m_dispatcher.isBusinessPacket(packet, &protocolType)) {
            if (!submitBusinessTask(connection, packet, protocolType))
                return false;
            if (connection.businessTaskInFlight)
                return true;
            continue;
        }

        std::vector<std::vector<char> > responses;
        m_dispatcher.dispatch(connection.connectionId,
                              connection.fd,
                              packet,
                              &responses);
        for (size_t index = 0; index < responses.size(); ++index) {
            if (!queueResponse(connection, responses[index]))
                return false;
        }
    }
    return true;
}

bool EpollServer::submitBusinessTask(ConnectionContext &connection,
                                     const std::vector<char> &packet,
                                     int32_t protocolType)
{
    if (connection.businessTaskInFlight)
        return true;

    const uint64_t connectionId = connection.connectionId;
    const int clientFd = connection.fd;
    connection.businessTaskInFlight = true;
    const bool submitted = m_threadPool.submit(
                [this, connectionId, clientFd, protocolType, packet]() {
        CompletedTask completed;
        completed.connectionId = connectionId;
        completed.clientFd = clientFd;
        completed.protocolType = protocolType;
        std::printf("worker started task connectionId=%llu fd=%d protocol=%d\n",
                    static_cast<unsigned long long>(connectionId),
                    clientFd,
                    protocolType);
        try {
            m_dispatcher.dispatch(connectionId,
                                  clientFd,
                                  packet,
                                  &completed.responses);
        } catch (const std::exception &error) {
            completed.closeConnection = true;
            completed.error = std::string("business task exception: ") + error.what();
        } catch (...) {
            completed.closeConnection = true;
            completed.error = "business task exception";
        }
        std::printf("worker completed task connectionId=%llu fd=%d protocol=%d responses=%zu\n",
                    static_cast<unsigned long long>(connectionId),
                    clientFd,
                    protocolType,
                    completed.responses.size());
        pushCompletion(std::move(completed));
    });

    if (submitted) {
        std::printf("task submitted connectionId=%llu fd=%d protocol=%d pending=%zu workers=%zu\n",
                    static_cast<unsigned long long>(connectionId),
                    clientFd,
                    protocolType,
                    m_threadPool.pendingTaskCount(),
                    m_threadPool.workerCount());
        return true;
    }

    connection.businessTaskInFlight = false;
    std::printf("task rejected: queue full connectionId=%llu fd=%d protocol=%d\n",
                static_cast<unsigned long long>(connectionId),
                clientFd,
                protocolType);
    std::vector<std::vector<char> > responses;
    m_dispatcher.buildErrorResponse(packet, "server busy", &responses);
    for (size_t index = 0; index < responses.size(); ++index) {
        if (!queueResponse(connection, responses[index]))
            return false;
    }
    return true;
}

void EpollServer::pushCompletion(CompletedTask task)
{
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        m_completionQueue.push(std::move(task));
    }

    const uint64_t value = 1;
    while (write(m_completionEventFd, &value, sizeof(value)) < 0) {
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            std::printf("eventfd write failed errno=%d\n", errno);
        break;
    }
}

void EpollServer::scheduleMaintenance(std::time_t now)
{
    if (m_maintenanceTaskInFlight)
        return;
    m_maintenanceTaskInFlight = true;
    if (!m_threadPool.submit([this, now]() {
        CompletedTask completed;
        completed.maintenanceTask = true;
        try {
            m_dispatcher.performMaintenance(now);
        } catch (const std::exception &error) {
            completed.error = std::string("upload maintenance exception: ") +
                    error.what();
            std::printf("%s\n", completed.error.c_str());
        } catch (...) {
            completed.error = "upload maintenance exception";
            std::printf("%s\n", completed.error.c_str());
        }
        pushCompletion(std::move(completed));
    })) {
        m_maintenanceTaskInFlight = false;
        std::printf("upload maintenance skipped: task queue full\n");
    }
}

bool EpollServer::queueResponse(ConnectionContext &connection,
                                const std::vector<char> &packetBody)
{
    if (packetBody.empty() || packetBody.size() > kMaxPacketLength) {
        std::printf("invalid response length fd=%d length=%zu\n",
                    connection.fd,
                    packetBody.size());
        return false;
    }

    const size_t frameSize = sizeof(int32_t) + packetBody.size();
    if (connection.queuedBytes + frameSize > kMaxQueuedBytesPerConnection) {
        std::printf("send queue limit exceeded fd=%d queued=%zu incoming=%zu\n",
                    connection.fd,
                    connection.queuedBytes,
                    frameSize);
        return false;
    }

    const int32_t packetLength = static_cast<int32_t>(packetBody.size());
    std::vector<char> frame(frameSize);
    std::memcpy(frame.data(), &packetLength, sizeof(packetLength));
    std::memcpy(frame.data() + sizeof(packetLength),
                packetBody.data(),
                packetBody.size());
    connection.queuedBytes += frame.size();
    connection.sendQueue.push_back(PendingSend(std::move(frame)));
    return true;
}

bool EpollServer::flushSendQueue(int clientFd)
{
    std::map<int, std::unique_ptr<ConnectionContext> >::iterator it =
            m_connections.find(clientFd);
    if (it == m_connections.end())
        return false;

    ConnectionContext &connection = *it->second;
    while (!connection.sendQueue.empty()) {
        PendingSend &pending = connection.sendQueue.front();
        const size_t remaining = pending.data.size() - pending.offset;
        const ssize_t sent = send(clientFd,
                                  pending.data.data() + pending.offset,
                                  remaining,
                                  MSG_NOSIGNAL);
        if (sent > 0) {
            pending.offset += static_cast<size_t>(sent);
            connection.queuedBytes -= static_cast<size_t>(sent);
            connection.lastActivity = std::time(nullptr);
            if (pending.offset == pending.data.size())
                connection.sendQueue.pop_front();
            continue;
        }
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::printf("partial send fd=%d remaining=%zu queued=%zu\n",
                        clientFd,
                        remaining,
                        connection.queuedBytes);
            return modifyClientEvents(clientFd, true);
        }

        std::printf("send failed fd=%d errno=%d\n", clientFd, errno);
        return false;
    }

    return modifyClientEvents(clientFd, false);
}

void EpollServer::closeConnection(int clientFd, const std::string &reason)
{
    std::map<int, std::unique_ptr<ConnectionContext> >::iterator it =
            m_connections.find(clientFd);
    if (it == m_connections.end())
        return;

    const uint64_t connectionId = it->second->connectionId;
    m_connectionFds.erase(connectionId);
    m_dispatcher.onClientDisconnected(connectionId, clientFd);
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
    close(clientFd);
    m_connections.erase(it);
    std::printf("client disconnected fd=%d connectionId=%llu reason=%s active=%zu\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                reason.c_str(),
                m_connections.size());
}

void EpollServer::closeAll()
{
    m_running = false;
    if (m_listenFd >= 0) {
        if (m_epollFd >= 0)
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, m_listenFd, nullptr);
        close(m_listenFd);
        m_listenFd = -1;
    }

    m_threadPool.stop();
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        std::queue<CompletedTask> empty;
        m_completionQueue.swap(empty);
    }

    while (!m_connections.empty())
        closeConnection(m_connections.begin()->first, "server shutdown");

    if (m_completionEventFd >= 0) {
        g_completionWakeFd = -1;
        if (m_epollFd >= 0)
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, m_completionEventFd, nullptr);
        close(m_completionEventFd);
        m_completionEventFd = -1;
    }
    if (m_epollFd >= 0) {
        close(m_epollFd);
        m_epollFd = -1;
    }
}
