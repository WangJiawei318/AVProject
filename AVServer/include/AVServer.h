#ifndef AVSERVER_H
#define AVSERVER_H

#include <stdint.h>
#include "EpollServer.h"

class AVServer
{
public:
    AVServer();
    ~AVServer();

    bool start(uint16_t port);

private:
    EpollServer m_epollServer;
};

#endif // AVSERVER_H
