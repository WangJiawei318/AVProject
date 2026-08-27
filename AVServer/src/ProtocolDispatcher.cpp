#include "ProtocolDispatcher.h"

#include "av_protocol.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {

const size_t kMaxResponseBodySize = 256 * 1024;

std::string boundedString(const char *value, size_t capacity)
{ return std::string(value, strnlen(value, capacity)); }

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

int32_t errorCodeForMessage(const std::string &message)
{
    if (message == "database temporarily unavailable")
        return AV_ERROR_DATABASE_UNAVAILABLE;
    if (message == "permission denied" ||
            message == "legacy upload task has no ownerUserId")
        return AV_ERROR_PERMISSION_DENIED;
    if (message.find("invalid") != std::string::npos ||
            message.find("mismatch") != std::string::npos)
        return AV_ERROR_INVALID_REQUEST;
    return AV_ERROR_INTERNAL;
}

std::string sanitizeField(const std::string &value)
{
    std::string result = value;
    for (std::size_t index = 0; index < result.size(); ++index) {
        if (result[index] == '|' || result[index] == '\n' || result[index] == '\r')
            result[index] = '_';
    }
    return result;
}

bool validStoredRecord(const MediaRecord &record)
{
    return record.mediaId != 0 && !record.storedName.empty() &&
            record.storagePath == "media/" + record.storedName &&
            record.storedName.find('/') == std::string::npos &&
            record.storedName.find('\\') == std::string::npos &&
            record.storedName.find("..") == std::string::npos;
}

} // namespace

DispatchContext::DispatchContext()
    : connectionId(0), clientFd(-1), authenticated(false), userId(0)
{
}

DispatchResult::DispatchResult()
    : hasAuthUpdate(false), authSuccess(false), authenticatedUserId(0)
{
}

ProtocolDispatcher::ProtocolDispatcher()
    : m_authService(m_databasePool),
      m_mediaRepository(m_databasePool)
{
}

bool ProtocolDispatcher::initialize()
{
    std::string error;
    if (!m_databasePool.initialize("config/db.conf", &error)) {
        std::printf("database initialization failed: %s\n", error.c_str());
        return false;
    }
    if (!m_authService.initialize(&error)) {
        std::printf("authentication initialization failed: %s\n", error.c_str());
        return false;
    }
    if (!m_uploadManager.initialize()) {
        std::printf("failed to initialize upload directories\n");
        return false;
    }
    std::printf("database pool connected host=%s port=%u database=%s size=%zu\n",
                m_databasePool.config().host.c_str(),
                m_databasePool.config().port,
                m_databasePool.config().database.c_str(),
                m_databasePool.config().poolSize);
    std::printf("media directory: media\n");
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
    return type == DEF_PACK_REGISTER_RQ || type == DEF_PACK_LOGIN_RQ ||
            requiresAuthentication(type);
}

void ProtocolDispatcher::dispatch(const DispatchContext &context,
                                  const std::vector<char> &packet,
                                  DispatchResult *result)
{
    if (!result || packet.size() < sizeof(PackType))
        return;
    PackType type = 0;
    std::memcpy(&type, packet.data(), sizeof(type));
    std::printf("received protocol type=%d fd=%d connectionId=%llu userId=%llu\n",
                type, context.clientFd,
                static_cast<unsigned long long>(context.connectionId),
                static_cast<unsigned long long>(context.userId));

    if (requiresAuthentication(type) &&
            (!context.authenticated || context.userId == 0)) {
        buildErrorResponse(packet, AV_ERROR_AUTH_REQUIRED,
                           "authentication required", &result->responses);
        return;
    }

    switch (type) {
    case DEF_PACK_PING_RQ: handlePing(context, result); break;
    case DEF_PACK_REGISTER_RQ: handleRegister(packet, result); break;
    case DEF_PACK_LOGIN_RQ: handleLogin(packet, result); break;
    case DEF_PACK_MEDIA_LIST_RQ: handleMediaList(context, packet, result); break;
    case DEF_PACK_UPLOAD_INIT_RQ: handleUploadInit(context, packet, result); break;
    case DEF_PACK_UPLOAD_RESUME_RQ: handleUploadResume(context, packet, result); break;
    case DEF_PACK_UPLOAD_BLOCK_RQ: handleUploadBlock(context, packet, result); break;
    case DEF_PACK_UPLOAD_FINISH_RQ: handleUploadFinish(context, packet, result); break;
    case DEF_PACK_DOWNLOAD_INIT_RQ: handleDownloadInit(context, packet, result); break;
    case DEF_PACK_DOWNLOAD_BLOCK_RQ: handleDownloadBlock(context, packet, result); break;
    case DEF_PACK_DOWNLOAD_FINISH_RQ: handleDownloadFinish(context, packet, result); break;
    default:
        std::printf("received unknown packet type=%d fd=%d\n", type, context.clientFd);
        break;
    }
}

