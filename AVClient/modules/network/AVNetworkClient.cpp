#include "AVNetworkClient.h"

#include "av_protocol.h"

#include <QDateTime>

namespace {

QString utf8Field(const char *value, int capacity)
{
    int length = 0;
    while (length < capacity && value[length] != '\0')
        ++length;
    return QString::fromUtf8(value, length);
}

bool copyUtf8Field(char *target, int capacity, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    if (bytes.size() >= capacity)
        return false;
    memcpy(target, bytes.constData(), bytes.size());
    return true;
}

} // namespace

AVNetworkClient::AVNetworkClient(QObject *parent)
    : QObject(parent),
      m_tcpClient(new TcpClient(this)),
      m_serverPort(0),
      m_authenticated(false),
      m_currentUserId(0)
{
    connect(m_tcpClient, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(m_tcpClient, SIGNAL(connectFailed(QString)), this, SLOT(onConnectFailed(QString)));
    connect(m_tcpClient, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    connect(m_tcpClient, SIGNAL(packetReceived(QByteArray)), this, SLOT(onPacketReceived(QByteArray)));
    connect(m_tcpClient, SIGNAL(errorOccurred(QString)), this, SLOT(onErrorOccurred(QString)));
}

AVNetworkClient::~AVNetworkClient()
{
    disconnectFromServer();
}

bool AVNetworkClient::connectToServer(const QString &ip, quint16 port)
{
    m_serverIp = ip.trimmed();
    m_serverPort = port;
    emit logMessage(QString("connecting to %1:%2").arg(ip).arg(port));
    const bool connected = m_tcpClient->connectToServer(ip, port);
    if (!connected) {
        m_serverIp.clear();
        m_serverPort = 0;
    }
    return connected;
}

void AVNetworkClient::disconnectFromServer()
{
    m_tcpClient->disconnectFromServer();
}

bool AVNetworkClient::sendPing()
{
    STRU_PING_RQ rq;
    bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent PING_RQ" : "failed to send PING_RQ");
    return ok;
}

bool AVNetworkClient::sendLogin(const QString &username, const QString &password)
{
    STRU_LOGIN_RQ rq;
    if (!copyUtf8Field(rq.username, sizeof(rq.username), username) ||
            !copyUtf8Field(rq.password, sizeof(rq.password), password)) {
        emit logMessage("failed to send LOGIN_RQ: credentials are too long");
        return false;
    }

    bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent LOGIN_RQ" : "failed to send LOGIN_RQ");
    return ok;
}

bool AVNetworkClient::sendRegister(const QString &username, const QString &password)
{
    STRU_REGISTER_RQ rq;
    if (!copyUtf8Field(rq.username, sizeof(rq.username), username) ||
            !copyUtf8Field(rq.password, sizeof(rq.password), password)) {
        emit logMessage("failed to send REGISTER_RQ: credentials are too long");
        return false;
    }
    const bool ok = m_tcpClient->sendPacket(
                reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent REGISTER_RQ" : "failed to send REGISTER_RQ");
    return ok;
}

bool AVNetworkClient::sendMediaListRequest(int scope, int page, int pageSize,
                                           const QString &keyword)
{
    STRU_MEDIA_LIST_RQ rq;
    if (!copyUtf8Field(rq.keyword, sizeof(rq.keyword), keyword)) {
        emit logMessage("failed to send MEDIA_LIST_RQ: keyword is too long");
        return false;
    }
    rq.scope = scope;
    rq.page = page;
    rq.pageSize = pageSize;
    bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent MEDIA_LIST_RQ" : "failed to send MEDIA_LIST_RQ");
    return ok;
}

bool AVNetworkClient::sendUploadInit(const QString &fileName,
                                     const QString &extension,
                                     qint64 fileSize)
{
    STRU_UPLOAD_INIT_RQ rq;
    if (!copyUtf8Field(rq.fileName, sizeof(rq.fileName), fileName) ||
            !copyUtf8Field(rq.extension, sizeof(rq.extension), extension)) {
        emit logMessage("failed to send UPLOAD_INIT_RQ: file name is too long");
        return false;
    }
    rq.fileSize = fileSize;

    const bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent UPLOAD_INIT_RQ" : "failed to send UPLOAD_INIT_RQ");
    return ok;
}

bool AVNetworkClient::sendUploadResume(const QString &transferId,
                                       const QString &resumeToken,
                                       const QString &fileName,
                                       qint64 expectedSize)
{
    STRU_UPLOAD_RESUME_RQ rq;
    if (!copyUtf8Field(rq.transferId, sizeof(rq.transferId), transferId) ||
            !copyUtf8Field(rq.resumeToken, sizeof(rq.resumeToken), resumeToken) ||
            !copyUtf8Field(rq.fileName, sizeof(rq.fileName), fileName)) {
        emit logMessage("failed to send UPLOAD_RESUME_RQ: invalid metadata");
        return false;
    }
    rq.expectedSize = expectedSize;

    const bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq),
                                             sizeof(rq));
    emit logMessage(ok ? "sent UPLOAD_RESUME_RQ" : "failed to send UPLOAD_RESUME_RQ");
    return ok;
}

bool AVNetworkClient::sendUploadBlock(const QString &transferId,
                                      qint64 offset,
                                      const QByteArray &data)
{
    if (data.isEmpty() || data.size() > AV_UPLOAD_BLOCK_SIZE) {
        emit logMessage("failed to send UPLOAD_BLOCK_RQ: invalid block size");
        return false;
    }

    STRU_UPLOAD_BLOCK_RQ_HEADER header;
    if (!copyUtf8Field(header.transferId, sizeof(header.transferId), transferId)) {
        emit logMessage("failed to send UPLOAD_BLOCK_RQ: invalid transfer id");
        return false;
    }
    header.offset = offset;
    header.dataSize = data.size();

    QByteArray packet;
    packet.resize(sizeof(header) + data.size());
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data() + sizeof(header), data.constData(), data.size());

    const bool ok = m_tcpClient->sendPacket(packet.constData(), packet.size());
    if (!ok)
        emit logMessage("failed to send UPLOAD_BLOCK_RQ");
    return ok;
}

