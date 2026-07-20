#ifndef CONNECTIONCONTEXT_H
#define CONNECTIONCONTEXT_H

#include <stddef.h>
#include <stdint.h>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

struct PendingSend
{
    explicit PendingSend(std::vector<char> &&frameData);

    std::vector<char> data;
    size_t offset;
};

class ConnectionContext
{
public:
    ConnectionContext(int socketFd,
                      uint64_t uniqueConnectionId,
                      const std::string &peerAddress,
                      uint16_t peerPort);

    int fd;
    uint64_t connectionId;
    std::string peerIp;
    uint16_t peerPort;
    std::vector<char> receiveBuffer;
    std::deque<PendingSend> sendQueue;
    size_t queuedBytes;
    std::time_t lastActivity;
    bool closing;
    bool businessTaskInFlight;
};

#endif // CONNECTIONCONTEXT_H
