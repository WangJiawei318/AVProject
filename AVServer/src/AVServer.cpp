#include "AVServer.h"

#include "av_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string boundedString(const char *value, size_t capacity)
{
    return std::string(value, strnlen(value, capacity));
}

void copyText(char *target, size_t capacity, const std::string &value)
{
    if (capacity == 0)
        return;
    strncpy(target, value.c_str(), capacity - 1);
    target[capacity - 1] = '\0';
}

} // namespace

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
    if (!m_mediaManager.ensureMediaDir()) {
        printf("failed to create or open media directory: %s\n", m_mediaManager.mediaDir().c_str());
        return false;
    }
    printf("media directory: %s\n", m_mediaManager.mediaDir().c_str());
    if (!m_uploadManager.ensureDirectories()) {
        printf("failed to create or open upload directory: %s\n",
               m_uploadManager.tempDir().c_str());
        return false;
    }
    printf("upload temp directory: %s\n", m_uploadManager.tempDir().c_str());

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
        m_uploadManager.abortAll();
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
    case DEF_PACK_MEDIA_LIST_RQ:
    {
        printf("received MEDIA_LIST_RQ\n");
        sendMediaList(clientFd);
        break;
    }
    case DEF_PACK_UPLOAD_INIT_RQ:
        handleUploadInit(clientFd, packet);
        break;
    case DEF_PACK_UPLOAD_BLOCK_RQ:
        handleUploadBlock(clientFd, packet);
        break;
    case DEF_PACK_UPLOAD_FINISH_RQ:
        handleUploadFinish(clientFd, packet);
        break;
    default:
        printf("received unknown packet type: %d\n", type);
        break;
    }
}

void AVServer::handleUploadInit(int clientFd, const std::vector<char> &packet)
{
    printf("received UPLOAD_INIT_RQ\n");
    STRU_UPLOAD_INIT_RS rs;
    if (packet.size() != sizeof(STRU_UPLOAD_INIT_RQ)) {
        copyText(rs.message, sizeof(rs.message), "invalid UPLOAD_INIT_RQ size");
        sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));
        return;
    }

    STRU_UPLOAD_INIT_RQ rq;
    memcpy(&rq, packet.data(), sizeof(rq));
    const std::string fileName = boundedString(rq.fileName, sizeof(rq.fileName));
    const std::string extension = boundedString(rq.extension, sizeof(rq.extension));
    std::string uploadId;
    std::string message;

    rs.result = m_uploadManager.createUpload(fileName,
                                             extension,
                                             rq.fileSize,
                                             &uploadId,
                                             &message) ? 1 : 0;
    copyText(rs.uploadId, sizeof(rs.uploadId), uploadId);
    copyText(rs.message, sizeof(rs.message), message);
    sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));

    if (rs.result)
        printf("upload_id created: %s file=%s size=%lld\n",
               uploadId.c_str(), fileName.c_str(), static_cast<long long>(rq.fileSize));
    else
        printf("upload init rejected: %s\n", message.c_str());
}

void AVServer::handleUploadBlock(int clientFd, const std::vector<char> &packet)
{
    STRU_UPLOAD_BLOCK_RS rs;
    if (packet.size() < sizeof(STRU_UPLOAD_BLOCK_RQ_HEADER)) {
        copyText(rs.message, sizeof(rs.message), "invalid UPLOAD_BLOCK_RQ size");
        sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));
        return;
    }

    STRU_UPLOAD_BLOCK_RQ_HEADER header;
    memcpy(&header, packet.data(), sizeof(header));
    const std::string uploadId = boundedString(header.uploadId, sizeof(header.uploadId));
    copyText(rs.uploadId, sizeof(rs.uploadId), uploadId);

    const size_t expectedSize = sizeof(header) +
            (header.dataSize > 0 ? static_cast<size_t>(header.dataSize) : 0);
    if (header.dataSize <= 0 ||
            header.dataSize > AV_UPLOAD_BLOCK_SIZE ||
            packet.size() != expectedSize) {
        copyText(rs.message, sizeof(rs.message), "invalid upload block payload");
        sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));
        return;
    }

    printf("received UPLOAD_BLOCK_RQ offset=%lld size=%d\n",
           static_cast<long long>(header.offset), header.dataSize);
    std::string message;
    rs.result = m_uploadManager.writeBlock(uploadId,
                                           header.offset,
                                           packet.data() + sizeof(header),
                                           header.dataSize,
                                           &rs.receivedOffset,
                                           &message) ? 1 : 0;
    copyText(rs.message, sizeof(rs.message), message);
    sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));
    if (!rs.result)
        printf("upload block rejected: %s\n", message.c_str());
}

void AVServer::handleUploadFinish(int clientFd, const std::vector<char> &packet)
{
    printf("received UPLOAD_FINISH_RQ\n");
    STRU_UPLOAD_FINISH_RS rs;
    if (packet.size() != sizeof(STRU_UPLOAD_FINISH_RQ)) {
        copyText(rs.message, sizeof(rs.message), "invalid UPLOAD_FINISH_RQ size");
        sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));
        return;
    }

    STRU_UPLOAD_FINISH_RQ rq;
    memcpy(&rq, packet.data(), sizeof(rq));
    const std::string uploadId = boundedString(rq.uploadId, sizeof(rq.uploadId));
    const std::string fileName = boundedString(rq.fileName, sizeof(rq.fileName));
    std::string savedFileName;
    std::string message;

    rs.result = m_uploadManager.finishUpload(uploadId,
                                             fileName,
                                             rq.fileSize,
                                             &savedFileName,
                                             &message) ? 1 : 0;
    copyText(rs.fileName, sizeof(rs.fileName), savedFileName);
    copyText(rs.message, sizeof(rs.message), message);
    sendPacket(clientFd, reinterpret_cast<const char *>(&rs), sizeof(rs));

    if (rs.result) {
        printf("upload finished\n");
        printf("saved to media/%s\n", savedFileName.c_str());
    } else {
        printf("upload finish rejected: %s\n", message.c_str());
    }
}

void AVServer::sendMediaList(int clientFd)
{
    printf("scan media directory\n");
    int mediaCount = 0;
    std::string payload = m_mediaManager.buildMediaListPayload(&mediaCount);
    printf("media count: %d\n", mediaCount);

    const int maxPayload = 1024 * 1024 - static_cast<int>(sizeof(STRU_MEDIA_LIST_RS_HEADER));
    if (static_cast<int>(payload.size()) > maxPayload) {
        payload.resize(maxPayload);
        printf("media list payload truncated to %d bytes\n", maxPayload);
    }

    STRU_MEDIA_LIST_RS_HEADER header;
    header.payloadSize = static_cast<int32_t>(payload.size());

    std::vector<char> packet(sizeof(header) + payload.size());
    memcpy(packet.data(), &header, sizeof(header));
    if (!payload.empty())
        memcpy(packet.data() + sizeof(header), payload.data(), payload.size());

    sendPacket(clientFd, packet.data(), static_cast<int>(packet.size()));
    printf("sent MEDIA_LIST_RS\n");
}

void AVServer::closeListenFd()
{
    if (m_listenFd >= 0) {
        close(m_listenFd);
        m_listenFd = -1;
    }
}