bool AVNetworkClient::sendUploadFinish(const QString &transferId,
                                       const QString &fileName,
                                       qint64 fileSize)
{
    STRU_UPLOAD_FINISH_RQ rq;
    if (!copyUtf8Field(rq.transferId, sizeof(rq.transferId), transferId) ||
            !copyUtf8Field(rq.fileName, sizeof(rq.fileName), fileName)) {
        emit logMessage("failed to send UPLOAD_FINISH_RQ: invalid metadata");
        return false;
    }
    rq.fileSize = fileSize;

    const bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent UPLOAD_FINISH_RQ" : "failed to send UPLOAD_FINISH_RQ");
    return ok;
}

bool AVNetworkClient::sendDownloadInit(quint64 mediaId,
                                       qint64 resumeOffset,
                                       qint64 expectedFileSize,
                                       qint64 expectedModifiedTime)
{
    STRU_DOWNLOAD_INIT_RQ rq;
    if (mediaId == 0) {
        emit logMessage("failed to send DOWNLOAD_INIT_RQ: invalid media id");
        return false;
    }
    rq.mediaId = mediaId;
    rq.resumeOffset = resumeOffset;
    rq.expectedFileSize = expectedFileSize;
    rq.expectedModifiedTime = expectedModifiedTime;

    const bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent DOWNLOAD_INIT_RQ" : "failed to send DOWNLOAD_INIT_RQ");
    return ok;
}

