#ifndef PROTOCOLDISPATCHER_H
#define PROTOCOLDISPATCHER_H

#include <stdint.h>
#include <string>
#include <ctime>
#include <vector>

#include "DownloadManager.h"
#include "MediaManager.h"
#include "UploadManager.h"

class ProtocolDispatcher
{
public:
    ProtocolDispatcher();

    bool initialize();
    bool isBusinessPacket(const std::vector<char> &packet,
                          int32_t *protocolType = nullptr) const;
    void dispatch(uint64_t connectionId,
                  int clientFd,
                  const std::vector<char> &packet,
                  std::vector<std::vector<char> > *responses);
    void buildErrorResponse(const std::vector<char> &packet,
                            const std::string &message,
                            std::vector<std::vector<char> > *responses) const;
    void onClientDisconnected(uint64_t connectionId, int clientFd);
    void performMaintenance(std::time_t now);

private:
    void handlePing(int clientFd,
                    std::vector<std::vector<char> > *responses);
    void handleLogin(int clientFd,
                     std::vector<std::vector<char> > *responses);
    void handleMediaList(uint64_t connectionId,
                         int clientFd,
                         std::vector<std::vector<char> > *responses);
    void handleUploadInit(uint64_t connectionId,
                          int clientFd,
                          const std::vector<char> &packet,
                          std::vector<std::vector<char> > *responses);
    void handleUploadResume(uint64_t connectionId,
                            int clientFd,
                            const std::vector<char> &packet,
                            std::vector<std::vector<char> > *responses);
    void handleUploadBlock(uint64_t connectionId,
                           int clientFd,
                           const std::vector<char> &packet,
                           std::vector<std::vector<char> > *responses);
    void handleUploadFinish(uint64_t connectionId,
                            int clientFd,
                            const std::vector<char> &packet,
                            std::vector<std::vector<char> > *responses);
    void handleDownloadInit(uint64_t connectionId,
                            int clientFd,
                            const std::vector<char> &packet,
                            std::vector<std::vector<char> > *responses);
    void handleDownloadBlock(uint64_t connectionId,
                             int clientFd,
                             const std::vector<char> &packet,
                             std::vector<std::vector<char> > *responses);
    void handleDownloadFinish(uint64_t connectionId,
                              int clientFd,
                              const std::vector<char> &packet,
                              std::vector<std::vector<char> > *responses);

private:
    MediaManager m_mediaManager;
    UploadManager m_uploadManager;
    DownloadManager m_downloadManager;
};

#endif // PROTOCOLDISPATCHER_H
