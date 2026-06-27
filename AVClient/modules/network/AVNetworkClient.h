#ifndef AVNETWORKCLIENT_H
#define AVNETWORKCLIENT_H

#include <QObject>
#include <QByteArray>
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
    bool sendMediaListRequest();
    bool sendUploadInit(const QString &fileName,
                        const QString &extension,
                        qint64 fileSize);
    bool sendUploadBlock(const QString &uploadId,
                         qint64 offset,
                         const QByteArray &data);
    bool sendUploadFinish(const QString &uploadId,
                          const QString &fileName,
                          qint64 fileSize);
    bool sendDownloadInit(const QString &fileName);
    bool sendDownloadBlock(const QString &fileName,
                           qint64 offset,
                           int requestSize);
    bool sendDownloadFinish(const QString &fileName, qint64 fileSize);
    bool isConnected() const;

signals:
    void connectedChanged(bool connected);
    void logMessage(const QString &message);
    void pingResponse(const QString &message);
    void loginResponse(bool success, const QString &message);
    void mediaListReceived(const QString &payload);
    void uploadInitResponse(bool success,
                            const QString &uploadId,
                            const QString &message);
    void uploadBlockResponse(bool success,
                             const QString &uploadId,
                             qint64 receivedOffset,
                             const QString &message);
    void uploadFinishResponse(bool success,
                              const QString &fileName,
                              const QString &message);
    void downloadInitResponse(bool success,
                              const QString &fileName,
                              qint64 fileSize,
                              const QString &message);
    void downloadBlockResponse(bool success,
                               const QString &fileName,
                               qint64 offset,
                               const QByteArray &data,
                               const QString &message);
    void downloadFinishResponse(bool success,
                                const QString &fileName,
                                const QString &message);

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