bool AVNetworkClient::sendDownloadBlock(quint64 mediaId,
                                        qint64 offset,
                                        int requestSize)
{
    if (requestSize <= 0 || requestSize > AV_UPLOAD_BLOCK_SIZE) {
        emit logMessage("failed to send DOWNLOAD_BLOCK_RQ: invalid block size");
        return false;
    }

    STRU_DOWNLOAD_BLOCK_RQ rq;
    if (mediaId == 0) {
        emit logMessage("failed to send DOWNLOAD_BLOCK_RQ: invalid media id");
        return false;
    }
    rq.mediaId = mediaId;
    rq.offset = offset;
    rq.requestSize = requestSize;

    const bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    if (!ok)
        emit logMessage("failed to send DOWNLOAD_BLOCK_RQ");
    return ok;
}

bool AVNetworkClient::sendDownloadFinish(quint64 mediaId, qint64 fileSize)
{
    STRU_DOWNLOAD_FINISH_RQ rq;
    if (mediaId == 0) {
        emit logMessage("failed to send DOWNLOAD_FINISH_RQ: invalid media id");
        return false;
    }
    rq.mediaId = mediaId;
    rq.fileSize = fileSize;

    const bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent DOWNLOAD_FINISH_RQ" : "failed to send DOWNLOAD_FINISH_RQ");
    return ok;
}

bool AVNetworkClient::isConnected() const
{
    return m_tcpClient->isConnected();
}

QString AVNetworkClient::serverIp() const
{
    return m_serverIp;
}

quint16 AVNetworkClient::serverPort() const
{
    return m_serverPort;
}

bool AVNetworkClient::isAuthenticated() const
{
    return m_authenticated;
}

quint64 AVNetworkClient::currentUserId() const
{
    return m_currentUserId;
}

QString AVNetworkClient::currentUsername() const
{
    return m_currentUsername;
}

void AVNetworkClient::onConnected()
{
    m_authenticated = false;
    m_currentUserId = 0;
    m_currentUsername.clear();
    emit authenticationChanged(false, 0, QString());
    emit connectedChanged(true);
    emit logMessage("connected");
}

void AVNetworkClient::onConnectFailed(const QString &reason)
{
    m_authenticated = false;
    m_currentUserId = 0;
    m_currentUsername.clear();
    emit authenticationChanged(false, 0, QString());
    emit connectedChanged(false);
    emit logMessage(QString("connect failed: %1").arg(reason));
}

void AVNetworkClient::onDisconnected()
{
    m_authenticated = false;
    m_currentUserId = 0;
    m_currentUsername.clear();
    emit authenticationChanged(false, 0, QString());
    emit connectedChanged(false);
    emit logMessage("disconnected");
}

