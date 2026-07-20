#ifndef PROTOCOLDISPATCHER_H
#define PROTOCOLDISPATCHER_H

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
    void dispatch(int clientFd,
                  const std::vector<char> &packet,
                  std::vector<std::vector<char> > *responses);
    void onClientDisconnected(int clientFd);
    void performMaintenance(std::time_t now);

private:
    void handlePing(int clientFd,
                    std::vector<std::vector<char> > *responses);
    void handleLogin(int clientFd,
                     std::vector<std::vector<char> > *responses);
    void handleMediaList(int clientFd,
                         std::vector<std::vector<char> > *responses);
    void handleUploadInit(int clientFd,
                          const std::vector<char> &packet,
                          std::vector<std::vector<char> > *responses);
    void handleUploadResume(int clientFd,
                            const std::vector<char> &packet,
                            std::vector<std::vector<char> > *responses);
    void handleUploadBlock(int clientFd,
                           const std::vector<char> &packet,
                           std::vector<std::vector<char> > *responses);
    void handleUploadFinish(int clientFd,
                            const std::vector<char> &packet,
                            std::vector<std::vector<char> > *responses);
    void handleDownloadInit(int clientFd,
                            const std::vector<char> &packet,
                            std::vector<std::vector<char> > *responses);
    void handleDownloadBlock(int clientFd,
                             const std::vector<char> &packet,
                             std::vector<std::vector<char> > *responses);
    void handleDownloadFinish(int clientFd,
                              const std::vector<char> &packet,
                              std::vector<std::vector<char> > *responses);

private:
    MediaManager m_mediaManager;
    UploadManager m_uploadManager;
    DownloadManager m_downloadManager;
};

#endif // PROTOCOLDISPATCHER_H