void ProtocolDispatcher::buildErrorResponse(
        const std::vector<char> &packet, int32_t errorCode,
        const std::string &message,
        std::vector<std::vector<char> > *responses) const
{
    if (!responses || packet.size() < sizeof(PackType))
        return;
    PackType type = 0;
    std::memcpy(&type, packet.data(), sizeof(type));
#define APPEND_ERROR(ResponseType) \
    do { ResponseType response; response.errorCode = errorCode; \
         copyText(response.message, sizeof(response.message), message); \
         appendStructResponse(response, responses); } while (0)
    switch (type) {
    case DEF_PACK_REGISTER_RQ: APPEND_ERROR(STRU_REGISTER_RS); break;
    case DEF_PACK_LOGIN_RQ: APPEND_ERROR(STRU_LOGIN_RS); break;
    case DEF_PACK_MEDIA_LIST_RQ: APPEND_ERROR(STRU_MEDIA_LIST_RS_HEADER); break;
    case DEF_PACK_UPLOAD_INIT_RQ: APPEND_ERROR(STRU_UPLOAD_INIT_RS); break;
    case DEF_PACK_UPLOAD_RESUME_RQ: APPEND_ERROR(STRU_UPLOAD_RESUME_RS); break;
    case DEF_PACK_UPLOAD_BLOCK_RQ: APPEND_ERROR(STRU_UPLOAD_BLOCK_RS); break;
    case DEF_PACK_UPLOAD_FINISH_RQ: APPEND_ERROR(STRU_UPLOAD_FINISH_RS); break;
    case DEF_PACK_DOWNLOAD_INIT_RQ: APPEND_ERROR(STRU_DOWNLOAD_INIT_RS); break;
    case DEF_PACK_DOWNLOAD_BLOCK_RQ: APPEND_ERROR(STRU_DOWNLOAD_BLOCK_RS_HEADER); break;
    case DEF_PACK_DOWNLOAD_FINISH_RQ: APPEND_ERROR(STRU_DOWNLOAD_FINISH_RS); break;
    default: break;
    }
#undef APPEND_ERROR
}

void ProtocolDispatcher::onClientDisconnected(uint64_t connectionId, int clientFd)
{
    const size_t count = m_uploadManager.unbindConnection(connectionId);
    if (count > 0)
        std::printf("upload tasks unbound fd=%d connectionId=%llu count=%zu\n",
                    clientFd, static_cast<unsigned long long>(connectionId), count);
}

void ProtocolDispatcher::performMaintenance(std::time_t now)
{
    const size_t removed = m_uploadManager.cleanupExpiredTasks(now);
    if (removed > 0)
        std::printf("upload task maintenance removed=%zu\n", removed);
}

