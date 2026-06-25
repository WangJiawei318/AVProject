#ifndef AVNETWORKCLIENT_H
#define AVNETWORKCLIENT_H

#include <QObject>
#include <QString>

#include "TcpClient.h"

class AVNetworkClient : public QObject
{
    Q_OBJECT

public:
    explicit AVNetworkClient(QObject *parent = nullptr);
    ~AVNetworkClient() override;

    bool connectToServer(const QString &ip, quint16 port);
    void disconnectFromServer();
    bool sendPing();
    bool sendLogin(const QString &username, const QString &password);
    bool isConnected() const;

signals:
    void connectedChanged(bool connected);
    void logMessage(const QString &message);
    void pingResponse(const QString &message);
    void loginResponse(bool success, const QString &message);

private slots:
    void onConnected();
    void onConnectFailed(const QString &reason);
    void onDisconnected();
    void onPacketReceived(const QByteArray &packet);
    void onErrorOccurred(const QString &reason);

private:
    TcpClient *m_tcpClient;
};

#endif // AVNETWORKCLIENT_H
