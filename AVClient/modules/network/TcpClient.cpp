#include "TcpClient.h"

#include <QHostAddress>

#include <ws2tcpip.h>

TcpClient::TcpClient(QObject *parent)
    : QObject(parent),
      m_socket(INVALID_SOCKET),
      m_running(false),
      m_connected(false),
      m_wsaReady(false)
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) == 0)
        m_wsaReady = true;
}

TcpClient::~TcpClient()
{
    disconnectFromServer();
    if (m_wsaReady)
        WSACleanup();
}

bool TcpClient::connectToServer(const QString &ip, quint16 port, int timeoutMs)
{
    disconnectFromServer();

    if (!m_wsaReady) {
        emit connectFailed("WSAStartup failed");
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        emit connectFailed(QString("socket failed: %1").arg(WSAGetLastError()));
        return false;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.toLocal8Bit().constData());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        closesocket(sock);
        emit connectFailed("invalid IPv4 address");
        return false;
    }

    u_long nonblock = 1;
    ioctlsocket(sock, FIONBIO, &nonblock);

    int ret = ::connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            closesocket(sock);
            emit connectFailed(QString("connect failed: %1").arg(err));
            return false;
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sock, &writeSet);

        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        ret = select(0, nullptr, &writeSet, nullptr, &tv);
        if (ret <= 0) {
            closesocket(sock);
            emit connectFailed(ret == 0 ? "connect timeout" : QString("select failed: %1").arg(WSAGetLastError()));
            return false;
        }

        int soError = 0;
        int len = sizeof(soError);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soError), &len);
        if (soError != 0) {
            closesocket(sock);
            emit connectFailed(QString("connect failed: %1").arg(soError));
            return false;
        }
    }

    nonblock = 0;
    ioctlsocket(sock, FIONBIO, &nonblock);

    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&nodelay), sizeof(nodelay));

    {
        std::lock_guard<std::mutex> guard(m_socketMutex);
        m_socket = sock;
    }

    m_running = true;
    m_connected = true;
    m_recvThread = std::thread(&TcpClient::recvLoop, this);

    emit connected();
    return true;
}

void TcpClient::disconnectFromServer()
{
    m_running = false;

    {
        std::lock_guard<std::mutex> guard(m_socketMutex);
        closeSocketLocked();
    }

    if (m_recvThread.joinable())
        m_recvThread.join();

    if (m_connected.exchange(false))
        emit disconnected();
}

bool TcpClient::sendPacket(const char *data, int len)
{
    if (!data || len <= 0 || !m_connected)
        return false;

    QByteArray frame;
    frame.resize(sizeof(int32_t) + len);
    memcpy(frame.data(), &len, sizeof(int32_t));
    memcpy(frame.data() + sizeof(int32_t), data, len);

    return sendAll(frame.constData(), frame.size());
}

bool TcpClient::isConnected() const
{
    return m_connected;
}

bool TcpClient::sendAll(const char *data, int len)
{
    int sent = 0;
    while (sent < len) {
        SOCKET sock;
        {
            std::lock_guard<std::mutex> guard(m_socketMutex);
            sock = m_socket;
        }

        if (sock == INVALID_SOCKET)
            return false;

        int ret = send(sock, data + sent, len - sent, 0);
        if (ret <= 0) {
            emit errorOccurred(QString("send failed: %1").arg(WSAGetLastError()));
            return false;
        }
        sent += ret;
    }
    return true;
}

bool TcpClient::recvAll(char *data, int len)
{
    int received = 0;
    while (m_running && received < len) {
        SOCKET sock;
        {
            std::lock_guard<std::mutex> guard(m_socketMutex);
            sock = m_socket;
        }

        if (sock == INVALID_SOCKET)
            return false;

        int ret = recv(sock, data + received, len - received, 0);
        if (ret <= 0)
            return false;

        received += ret;
    }
    return received == len;
}

void TcpClient::recvLoop()
{
    while (m_running) {
        int32_t packLen = 0;
        if (!recvAll(reinterpret_cast<char *>(&packLen), sizeof(packLen)))
            break;

        if (packLen <= 0 || packLen > 1024 * 1024) {
            emit errorOccurred(QString("invalid packet length: %1").arg(packLen));
            break;
        }

        QByteArray packet;
        packet.resize(packLen);
        if (!recvAll(packet.data(), packLen))
            break;

        emit packetReceived(packet);
    }

    m_running = false;
    {
        std::lock_guard<std::mutex> guard(m_socketMutex);
        closeSocketLocked();
    }

    if (m_connected.exchange(false))
        emit disconnected();
}

void TcpClient::closeSocketLocked()
{
    if (m_socket != INVALID_SOCKET) {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}
