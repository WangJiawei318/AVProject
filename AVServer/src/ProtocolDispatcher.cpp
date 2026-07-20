#include "ProtocolDispatcher.h"

#include "av_protocol.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

const size_t kMaxResponseBodySize = 256 * 1024;

std::string boundedString(const char *value, size_t capacity)
{
    return std::string(value, strnlen(value, capacity));
}

void copyText(char *target, size_t capacity, const std::string &value)
{
    if (capacity == 0)
        return;
    std::strncpy(target, value.c_str(), capacity - 1);
    target[capacity - 1] = '\0';
}

template <typename T>
void appendStructResponse(const T &response,
                          std::vector<std::vector<char> > *responses)
{
    const char *begin = reinterpret_cast<const char *>(&response);
    responses->push_back(std::vector<char>(begin, begin + sizeof(response)));
}

} // namespace

ProtocolDispatcher::ProtocolDispatcher()
{
}

bool ProtocolDispatcher::initialize()
{
    if (!m_mediaManager.ensureMediaDir()) {
        std::printf("failed to create or open media directory: %s\n",
                    m_mediaManager.mediaDir().c_str());
        return false;
    }
    std::printf("media directory: %s\n", m_mediaManager.mediaDir().c_str());

    if (!m_uploadManager.initialize()) {
        std::printf("failed to create or open upload directory: %s\n",
                    m_uploadManager.tempDir().c_str());
        return false;
    }
    std::printf("upload temp directory: %s\n", m_uploadManager.tempDir().c_str());
    std::printf("upload task directory: %s\n", m_uploadManager.tasksDir().c_str());
    return true;
}

bool ProtocolDispatcher::isBusinessPacket(const std::vector<char> &packet,
                                          int32_t *protocolType) const
{
    if (packet.size() < sizeof(PackType))
        return false;
    PackType type = 0;
    std::memcpy(&type, packet.data(), sizeof(type));
    if (protocolType)
        *protocolType = type;
    switch (type) {
    case DEF_PACK_MEDIA_LIST_RQ:
    case DEF_PACK_UPLOAD_INIT_RQ:
    case DEF_PACK_UPLOAD_RESUME_RQ:
    case DEF_PACK_UPLOAD_BLOCK_RQ:
    case DEF_PACK_UPLOAD_FINISH_RQ:
    case DEF_PACK_DOWNLOAD_INIT_RQ:
    case DEF_PACK_DOWNLOAD_BLOCK_RQ:
    case DEF_PACK_DOWNLOAD_FINISH_RQ:
        return true;
    default:
        return false;
    }
}

void ProtocolDispatcher::dispatch(uint64_t connectionId,
                                  int clientFd,
                                  const std::vector<char> &packet,
                                  std::vector<std::vector<char> > *responses)
{
    if (!responses || packet.size() < sizeof(PackType)) {
        std::printf("received invalid packet fd=%d connectionId=%llu\n",
                    clientFd,
                    static_cast<unsigned long long>(connectionId));
        return;
    }

    PackType type = 0;
    std::memcpy(&type, packet.data(), sizeof(type));
    std::printf("received protocol type=%d fd=%d connectionId=%llu\n",
                type,
                clientFd,
                static_cast<unsigned long long>(connectionId));

    switch (type) {
    case DEF_PACK_PING_RQ:
        handlePing(clientFd, responses);
        break;
    case DEF_PACK_LOGIN_RQ:
        handleLogin(clientFd, responses);
        break;
    case DEF_PACK_MEDIA_LIST_RQ:
        handleMediaList(connectionId, clientFd, responses);
        break;
    case DEF_PACK_UPLOAD_INIT_RQ:
        handleUploadInit(connectionId, clientFd, packet, responses);
        break;
    case DEF_PACK_UPLOAD_RESUME_RQ:
        handleUploadResume(connectionId, clientFd, packet, responses);
        break;
    case DEF_PACK_UPLOAD_BLOCK_RQ:
        handleUploadBlock(connectionId, clientFd, packet, responses);
        break;
    case DEF_PACK_UPLOAD_FINISH_RQ:
        handleUploadFinish(connectionId, clientFd, packet, responses);
        break;
    case DEF_PACK_DOWNLOAD_INIT_RQ:
        handleDownloadInit(connectionId, clientFd, packet, responses);
        break;
    case DEF_PACK_DOWNLOAD_BLOCK_RQ:
        handleDownloadBlock(connectionId, clientFd, packet, responses);
        break;
    case DEF_PACK_DOWNLOAD_FINISH_RQ:
        handleDownloadFinish(connectionId, clientFd, packet, responses);
        break;
    default:
        std::printf("received unknown packet type=%d fd=%d connectionId=%llu\n",
                    type,
                    clientFd,
                    static_cast<unsigned long long>(connectionId));
        break;
    }
}

