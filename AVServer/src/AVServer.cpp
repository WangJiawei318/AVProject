#include "AVServer.h"

#include "av_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

AVServer::AVServer()
    : m_listenFd(-1)
{
}

AVServer::~AVServer()
{
    closeListenFd();
}

bool AVServer::start(uint16_t port)
{
    printf("server started\n");

    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        perror("socket");
        return false;
    }

    int reuse = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return false;
    }

    if (listen(m_listenFd, 16) < 0) {
        perror("listen");
        return false;
    }

    printf("listening on port %u\n", static_cast<unsigned>(port));

    while (true) {
        sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        int clientFd = accept(m_listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &len);
        if (clientFd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        int nodelay = 1;
        setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        printf("client connected: %s:%u\n",
               inet_ntoa(clientAddr.sin_addr),
               static_cast<unsigned>(ntohs(clientAddr.sin_port)));

        handleClient(clientFd);
        close(clientFd);
        printf("client disconnected\n");
    }

    return true;
}

bool AVServer::readExact(int fd, char *data, int len)
{
    int received = 0;
    while (received < len) {
        int ret = recv(fd, data + received, len - received, 0);
        if (ret <= 0)
            return false;
        received += ret;
    }
    return true;
}

bool AVServer::sendPacket(int fd, const char *data, int len)
{
    int32_t packLen = len;
    std::vector<char> frame(sizeof(packLen) + len);
    memcpy(frame.data(), &packLen, sizeof(packLen));
    memcpy(frame.data() + sizeof(packLen), data, len);

    int sent = 0;
    while (sent < static_cast<int>(frame.size())) {
        int ret = send(fd, frame.data() + sent, frame.size() - sent, 0);
        if (ret <= 0)
            return false;
        sent += ret;
    }
    return true;
}

void AVServer::handleClient(int clientFd)
{
    while (true) {
        int32_t packLen = 0;
        if (!readExact(clientFd, reinterpret_cast<char *>(&packLen), sizeof(packLen)))
            break;

        if (packLen <= 0 || packLen > 1024 * 1024) {
            printf("invalid packet length: %d\n", packLen);
            break;
        }

        std::vector<char> packet(packLen);
        if (!readExact(clientFd, packet.data(), packLen))
            break;

        handlePacket(clientFd, packet);
    }
}

void AVServer::handlePacket(int clientFd, const std::vector<char> &packet)
{
    if (packet.size() < sizeof(PackType)) {
        printf("received invalid packet\n");
        return;
    }

    PackType type = 0;
    memcpy(&type, packet.data(), sizeof(type));

    switch (type) {
    case DEF_PACK_PING_RQ:
    {
        printf("received PING_RQ\n");
        STRU_PING_RS rs;
        sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));
        printf("sent PING_RS\n");
        break;
    }
    case DEF_PACK_LOGIN_RQ:
    {
        printf("received LOGIN_RQ\n");
        STRU_LOGIN_RS rs;
        sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));
        printf("sent LOGIN_RS\n");
        break;
    }
    default:
        printf("received unknown packet type: %d\n", type);
        break;
    }
}

void AVServer::closeListenFd()
{
    if (m_listenFd >= 0) {
        close(m_listenFd);
        m_listenFd = -1;
    }
}
