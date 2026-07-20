#include "AVServer.h"

AVServer::AVServer()
{
}

AVServer::~AVServer()
{
}

bool AVServer::start(uint16_t port)
{
    return m_epollServer.start(port);
}