void ProtocolDispatcher::buildErrorResponse(
        const std::vector<char> &packet,
        const std::string &message,
        std::vector<std::vector<char> > *responses) const
{
    if (!responses || packet.size() < sizeof(PackType))
        return;
    PackType type = 0;
    std::memcpy(&type, packet.data(), sizeof(type));
    switch (type) {
    case DEF_PACK_MEDIA_LIST_RQ:
    {
        STRU_MEDIA_LIST_RS_HEADER header;
        header.payloadSize = static_cast<int32_t>(message.size());
        std::vector<char> response(sizeof(header) + message.size());
        std::memcpy(response.data(), &header, sizeof(header));
        if (!message.empty())
            std::memcpy(response.data() + sizeof(header), message.data(), message.size());
        responses->push_back(response);
        break;
    }
    case DEF_PACK_UPLOAD_INIT_RQ:
    {
        STRU_UPLOAD_INIT_RS response;
        copyText(response.message, sizeof(response.message), message);
        appendStructResponse(response, responses);
        break;
    }
    case DEF_PACK_UPLOAD_RESUME_RQ:
    {
        STRU_UPLOAD_RESUME_RS response;
        if (packet.size() == sizeof(STRU_UPLOAD_RESUME_RQ)) {
            STRU_UPLOAD_RESUME_RQ request;
            std::memcpy(&request, packet.data(), sizeof(request));
            copyText(response.transferId, sizeof(response.transferId),
                     boundedString(request.transferId, sizeof(request.transferId)));
        }
        copyText(response.message, sizeof(response.message), message);
        appendStructResponse(response, responses);
        break;
    }
    case DEF_PACK_UPLOAD_BLOCK_RQ:
    {
        STRU_UPLOAD_BLOCK_RS response;
        if (packet.size() >= sizeof(STRU_UPLOAD_BLOCK_RQ_HEADER)) {
            STRU_UPLOAD_BLOCK_RQ_HEADER request;
            std::memcpy(&request, packet.data(), sizeof(request));
            copyText(response.transferId, sizeof(response.transferId),
                     boundedString(request.transferId, sizeof(request.transferId)));
        }
        copyText(response.message, sizeof(response.message), message);
        appendStructResponse(response, responses);
        break;
    }
    case DEF_PACK_UPLOAD_FINISH_RQ:
    {
        STRU_UPLOAD_FINISH_RS response;
        copyText(response.message, sizeof(response.message), message);
        appendStructResponse(response, responses);
        break;
    }
    case DEF_PACK_DOWNLOAD_INIT_RQ:
    {
        STRU_DOWNLOAD_INIT_RS response;
        if (packet.size() == sizeof(STRU_DOWNLOAD_INIT_RQ)) {
            STRU_DOWNLOAD_INIT_RQ request;
            std::memcpy(&request, packet.data(), sizeof(request));
            copyText(response.fileName, sizeof(response.fileName),
                     boundedString(request.fileName, sizeof(request.fileName)));
        }
        copyText(response.message, sizeof(response.message), message);
        appendStructResponse(response, responses);
        break;
    }
    case DEF_PACK_DOWNLOAD_BLOCK_RQ:
    {
        STRU_DOWNLOAD_BLOCK_RS_HEADER response;
        if (packet.size() == sizeof(STRU_DOWNLOAD_BLOCK_RQ)) {
            STRU_DOWNLOAD_BLOCK_RQ request;
            std::memcpy(&request, packet.data(), sizeof(request));
            copyText(response.fileName, sizeof(response.fileName),
                     boundedString(request.fileName, sizeof(request.fileName)));
            response.offset = request.offset;
        }
        copyText(response.message, sizeof(response.message), message);
        appendStructResponse(response, responses);
        break;
    }
    case DEF_PACK_DOWNLOAD_FINISH_RQ:
    {
        STRU_DOWNLOAD_FINISH_RS response;
        if (packet.size() == sizeof(STRU_DOWNLOAD_FINISH_RQ)) {
            STRU_DOWNLOAD_FINISH_RQ request;
            std::memcpy(&request, packet.data(), sizeof(request));
            copyText(response.fileName, sizeof(response.fileName),
                     boundedString(request.fileName, sizeof(request.fileName)));
        }
        copyText(response.message, sizeof(response.message), message);
        appendStructResponse(response, responses);
        break;
    }
    default:
        break;
    }
}

