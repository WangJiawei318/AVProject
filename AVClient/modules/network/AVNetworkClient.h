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
    bool sendUploadResume(const QString &transferId,
                          const QString &resumeToken,
                          const QString &fileName,
                          qint64 expectedSize);
    bool sendUploadBlock(const QString &transferId,
                         qint64 offset,
                         const QByteArray &data);
    bool sendUploadFinish(const QString &transferId,
                          const QString &fileName,
                          qint64 fileSize);
    bool sendDownloadInit(const QString &fileName,
                          qint64 resumeOffset = 0,
                          qint64 expectedFileSize = 0,
                          qint64 expectedModifiedTime = 0);
    bool sendDownloadBlock(const QString &fileName,
                           qint64 offset,
                           int requestSize);
    bool sendDownloadFinish(const QString &fileName, qint64 fileSize);
    bool isConnected() const;
    QString serverIp() const;
    quint16 serverPort() const;

signals:
    void connectedChanged(bool connected);
    void logMessage(const QString &message);
    void pingResponse(const QString &message);
    void loginResponse(bool success, const QString &message);
    void mediaListReceived(const QString &payload);
    void uploadInitResponse(bool success,
                            const QString &transferId,
                            const QString &resumeToken,
                            qint64 resumeOffset,
                            const QString &finalFileName,
                            const QString &message);
    void uploadResumeResponse(bool success,
                              const QString &transferId,
                              qint64 resumeOffset,
                              const QString &finalFileName,
                            const QString &message);
    void uploadBlockResponse(bool success,
                             const QString &transferId,
                             qint64 receivedOffset,
                             const QString &message);
    void uploadFinishResponse(bool success,
                              const QString &fileName,
                              const QString &message);
    void downloadInitResponse(bool success,
                              const QString &fileName,
                              qint64 fileSize,
                              qint64 modifiedTime,
                              qint64 acceptedOffset,
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
    QString m_serverIp;
    quint16 m_serverPort;
};

#endif // AVNETWORKCLIENT_H
