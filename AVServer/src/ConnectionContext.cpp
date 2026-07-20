#include "ConnectionContext.h"

#include <utility>

PendingSend::PendingSend(std::vector<char> &&frameData)
    : data(std::move(frameData)),
      offset(0)
{
}

ConnectionContext::ConnectionContext(int socketFd,
                                     const std::string &peerAddress,
                                     uint16_t port)
    : fd(socketFd),
      peerIp(peerAddress),
      peerPort(port),
      queuedBytes(0),
      lastActivity(std::time(nullptr)),
      closing(false)
{
    receiveBuffer.reserve(128 * 1024);
}