void ProtocolDispatcher::onClientDisconnected(uint64_t connectionId,
                                              int clientFd)
{
    const size_t unbound = m_uploadManager.unbindConnection(connectionId);
    if (unbound > 0) {
        std::printf("upload tasks unbound fd=%d connectionId=%llu count=%zu\n",
                    clientFd,
                    static_cast<unsigned long long>(connectionId),
                    unbound);
    }
}

void ProtocolDispatcher::performMaintenance(std::time_t now)
{
    const size_t removed = m_uploadManager.cleanupExpiredTasks(now);
    if (removed > 0)
        std::printf("upload task maintenance removed=%zu\n", removed);
}

void ProtocolDispatcher::handlePing(int clientFd,
                                    std::vector<std::vector<char> > *responses)
{
    std::printf("PING_RQ fd=%d\n", clientFd);
    STRU_PING_RS response;
    appendStructResponse(response, responses);
}

void ProtocolDispatcher::handleLogin(int clientFd,
                                     std::vector<std::vector<char> > *responses)
{
    std::printf("LOGIN_RQ fd=%d\n", clientFd);
    STRU_LOGIN_RS response;
    appendStructResponse(response, responses);
}

void ProtocolDispatcher::handleMediaList(uint64_t connectionId,
                                         int clientFd,
                                         std::vector<std::vector<char> > *responses)
{
    std::printf("MEDIA_LIST_RQ fd=%d connectionId=%llu\n",
                clientFd,
                static_cast<unsigned long long>(connectionId));
    int mediaCount = 0;
    std::string payload = m_mediaManager.buildMediaListPayload(&mediaCount);
    const size_t maxPayload = kMaxResponseBodySize - sizeof(STRU_MEDIA_LIST_RS_HEADER);
    if (payload.size() > maxPayload) {
        payload.resize(maxPayload);
        std::printf("media list payload truncated fd=%d bytes=%zu\n",
                    clientFd, payload.size());
    }

    STRU_MEDIA_LIST_RS_HEADER header;
    header.payloadSize = static_cast<int32_t>(payload.size());
    std::vector<char> response(sizeof(header) + payload.size());
    std::memcpy(response.data(), &header, sizeof(header));
    if (!payload.empty())
        std::memcpy(response.data() + sizeof(header), payload.data(), payload.size());
    responses->push_back(response);
    std::printf("MEDIA_LIST_RS fd=%d media_count=%d\n", clientFd, mediaCount);
}

