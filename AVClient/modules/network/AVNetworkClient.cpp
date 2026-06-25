#include "AVNetworkClient.h"

#include "av_protocol.h"

#include <QDateTime>
#include <QTextCodec>

AVNetworkClient::AVNetworkClient(QObject *parent)
    : QObject(parent),
      m_tcpClient(new TcpClient(this))
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
    emit logMessage(QString("connecting to %1:%2").arg(ip).arg(port));
    return m_tcpClient->connectToServer(ip, port);
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
    QByteArray user = username.toUtf8();
    QByteArray pwd = password.toUtf8();
    strncpy(rq.username, user.constData(), AV_NAME_SIZE - 1);
    strncpy(rq.password, pwd.constData(), AV_NAME_SIZE - 1);

    bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent LOGIN_RQ" : "failed to send LOGIN_RQ");
    return ok;
}

bool AVNetworkClient::sendMediaListRequest()
{
    STRU_MEDIA_LIST_RQ rq;
    bool ok = m_tcpClient->sendPacket(reinterpret_cast<const char *>(&rq), sizeof(rq));
    emit logMessage(ok ? "sent MEDIA_LIST_RQ" : "failed to send MEDIA_LIST_RQ");
    return ok;
}

bool AVNetworkClient::isConnected() const
{
    return m_tcpClient->isConnected();
}

void AVNetworkClient::onConnected()
{
    emit connectedChanged(true);
    emit logMessage("connected");
}

void AVNetworkClient::onConnectFailed(const QString &reason)
{
    emit connectedChanged(false);
    emit logMessage(QString("connect failed: %1").arg(reason));
}

void AVNetworkClient::onDisconnected()
{
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
    case DEF_PACK_LOGIN_RS:
    {
        if (packet.size() < static_cast<int>(sizeof(STRU_LOGIN_RS))) {
            emit logMessage("received short LOGIN_RS");
            return;
        }
        const STRU_LOGIN_RS *rs = reinterpret_cast<const STRU_LOGIN_RS *>(packet.constData());
        QString message = QString::fromLocal8Bit(rs->message);
        emit logMessage(QString("received LOGIN_RS: %1").arg(message));
        emit loginResponse(rs->result != 0, message);
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
        emit logMessage(QString("received MEDIA_LIST_RS: %1 bytes").arg(header->payloadSize));
        emit mediaListReceived(text);
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
