#include "ConnectionContext.h"

#include <utility>

PendingSend::PendingSend(std::vector<char> &&frameData)
    : data(std::move(frameData)),
      offset(0)
{
}

ConnectionContext::ConnectionContext(int socketFd,
                                     uint64_t uniqueConnectionId,
                                     const std::string &peerAddress,
                                     uint16_t port)
    : fd(socketFd),
      connectionId(uniqueConnectionId),
      peerIp(peerAddress),
      peerPort(port),
      queuedBytes(0),
      lastActivity(std::time(nullptr)),
      closing(false),
      businessTaskInFlight(false)
{
    receiveBuffer.reserve(128 * 1024);
}
