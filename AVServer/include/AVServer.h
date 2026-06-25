#ifndef AVSERVER_H
#define AVSERVER_H

#include <stdint.h>
#include <vector>

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
    void closeListenFd();

private:
    int m_listenFd;
};

#endif // AVSERVER_H
