#pragma once

// Platform detection and one-time WSA init/cleanup.
// POSIX: no-op.  Windows: WSAStartup / WSACleanup.

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32")
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

#include <string>

namespace ctf::net {

// Initialise the networking subsystem.
// Returns true on success.  Safe to call multiple times (reference counted on
// Windows, idempotent on POSIX).
auto init_net() -> bool;

// Clean up the networking subsystem.  Call once per process on shutdown.
void cleanup_net();

}  // namespace ctf::net
