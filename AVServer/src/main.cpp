#include "AVServer.h"

#include <stdint.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    uint16_t port = 8000;
    if (argc >= 2)
        port = static_cast<uint16_t>(atoi(argv[1]));

    AVServer server;
    if (!server.start(port))
        return 1;

    return 0;
}
