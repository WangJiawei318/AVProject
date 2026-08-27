#ifndef PROTOCOLDISPATCHER_H
#define PROTOCOLDISPATCHER_H

#include <stdint.h>
#include <ctime>
#include <string>
#include <vector>

#include "AuthService.h"
#include "DatabaseConnectionPool.h"
#include "DownloadManager.h"
#include "MediaRepository.h"
#include "UploadManager.h"

struct DispatchContext
{
    DispatchContext();

    uint64_t connectionId;
    int clientFd;
    bool authenticated;
    uint64_t userId;
    std::string username;
};

struct DispatchResult
{
    DispatchResult();

    std::vector<std::vector<char> > responses;
    bool hasAuthUpdate;
    bool authSuccess;
    uint64_t authenticatedUserId;
    std::string authenticatedUsername;
};

class ProtocolDispatcher
{
public:
    ProtocolDispatcher();

    bool initialize();
    bool isBusinessPacket(const std::vector<char> &packet,
                          int32_t *protocolType = nullptr) const;
    void dispatch(const DispatchContext &context,
                  const std::vector<char> &packet,
                  DispatchResult *result);
    void buildErrorResponse(const std::vector<char> &packet,
                            int32_t errorCode,
                            const std::string &message,
                            std::vector<std::vector<char> > *responses) const;
    void onClientDisconnected(uint64_t connectionId, int clientFd);
    void performMaintenance(std::time_t now);

private:
    bool requiresAuthentication(int32_t protocolType) const;
    void handlePing(const DispatchContext &context, DispatchResult *result);
    void handleRegister(const std::vector<char> &packet, DispatchResult *result);
    void handleLogin(const std::vector<char> &packet, DispatchResult *result);
    void handleMediaList(const DispatchContext &context,
                         const std::vector<char> &packet,
                         DispatchResult *result);
    void handleUploadInit(const DispatchContext &context,
                          const std::vector<char> &packet,
                          DispatchResult *result);
    void handleUploadResume(const DispatchContext &context,
                            const std::vector<char> &packet,
                            DispatchResult *result);
    void handleUploadBlock(const DispatchContext &context,
                           const std::vector<char> &packet,
                           DispatchResult *result);
    void handleUploadFinish(const DispatchContext &context,
                            const std::vector<char> &packet,
                            DispatchResult *result);
    void handleDownloadInit(const DispatchContext &context,
                            const std::vector<char> &packet,
                            DispatchResult *result);
    void handleDownloadBlock(const DispatchContext &context,
                             const std::vector<char> &packet,
                             DispatchResult *result);
    void handleDownloadFinish(const DispatchContext &context,
                              const std::vector<char> &packet,
                              DispatchResult *result);

    DatabaseConnectionPool m_databasePool;
    AuthService m_authService;
    MediaRepository m_mediaRepository;
    UploadManager m_uploadManager;
    DownloadManager m_downloadManager;
};

#endif // PROTOCOLDISPATCHER_H