bool ProtocolDispatcher::requiresAuthentication(int32_t type) const
{
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

void ProtocolDispatcher::handlePing(const DispatchContext &context,
                                    DispatchResult *result)
{
    std::printf("PING_RQ fd=%d\n", context.clientFd);
    appendStructResponse(STRU_PING_RS(), &result->responses);
}

void ProtocolDispatcher::handleRegister(const std::vector<char> &packet,
                                        DispatchResult *result)
{
    STRU_REGISTER_RS response;
    if (packet.size() != sizeof(STRU_REGISTER_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid REGISTER_RQ size");
        appendStructResponse(response, &result->responses);
        return;
    }
    STRU_REGISTER_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const AuthResult auth = m_authService.registerUser(
                boundedString(request.username, sizeof(request.username)),
                boundedString(request.password, sizeof(request.password)));
    response.result = auth.success ? 1 : 0;
    response.errorCode = auth.errorCode;
    response.userId = auth.userId;
    copyText(response.message, sizeof(response.message), auth.message);
    appendStructResponse(response, &result->responses);
    std::printf("REGISTER_RQ result=%d userId=%llu\n", response.result,
                static_cast<unsigned long long>(response.userId));
}

void ProtocolDispatcher::handleLogin(const std::vector<char> &packet,
                                     DispatchResult *result)
{
    STRU_LOGIN_RS response;
    if (packet.size() != sizeof(STRU_LOGIN_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid LOGIN_RQ size");
        appendStructResponse(response, &result->responses);
        return;
    }
    STRU_LOGIN_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    const AuthResult auth = m_authService.login(
                boundedString(request.username, sizeof(request.username)),
                boundedString(request.password, sizeof(request.password)));
    response.result = auth.success ? 1 : 0;
    response.errorCode = auth.errorCode;
    response.userId = auth.userId;
    copyText(response.username, sizeof(response.username), auth.username);
    copyText(response.message, sizeof(response.message), auth.message);
    appendStructResponse(response, &result->responses);
    if (auth.success) {
        result->hasAuthUpdate = true;
        result->authSuccess = true;
        result->authenticatedUserId = auth.userId;
        result->authenticatedUsername = auth.username;
    }
    std::printf("LOGIN_RQ result=%d userId=%llu\n", response.result,
                static_cast<unsigned long long>(response.userId));
}

void ProtocolDispatcher::handleMediaList(const DispatchContext &context,
                                         const std::vector<char> &packet,
                                         DispatchResult *result)
{
    STRU_MEDIA_LIST_RS_HEADER header;
    if (packet.size() != sizeof(STRU_MEDIA_LIST_RQ)) {
        copyText(header.message, sizeof(header.message), "invalid MEDIA_LIST_RQ size");
        appendStructResponse(header, &result->responses);
        return;
    }
    STRU_MEDIA_LIST_RQ request;
    std::memcpy(&request, packet.data(), sizeof(request));
    std::vector<MediaRecord> records;
    std::string message;
    if (!m_mediaRepository.listMedia(request.scope, context.userId,
                                     request.page, request.pageSize,
                                     boundedString(request.keyword, sizeof(request.keyword)),
                                     &records, &message)) {
        header.errorCode = errorCodeForMessage(message);
        copyText(header.message, sizeof(header.message), message);
        appendStructResponse(header, &result->responses);
        return;
    }
    std::ostringstream payload;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const MediaRecord &item = records[index];
        payload << item.mediaId << '|' << item.ownerUserId << '|'
                << sanitizeField(item.ownerName) << '|'
                << sanitizeField(item.originalName) << '|'
                << item.fileSize << '|' << sanitizeField(item.extension) << '|'
                << sanitizeField(item.createdAt) << '|'
                << sanitizeField(item.status) << '\n';
    }
    std::string bytes = payload.str();
    const size_t maxPayload = kMaxResponseBodySize - sizeof(header);
    if (bytes.size() > maxPayload)
        bytes.resize(maxPayload);
    header.result = 1;
    header.errorCode = AV_ERROR_SUCCESS;
    header.payloadSize = static_cast<int32_t>(bytes.size());
    copyText(header.message, sizeof(header.message), "media list ready");
    std::vector<char> response(sizeof(header) + bytes.size());
    std::memcpy(response.data(), &header, sizeof(header));
    if (!bytes.empty())
        std::memcpy(response.data() + sizeof(header), bytes.data(), bytes.size());
    result->responses.push_back(response);
}

void ProtocolDispatcher::handleUploadInit(const DispatchContext &context,
                                          const std::vector<char> &packet,
                                          DispatchResult *result)
{
    STRU_UPLOAD_INIT_RS response;
    if (packet.size() != sizeof(STRU_UPLOAD_INIT_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid UPLOAD_INIT_RQ size");
        appendStructResponse(response, &result->responses); return;
    }
    STRU_UPLOAD_INIT_RQ request; std::memcpy(&request, packet.data(), sizeof(request));
    std::string transferId, token, storedName, message;
    response.result = m_uploadManager.createUpload(
                context.connectionId, context.userId,
                boundedString(request.fileName, sizeof(request.fileName)),
                boundedString(request.extension, sizeof(request.extension)),
                request.fileSize, &transferId, &token, &response.resumeOffset,
                &storedName, &message) ? 1 : 0;
    response.errorCode = response.result ? AV_ERROR_SUCCESS : errorCodeForMessage(message);
    copyText(response.transferId, sizeof(response.transferId), transferId);
    copyText(response.resumeToken, sizeof(response.resumeToken), token);
    copyText(response.finalFileName, sizeof(response.finalFileName), storedName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, &result->responses);
}

void ProtocolDispatcher::handleUploadResume(const DispatchContext &context,
                                            const std::vector<char> &packet,
                                            DispatchResult *result)
{
    STRU_UPLOAD_RESUME_RS response;
    if (packet.size() != sizeof(STRU_UPLOAD_RESUME_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid UPLOAD_RESUME_RQ size");
        appendStructResponse(response, &result->responses); return;
    }
    STRU_UPLOAD_RESUME_RQ request; std::memcpy(&request, packet.data(), sizeof(request));
    const std::string transferId = boundedString(request.transferId, sizeof(request.transferId));
    std::string storedName, message;
    response.result = m_uploadManager.resumeUpload(
                context.connectionId, context.userId, transferId,
                boundedString(request.resumeToken, sizeof(request.resumeToken)),
                boundedString(request.fileName, sizeof(request.fileName)),
                request.expectedSize, &response.resumeOffset, &storedName, &message) ? 1 : 0;
    response.errorCode = response.result ? AV_ERROR_SUCCESS : errorCodeForMessage(message);
    copyText(response.transferId, sizeof(response.transferId), transferId);
    copyText(response.finalFileName, sizeof(response.finalFileName), storedName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, &result->responses);
}

void ProtocolDispatcher::handleUploadBlock(const DispatchContext &context,
                                           const std::vector<char> &packet,
                                           DispatchResult *result)
{
    STRU_UPLOAD_BLOCK_RS response;
    if (packet.size() < sizeof(STRU_UPLOAD_BLOCK_RQ_HEADER)) {
        copyText(response.message, sizeof(response.message), "invalid UPLOAD_BLOCK_RQ size");
        appendStructResponse(response, &result->responses); return;
    }
    STRU_UPLOAD_BLOCK_RQ_HEADER request; std::memcpy(&request, packet.data(), sizeof(request));
    const std::string transferId = boundedString(request.transferId, sizeof(request.transferId));
    copyText(response.transferId, sizeof(response.transferId), transferId);
    const size_t expected = sizeof(request) +
            (request.dataSize > 0 ? static_cast<size_t>(request.dataSize) : 0);
    if (request.dataSize <= 0 || request.dataSize > AV_UPLOAD_BLOCK_SIZE || packet.size() != expected) {
        copyText(response.message, sizeof(response.message), "invalid upload block payload");
        appendStructResponse(response, &result->responses); return;
    }
    std::string message;
    response.result = m_uploadManager.writeBlock(
                context.connectionId, context.userId, transferId,
                request.offset, packet.data() + sizeof(request), request.dataSize,
                &response.receivedOffset, &message) ? 1 : 0;
    response.errorCode = response.result ? AV_ERROR_SUCCESS : errorCodeForMessage(message);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, &result->responses);
}

void ProtocolDispatcher::handleUploadFinish(const DispatchContext &context,
                                            const std::vector<char> &packet,
                                            DispatchResult *result)
{
    STRU_UPLOAD_FINISH_RS response;
    if (packet.size() != sizeof(STRU_UPLOAD_FINISH_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid UPLOAD_FINISH_RQ size");
        appendStructResponse(response, &result->responses); return;
    }
    STRU_UPLOAD_FINISH_RQ request; std::memcpy(&request, packet.data(), sizeof(request));
    std::string savedName, message;
    const UploadManager::MediaPublisher publisher =
            [this](const UploadManager::CompletedUpload &completed,
                   uint64_t *mediaId, std::string *publishMessage) {
        MediaRecord media;
        media.ownerUserId = completed.ownerUserId;
        media.originalName = completed.originalFileName;
        media.storedName = completed.storedFileName;
        media.storagePath = completed.storagePath;
        media.extension = completed.extension;
        media.fileSize = completed.fileSize;
        media.status = "published";
        return m_mediaRepository.insertMedia(media, mediaId, publishMessage);
    };
    response.result = m_uploadManager.finishUpload(
                context.connectionId, context.userId,
                boundedString(request.transferId, sizeof(request.transferId)),
                boundedString(request.fileName, sizeof(request.fileName)),
                request.fileSize, publisher, &savedName, &response.mediaId,
                &message) ? 1 : 0;
    response.errorCode = response.result ? AV_ERROR_SUCCESS : errorCodeForMessage(message);
    copyText(response.fileName, sizeof(response.fileName), savedName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, &result->responses);
}

void ProtocolDispatcher::handleDownloadInit(const DispatchContext &,
                                            const std::vector<char> &packet,
                                            DispatchResult *result)
{
    STRU_DOWNLOAD_INIT_RS response;
    if (packet.size() != sizeof(STRU_DOWNLOAD_INIT_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid DOWNLOAD_INIT_RQ size");
        appendStructResponse(response, &result->responses); return;
    }
    STRU_DOWNLOAD_INIT_RQ request; std::memcpy(&request, packet.data(), sizeof(request));
    response.mediaId = request.mediaId;
    MediaRecord media; std::string message;
    if (!m_mediaRepository.findMediaById(request.mediaId, &media, &message) ||
            !validStoredRecord(media)) {
        response.errorCode = errorCodeForMessage(message);
        copyText(response.message, sizeof(response.message), message.empty() ? "media not found" : message);
        appendStructResponse(response, &result->responses); return;
    }
    response.result = m_downloadManager.initializeDownload(
                media.storedName, request.resumeOffset, request.expectedFileSize,
                request.expectedModifiedTime, &response.fileSize,
                &response.modifiedTime, &response.acceptedOffset, &message) ? 1 : 0;
    if (response.result && response.fileSize != media.fileSize) {
        response.result = 0;
        message = "media file metadata mismatch";
    }
    response.errorCode = response.result ? AV_ERROR_SUCCESS : errorCodeForMessage(message);
    copyText(response.fileName, sizeof(response.fileName), media.originalName);
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, &result->responses);
}

void ProtocolDispatcher::handleDownloadBlock(const DispatchContext &,
                                             const std::vector<char> &packet,
                                             DispatchResult *result)
{
    STRU_DOWNLOAD_BLOCK_RS_HEADER header;
    if (packet.size() != sizeof(STRU_DOWNLOAD_BLOCK_RQ)) {
        copyText(header.message, sizeof(header.message), "invalid DOWNLOAD_BLOCK_RQ size");
        appendStructResponse(header, &result->responses); return;
    }
    STRU_DOWNLOAD_BLOCK_RQ request; std::memcpy(&request, packet.data(), sizeof(request));
    header.mediaId = request.mediaId; header.offset = request.offset;
    MediaRecord media; std::string message; std::vector<char> data;
    if (!m_mediaRepository.findMediaById(request.mediaId, &media, &message) ||
            !validStoredRecord(media)) {
        header.errorCode = errorCodeForMessage(message);
    } else {
        header.result = m_downloadManager.readBlock(
                    media.storedName, request.offset, request.requestSize,
                    &data, &message) ? 1 : 0;
        header.errorCode = header.result ? AV_ERROR_SUCCESS : errorCodeForMessage(message);
        copyText(header.fileName, sizeof(header.fileName), media.originalName);
    }
    header.dataSize = header.result ? static_cast<int32_t>(data.size()) : 0;
    copyText(header.message, sizeof(header.message), message);
    std::vector<char> response(sizeof(header) + data.size());
    std::memcpy(response.data(), &header, sizeof(header));
    if (!data.empty())
        std::memcpy(response.data() + sizeof(header), data.data(), data.size());
    result->responses.push_back(response);
}

void ProtocolDispatcher::handleDownloadFinish(const DispatchContext &,
                                              const std::vector<char> &packet,
                                              DispatchResult *result)
{
    STRU_DOWNLOAD_FINISH_RS response;
    if (packet.size() != sizeof(STRU_DOWNLOAD_FINISH_RQ)) {
        copyText(response.message, sizeof(response.message), "invalid DOWNLOAD_FINISH_RQ size");
        appendStructResponse(response, &result->responses); return;
    }
    STRU_DOWNLOAD_FINISH_RQ request; std::memcpy(&request, packet.data(), sizeof(request));
    response.mediaId = request.mediaId;
    MediaRecord media; std::string message;
    if (!m_mediaRepository.findMediaById(request.mediaId, &media, &message) ||
            !validStoredRecord(media)) {
        response.errorCode = errorCodeForMessage(message);
    } else {
        response.result = m_downloadManager.validateCompletion(
                    media.storedName, request.fileSize, &message) ? 1 : 0;
        response.errorCode = response.result ? AV_ERROR_SUCCESS : errorCodeForMessage(message);
        copyText(response.fileName, sizeof(response.fileName), media.originalName);
    }
    copyText(response.message, sizeof(response.message), message);
    appendStructResponse(response, &result->responses);
}
