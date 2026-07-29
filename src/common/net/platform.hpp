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

// Resolve the first non-loopback IPv4 address of this machine.
// Returns "127.0.0.1" as fallback if no suitable interface is found.
auto get_local_ipv4() -> std::string;

}  // namespace ctf::net