void ProtocolDispatcher::handleUploadInit(uint64_t connectionId,
                                          int clientFd,
                                          const std::vector<char> &packet,
                                          std::vector<std::vector<char> > *responses)
{
    STRU_UPLOAD_INIT_RS response;
    if (packet.size() != sizeof(STRU_UPLOAD_INIT_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid UPLOAD_INIT_RQ size");
        appendStructResponse(response, responses);
        return;
    }

    STRU_UPLOAD_INIT_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const std::string fileName = boundedString(request.fileName, sizeof(request.fileName));
    const std::string extension = boundedString(request.extension, sizeof(request.extension));
    std::string transferId;
    std::string resumeToken;
    std::string finalFileName;
    std::string message;
    response.result = m_uploadManager.createUpload(connectionId,
                                                    fileName,
                                                    extension,
                                                    request.fileSize,
                                                    &transferId,
                                                    &resumeToken,
                                                    &response.resumeOffset,
                                                    &finalFileName,
                                                    &message) ? 1 : 0;
    copyText(response.transferId, sizeof(response.transferId), transferId);
    copyText(response.resumeToken, sizeof(response.resumeToken), resumeToken);
    copyText(response.finalFileName, sizeof(response.finalFileName), finalFileName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);

    std::printf("UPLOAD_INIT_RQ fd=%d connectionId=%llu transfer_id=%s file=%s result=%d\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                transferId.c_str(),
                fileName.c_str(),
                response.result);
}

void ProtocolDispatcher::handleUploadResume(
        uint64_t connectionId,
        int clientFd,
        const std::vector<char> &packet,
        std::vector<std::vector<char> > *responses)
{
    STRU_UPLOAD_RESUME_RS response;
    if (packet.size() != sizeof(STRU_UPLOAD_RESUME_RQ)) {
        copyText(response.message, sizeof(response.message),
                 "invalid UPLOAD_RESUME_RQ size");
        appendStructResponse(response, responses);
        return;
    }

    STRU_UPLOAD_RESUME_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const std::string transferId = boundedString(request.transferId,
                                                  sizeof(request.transferId));
    const std::string resumeToken = boundedString(request.resumeToken,
                                                   sizeof(request.resumeToken));
    const std::string fileName = boundedString(request.fileName,
                                                sizeof(request.fileName));
    std::string finalFileName;
    std::string message;
    response.result = m_uploadManager.resumeUpload(connectionId,
                                                    transferId,
                                                    resumeToken,
                                                    fileName,
                                                    request.expectedSize,
                                                    &response.resumeOffset,
                                                    &finalFileName,
                                                    &message) ? 1 : 0;
    copyText(response.transferId, sizeof(response.transferId), transferId);
    copyText(response.finalFileName, sizeof(response.finalFileName), finalFileName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("UPLOAD_RESUME_RQ fd=%d connectionId=%llu transfer_id=%s offset=%lld result=%d\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                transferId.c_str(),
                static_cast<long long>(response.resumeOffset),
                response.result);
}

void ProtocolDispatcher::handleUploadBlock(uint64_t connectionId,
                                           int clientFd,
                                           const std::vector<char> &packet,
                                           std::vector<std::vector<char> > *responses)
{
    STRU_UPLOAD_BLOCK_RS response;
    if (packet.size() < sizeof(STRU_UPLOAD_BLOCK_RQ_HEADER)) {
        copyText(response.message, sizeof(response.message), "invalid UPLOAD_BLOCK_RQ size");
        appendStructResponse(response, responses);
        return;
    }

    STRU_UPLOAD_BLOCK_RQ_HEADER header;
    std::memcpy(&header, packet.data(), sizeof(header));
    const std::string transferId = boundedString(header.transferId,
                                                  sizeof(header.transferId));
    copyText(response.transferId, sizeof(response.transferId), transferId);
    const size_t expectedSize = sizeof(header) +
            (header.dataSize > 0 ? static_cast<size_t>(header.dataSize) : 0);
    if (header.dataSize <= 0 ||
            header.dataSize > AV_UPLOAD_BLOCK_SIZE ||
            packet.size() != expectedSize) {
        copyText(response.message, sizeof(response.message), "invalid upload block payload");
        appendStructResponse(response, responses);
        return;
    }

    std::string message;
    response.result = m_uploadManager.writeBlock(connectionId,
                                                  transferId,
                                                  header.offset,
                                                  packet.data() + sizeof(header),
                                                  header.dataSize,
                                                  &response.receivedOffset,
                                                  &message) ? 1 : 0;
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("UPLOAD_BLOCK_RQ fd=%d connectionId=%llu offset=%lld size=%d result=%d\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                static_cast<long long>(header.offset),
                header.dataSize,
                response.result);
}

void ProtocolDispatcher::handleUploadFinish(uint64_t connectionId,
                                            int clientFd,
                                            const std::vector<char> &packet,
                                            std::vector<std::vector<char> > *responses)
{
    STRU_UPLOAD_FINISH_RS response;
    if (packet.size() != sizeof(STRU_UPLOAD_FINISH_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid UPLOAD_FINISH_RQ size");
        appendStructResponse(response, responses);
        return;
    }

    STRU_UPLOAD_FINISH_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const std::string transferId = boundedString(request.transferId,
                                                  sizeof(request.transferId));
    const std::string fileName = boundedString(request.fileName, sizeof(request.fileName));
    std::string savedFileName;
    std::string message;
    response.result = m_uploadManager.finishUpload(connectionId,
                                                    transferId,
                                                    fileName,
                                                    request.fileSize,
                                                    &savedFileName,
                                                    &message) ? 1 : 0;
    copyText(response.fileName, sizeof(response.fileName), savedFileName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("UPLOAD_FINISH_RQ fd=%d connectionId=%llu transfer_id=%s saved=%s result=%d\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                transferId.c_str(),
                savedFileName.c_str(),
                response.result);
}

void ProtocolDispatcher::handleDownloadInit(uint64_t connectionId,
                                            int clientFd,
                                            const std::vector<char> &packet,
                                            std::vector<std::vector<char> > *responses)
{
    STRU_DOWNLOAD_INIT_RS response;
    if (packet.size() != sizeof(STRU_DOWNLOAD_INIT_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid DOWNLOAD_INIT_RQ size");
        appendStructResponse(response, responses);
        return;
    }

    STRU_DOWNLOAD_INIT_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const std::string fileName = boundedString(request.fileName, sizeof(request.fileName));
    std::string message;
    response.result = m_downloadManager.initializeDownload(
                fileName,
                request.resumeOffset,
                request.expectedFileSize,
                request.expectedModifiedTime,
                &response.fileSize,
                &response.modifiedTime,
                &response.acceptedOffset,
                &message) ? 1 : 0;
    copyText(response.fileName, sizeof(response.fileName), fileName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("DOWNLOAD_INIT_RQ fd=%d connectionId=%llu filename=%s requested_offset=%lld result=%d\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                fileName.c_str(),
                static_cast<long long>(request.resumeOffset),
                response.result);
    if (response.result && request.resumeOffset > 0) {
        std::printf("download resume accepted offset=%lld filename=%s\n",
                    static_cast<long long>(response.acceptedOffset),
                    fileName.c_str());
    } else if (!response.result && message == "remote file changed") {
        std::printf("download resume rejected: remote file changed filename=%s\n",
                    fileName.c_str());
    }
}

void ProtocolDispatcher::handleDownloadBlock(uint64_t connectionId,
                                             int clientFd,
                                             const std::vector<char> &packet,
                                             std::vector<std::vector<char> > *responses)
{
    STRU_DOWNLOAD_BLOCK_RS_HEADER responseHeader;
    if (packet.size() != sizeof(STRU_DOWNLOAD_BLOCK_RQ)) {
        copyText(responseHeader.message,
                 sizeof(responseHeader.message),
                 "invalid DOWNLOAD_BLOCK_RQ size");
        appendStructResponse(responseHeader, responses);
        return;
    }

    STRU_DOWNLOAD_BLOCK_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const std::string fileName = boundedString(request.fileName, sizeof(request.fileName));
    copyText(responseHeader.fileName, sizeof(responseHeader.fileName), fileName);
    responseHeader.offset = request.offset;

    std::vector<char> data;
    std::string message;
    responseHeader.result = m_downloadManager.readBlock(fileName,
                                                         request.offset,
                                                         request.requestSize,
                                                         &data,
                                                         &message) ? 1 : 0;
    responseHeader.dataSize = responseHeader.result
            ? static_cast<int32_t>(data.size()) : 0;
    copyText(responseHeader.message, sizeof(responseHeader.message), message);

    std::vector<char> response(sizeof(responseHeader) + data.size());
    std::memcpy(response.data(), &responseHeader, sizeof(responseHeader));
    if (!data.empty())
        std::memcpy(response.data() + sizeof(responseHeader), data.data(), data.size());
    responses->push_back(response);
    std::printf("DOWNLOAD_BLOCK_RQ fd=%d connectionId=%llu offset=%lld size=%d result=%d\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                static_cast<long long>(request.offset),
                request.requestSize,
                responseHeader.result);
}

void ProtocolDispatcher::handleDownloadFinish(uint64_t connectionId,
                                              int clientFd,
                                              const std::vector<char> &packet,
                                              std::vector<std::vector<char> > *responses)
{
    STRU_DOWNLOAD_FINISH_RS response;
    if (packet.size() != sizeof(STRU_DOWNLOAD_FINISH_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid DOWNLOAD_FINISH_RQ size");
        appendStructResponse(response, responses);
        return;
    }

    STRU_DOWNLOAD_FINISH_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const std::string fileName = boundedString(request.fileName, sizeof(request.fileName));
    std::string message;
    response.result = m_downloadManager.validateCompletion(fileName,
                                                            request.fileSize,
                                                            &message) ? 1 : 0;
    copyText(response.fileName, sizeof(response.fileName), fileName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("download finished fd=%d connectionId=%llu filename=%s result=%d\n",
                clientFd,
                static_cast<unsigned long long>(connectionId),
                fileName.c_str(),
                response.result);
}
