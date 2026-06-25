#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

#include <winsock2.h>

class TcpClient : public QObject
{
    Q_OBJECT

public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient() override;

    bool connectToServer(const QString &ip, quint16 port, int timeoutMs = 3000);
    void disconnectFromServer();
    bool sendPacket(const char *data, int len);
    bool isConnected() const;

signals:
    void connected();
    void connectFailed(const QString &reason);
    void disconnected();
    void packetReceived(const QByteArray &packet);
    void errorOccurred(const QString &reason);

private:
    bool sendAll(const char *data, int len);
    bool recvAll(char *data, int len);
    void recvLoop();
    void closeSocketLocked();

private:
    SOCKET m_socket;
    std::thread m_recvThread;
    std::atomic_bool m_running;
    std::atomic_bool m_connected;
    mutable std::mutex m_socketMutex;
    bool m_wsaReady;
};

#endif // TCPCLIENT_H
