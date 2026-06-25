#ifndef AVSERVER_H
#define AVSERVER_H

#include <stdint.h>
#include <vector>

#include "MediaManager.h"

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
    void closeListenFd();

private:
    int m_listenFd;
    MediaManager m_mediaManager;
};

#endif // AVSERVER_H
