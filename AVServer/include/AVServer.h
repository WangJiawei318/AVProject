#ifndef AVSERVER_H
#define AVSERVER_H

#include <stdint.h>
#include <vector>

#include "MediaManager.h"
#include "DownloadManager.h"
#include "UploadManager.h"

class AVServer
{
public:
    AVServer();
    ~AVServer();

    bool start(uint16_t port);

private:
    bool readExact(int fd, char *data, int len);
    bool sendPacket(int fd, const char *data, int len);
    void handleClient(int clientFd);
    void handlePacket(int clientFd, const std::vector<char> &packet);
    void sendMediaList(int clientFd);
    void handleUploadInit(int clientFd, const std::vector<char> &packet);
    void handleUploadBlock(int clientFd, const std::vector<char> &packet);
    void handleUploadFinish(int clientFd, const std::vector<char> &packet);
    void handleDownloadInit(int clientFd, const std::vector<char> &packet);
    void handleDownloadBlock(int clientFd, const std::vector<char> &packet);
    void handleDownloadFinish(int clientFd, const std::vector<char> &packet);
    void closeListenFd();

private:
    int m_listenFd;
    MediaManager m_mediaManager;
    DownloadManager m_downloadManager;
    UploadManager m_uploadManager;
};

#endif // AVSERVER_H
