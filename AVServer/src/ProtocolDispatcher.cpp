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

    if (!m_uploadManager.ensureDirectories()) {
        std::printf("failed to create or open upload directory: %s\n",
                    m_uploadManager.tempDir().c_str());
        return false;
    }
    std::printf("upload temp directory: %s\n", m_uploadManager.tempDir().c_str());
    return true;
}

void ProtocolDispatcher::dispatch(int clientFd,
                                  const std::vector<char> &packet,
                                  std::vector<std::vector<char> > *responses)
{
    if (!responses || packet.size() < sizeof(PackType)) {
        std::printf("received invalid packet fd=%d\n", clientFd);
        return;
    }

    PackType type = 0;
    std::memcpy(&type, packet.data(), sizeof(type));
    std::printf("received protocol type=%d fd=%d\n", type, clientFd);

    switch (type) {
    case DEF_PACK_PING_RQ:
        handlePing(clientFd, responses);
        break;
    case DEF_PACK_LOGIN_RQ:
        handleLogin(clientFd, responses);
        break;
    case DEF_PACK_MEDIA_LIST_RQ:
        handleMediaList(clientFd, responses);
        break;
    case DEF_PACK_UPLOAD_INIT_RQ:
        handleUploadInit(clientFd, packet, responses);
        break;
    case DEF_PACK_UPLOAD_BLOCK_RQ:
        handleUploadBlock(clientFd, packet, responses);
        break;
    case DEF_PACK_UPLOAD_FINISH_RQ:
        handleUploadFinish(clientFd, packet, responses);
        break;
    case DEF_PACK_DOWNLOAD_INIT_RQ:
        handleDownloadInit(clientFd, packet, responses);
        break;
    case DEF_PACK_DOWNLOAD_BLOCK_RQ:
        handleDownloadBlock(clientFd, packet, responses);
        break;
    case DEF_PACK_DOWNLOAD_FINISH_RQ:
        handleDownloadFinish(clientFd, packet, responses);
        break;
    default:
        std::printf("received unknown packet type=%d fd=%d\n", type, clientFd);
        break;
    }
}

void ProtocolDispatcher::onClientDisconnected(int clientFd)
{
    const size_t removed = m_uploadManager.abortByOwner(clientFd);
    if (removed > 0) {
        std::printf("cleaned unfinished uploads fd=%d count=%zu\n",
                    clientFd,
                    removed);
    }
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

void ProtocolDispatcher::handleMediaList(int clientFd,
                                         std::vector<std::vector<char> > *responses)
{
    std::printf("MEDIA_LIST_RQ fd=%d\n", clientFd);
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

void ProtocolDispatcher::handleUploadInit(int clientFd,
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
    std::string uploadId;
    std::string message;
    response.result = m_uploadManager.createUpload(clientFd,
                                                    fileName,
                                                    extension,
                                                    request.fileSize,
                                                    &uploadId,
                                                    &message) ? 1 : 0;
    copyText(response.uploadId, sizeof(response.uploadId), uploadId);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);

    std::printf("UPLOAD_INIT_RQ fd=%d upload_id=%s file=%s result=%d\n",
                clientFd, uploadId.c_str(), fileName.c_str(), response.result);
}

void ProtocolDispatcher::handleUploadBlock(int clientFd,
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
    const std::string uploadId = boundedString(header.uploadId, sizeof(header.uploadId));
    copyText(response.uploadId, sizeof(response.uploadId), uploadId);
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
    response.result = m_uploadManager.writeBlock(clientFd,
                                                  uploadId,
                                                  header.offset,
                                                  packet.data() + sizeof(header),
                                                  header.dataSize,
                                                  &response.receivedOffset,
                                                  &message) ? 1 : 0;
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("UPLOAD_BLOCK_RQ fd=%d offset=%lld size=%d result=%d\n",
                clientFd,
                static_cast<long long>(header.offset),
                header.dataSize,
                response.result);
}

void ProtocolDispatcher::handleUploadFinish(int clientFd,
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
    const std::string uploadId = boundedString(request.uploadId, sizeof(request.uploadId));
    const std::string fileName = boundedString(request.fileName, sizeof(request.fileName));
    std::string savedFileName;
    std::string message;
    response.result = m_uploadManager.finishUpload(clientFd,
                                                    uploadId,
                                                    fileName,
                                                    request.fileSize,
                                                    &savedFileName,
                                                    &message) ? 1 : 0;
    copyText(response.fileName, sizeof(response.fileName), savedFileName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("UPLOAD_FINISH_RQ fd=%d upload_id=%s saved=%s result=%d\n",
                clientFd, uploadId.c_str(), savedFileName.c_str(), response.result);
}

void ProtocolDispatcher::handleDownloadInit(int clientFd,
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
    response.result = m_downloadManager.getFileInfo(fileName,
                                                     &response.fileSize,
                                                     &message) ? 1 : 0;
    copyText(response.fileName, sizeof(response.fileName), fileName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, responses);
    std::printf("DOWNLOAD_INIT_RQ fd=%d file=%s size=%lld result=%d\n",
                clientFd,
                fileName.c_str(),
                static_cast<long long>(response.fileSize),
                response.result);
}

void ProtocolDispatcher::handleDownloadBlock(int clientFd,
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
    std::printf("DOWNLOAD_BLOCK_RQ fd=%d offset=%lld size=%d result=%d\n",
                clientFd,
                static_cast<long long>(request.offset),
                request.requestSize,
                responseHeader.result);
}

void ProtocolDispatcher::handleDownloadFinish(int clientFd,
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
    std::printf("DOWNLOAD_FINISH_RQ fd=%d file=%s result=%d\n",
                clientFd, fileName.c_str(), response.result);
}