void AVNetworkClient::onPacketReceived(const QByteArray &packet)
{
    if (packet.size() < static_cast<int>(sizeof(PackType))) {
        emit logMessage("received invalid packet");
        return;
    }

    PackType type = 0;
    memcpy(&type, packet.constData(), sizeof(type));

    switch (type) {
    case DEF_PACK_PING_RS:
    {
        if (packet.size() < static_cast<int>(sizeof(STRU_PING_RS))) {
            emit logMessage("received short PING_RS");
            return;
        }
        const STRU_PING_RS *rs = reinterpret_cast<const STRU_PING_RS *>(packet.constData());
        QString message = QString::fromLocal8Bit(rs->message);
        emit logMessage(QString("received PING_RS: %1").arg(message));
        emit pingResponse(message);
        break;
    }
    case DEF_PACK_REGISTER_RS:
    {
        if (packet.size() != static_cast<int>(sizeof(STRU_REGISTER_RS))) {
            emit logMessage("received invalid REGISTER_RS");
            return;
        }
        STRU_REGISTER_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        emit logMessage(QString("received REGISTER_RS: %1").arg(message));
        emit registerResponse(rs.result != 0, rs.errorCode, rs.userId, message);
        break;
    }
    case DEF_PACK_LOGIN_RS:
    {
        if (packet.size() < static_cast<int>(sizeof(STRU_LOGIN_RS))) {
            emit logMessage("received short LOGIN_RS");
            return;
        }
        STRU_LOGIN_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        const QString username = utf8Field(rs.username, sizeof(rs.username));
        emit logMessage(QString("received LOGIN_RS: %1").arg(message));
        if (rs.result != 0) {
            m_authenticated = true;
            m_currentUserId = rs.userId;
            m_currentUsername = username;
            emit authenticationChanged(true, m_currentUserId, m_currentUsername);
        }
        emit loginResponse(rs.result != 0, rs.errorCode, rs.userId,
                           username, message);
        break;
    }
    case DEF_PACK_MEDIA_LIST_RS:
    {
        if (packet.size() < static_cast<int>(sizeof(STRU_MEDIA_LIST_RS_HEADER))) {
            emit logMessage("received short MEDIA_LIST_RS");
            return;
        }
        const STRU_MEDIA_LIST_RS_HEADER *header =
                reinterpret_cast<const STRU_MEDIA_LIST_RS_HEADER *>(packet.constData());
        if (header->payloadSize < 0 ||
                packet.size() < static_cast<int>(sizeof(STRU_MEDIA_LIST_RS_HEADER) + header->payloadSize)) {
            emit logMessage("received invalid MEDIA_LIST_RS payload");
            return;
        }

        QByteArray payload = packet.mid(sizeof(STRU_MEDIA_LIST_RS_HEADER), header->payloadSize);
        QString text = QString::fromUtf8(payload);
        const QString message = utf8Field(header->message, sizeof(header->message));
        emit logMessage(QString("received MEDIA_LIST_RS: %1").arg(message));
        emit mediaListResponse(header->result != 0, text, message);
        break;
    }
    case DEF_PACK_UPLOAD_INIT_RS:
    {
        if (packet.size() != static_cast<int>(sizeof(STRU_UPLOAD_INIT_RS))) {
            emit logMessage("received invalid UPLOAD_INIT_RS");
            return;
        }
        STRU_UPLOAD_INIT_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString transferId = utf8Field(rs.transferId, sizeof(rs.transferId));
        const QString resumeToken = utf8Field(rs.resumeToken, sizeof(rs.resumeToken));
        const QString finalFileName = utf8Field(rs.finalFileName,
                                                sizeof(rs.finalFileName));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        emit logMessage(QString("received UPLOAD_INIT_RS: %1").arg(message));
        emit uploadInitResponse(rs.result != 0,
                                transferId,
                                resumeToken,
                                rs.resumeOffset,
                                finalFileName,
                                message);
        break;
    }
    case DEF_PACK_UPLOAD_RESUME_RS:
    {
        if (packet.size() != static_cast<int>(sizeof(STRU_UPLOAD_RESUME_RS))) {
            emit logMessage("received invalid UPLOAD_RESUME_RS");
            return;
        }
        STRU_UPLOAD_RESUME_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString transferId = utf8Field(rs.transferId, sizeof(rs.transferId));
        const QString finalFileName = utf8Field(rs.finalFileName,
                                                sizeof(rs.finalFileName));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        emit logMessage(QString("received UPLOAD_RESUME_RS: %1").arg(message));
        emit uploadResumeResponse(rs.result != 0,
                                  transferId,
                                  rs.resumeOffset,
                                  finalFileName,
                                  message);
        break;
    }
    case DEF_PACK_UPLOAD_BLOCK_RS:
    {
        if (packet.size() != static_cast<int>(sizeof(STRU_UPLOAD_BLOCK_RS))) {
            emit logMessage("received invalid UPLOAD_BLOCK_RS");
            return;
        }
        STRU_UPLOAD_BLOCK_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString transferId = utf8Field(rs.transferId, sizeof(rs.transferId));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        emit uploadBlockResponse(rs.result != 0,
                                 transferId,
                                 rs.receivedOffset,
                                 message);
        break;
    }
    case DEF_PACK_UPLOAD_FINISH_RS:
    {
        if (packet.size() != static_cast<int>(sizeof(STRU_UPLOAD_FINISH_RS))) {
            emit logMessage("received invalid UPLOAD_FINISH_RS");
            return;
        }
        STRU_UPLOAD_FINISH_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString fileName = utf8Field(rs.fileName, sizeof(rs.fileName));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        emit logMessage(QString("received UPLOAD_FINISH_RS: %1").arg(message));
        emit uploadFinishResponse(rs.result != 0, rs.mediaId, fileName, message);
        break;
    }
    case DEF_PACK_DOWNLOAD_INIT_RS:
    {
        if (packet.size() != static_cast<int>(sizeof(STRU_DOWNLOAD_INIT_RS))) {
            emit logMessage("received invalid DOWNLOAD_INIT_RS");
            emit downloadInitResponse(false,
                                      0,
                                      QString(),
                                      0,
                                      0,
                                      0,
                                      "invalid DOWNLOAD_INIT_RS");
            return;
        }
        STRU_DOWNLOAD_INIT_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString fileName = utf8Field(rs.fileName, sizeof(rs.fileName));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        emit logMessage(QString("received DOWNLOAD_INIT_RS: %1").arg(message));
        emit downloadInitResponse(rs.result != 0,
                                  rs.mediaId,
                                  fileName,
                                  rs.fileSize,
                                  rs.modifiedTime,
                                  rs.acceptedOffset,
                                  message);
        break;
    }
    case DEF_PACK_DOWNLOAD_BLOCK_RS:
    {
        if (packet.size() < static_cast<int>(sizeof(STRU_DOWNLOAD_BLOCK_RS_HEADER))) {
            emit logMessage("received short DOWNLOAD_BLOCK_RS");
            emit downloadBlockResponse(false,
                                       0,
                                       QString(),
                                       0,
                                       QByteArray(),
                                       "short DOWNLOAD_BLOCK_RS");
            return;
        }
        STRU_DOWNLOAD_BLOCK_RS_HEADER rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        if (rs.dataSize < 0 ||
                rs.dataSize > AV_UPLOAD_BLOCK_SIZE ||
                packet.size() != static_cast<int>(sizeof(rs) + rs.dataSize)) {
            emit logMessage("received invalid DOWNLOAD_BLOCK_RS payload");
            emit downloadBlockResponse(false,
                                       rs.mediaId,
                                       QString(),
                                       rs.offset,
                                       QByteArray(),
                                       "invalid DOWNLOAD_BLOCK_RS payload");
            return;
        }

        const QString fileName = utf8Field(rs.fileName, sizeof(rs.fileName));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        const QByteArray data = packet.mid(sizeof(rs), rs.dataSize);
        emit downloadBlockResponse(rs.result != 0,
                                   rs.mediaId,
                                   fileName,
                                   rs.offset,
                                   data,
                                   message);
        break;
    }
    case DEF_PACK_DOWNLOAD_FINISH_RS:
    {
        if (packet.size() != static_cast<int>(sizeof(STRU_DOWNLOAD_FINISH_RS))) {
            emit logMessage("received invalid DOWNLOAD_FINISH_RS");
            emit downloadFinishResponse(false,
                                         0,
                                         QString(),
                                         "invalid DOWNLOAD_FINISH_RS");
            return;
        }
        STRU_DOWNLOAD_FINISH_RS rs;
        memcpy(&rs, packet.constData(), sizeof(rs));
        const QString fileName = utf8Field(rs.fileName, sizeof(rs.fileName));
        const QString message = utf8Field(rs.message, sizeof(rs.message));
        emit logMessage(QString("received DOWNLOAD_FINISH_RS: %1").arg(message));
        emit downloadFinishResponse(rs.result != 0, rs.mediaId, fileName, message);
        break;
    }
    default:
        emit logMessage(QString("received unknown packet type: %1").arg(type));
        break;
    }
}

void AVNetworkClient::onErrorOccurred(const QString &reason)
{
    emit logMessage(QString("network error: %1").arg(reason));
}
