#include "EpollServer.h"

#include "av_protocol.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

EpollServer::EpollServer()
    : m_listenFd(-1),
      m_epollFd(-1)
{
}

EpollServer::~EpollServer()
{
    closeAll();
}

bool EpollServer::start(uint16_t port)
{
    std::printf("server started\n");
    std::signal(SIGPIPE, SIG_IGN);

    if (!m_dispatcher.initialize())
        return false;
    if (!createListenSocket(port))
        return false;

    m_epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        std::perror("epoll_create1");
        return false;
    }
    if (!addToEpoll(m_listenFd, EPOLLIN))
        return false;

    std::printf("epoll initialized mode=LT max_packet=%d max_send_queue=%d\n",
                kMaxPacketLength,
                kMaxQueuedBytesPerConnection);
    std::printf("listening on port %u\n", static_cast<unsigned>(port));

    epoll_event events[kMaxEvents];
    while (true) {
        const int ready = epoll_wait(m_epollFd, events, kMaxEvents, -1);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            std::perror("epoll_wait");
            return false;
        }

        for (int index = 0; index < ready; ++index) {
            const int fd = events[index].data.fd;
            const uint32_t flags = events[index].events;
            if (fd == m_listenFd) {
                acceptClients();
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
    }
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

        std::unique_ptr<ConnectionContext> connection(
                    new ConnectionContext(clientFd, peerIp, peerPort));
        m_connections[clientFd] = std::move(connection);
        if (!addToEpoll(clientFd, EPOLLIN | EPOLLRDHUP)) {
            closeConnection(clientFd, "failed to register epoll events");
            continue;
        }

        std::printf("client connected fd=%d ip=%s port=%u active=%zu\n",
                    clientFd,
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

bool EpollServer::parseFrames(ConnectionContext &connection)
{
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

        std::vector<std::vector<char> > responses;
        m_dispatcher.dispatch(connection.fd, packet, &responses);
        for (size_t index = 0; index < responses.size(); ++index) {
            if (!queueResponse(connection, responses[index]))
                return false;
        }
    }
    return true;
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

    m_dispatcher.onClientDisconnected(clientFd);
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
    close(clientFd);
    m_connections.erase(it);
    std::printf("client disconnected fd=%d reason=%s active=%zu\n",
                clientFd,
                reason.c_str(),
                m_connections.size());
}

void EpollServer::closeAll()
{
    while (!m_connections.empty())
        closeConnection(m_connections.begin()->first, "server shutdown");

    if (m_listenFd >= 0) {
        close(m_listenFd);
        m_listenFd = -1;
    }
    if (m_epollFd >= 0) {
        close(m_epollFd);
        m_epollFd = -1;
    }
}
