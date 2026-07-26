#pragma once

#include "platform.hpp"

#include <string>

namespace ctf::net {

// Socket handle type.
#ifdef _WIN32
    using socket_t = SOCKET;
    constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
    using socket_t = int;
    constexpr socket_t invalid_socket = -1;
#endif

// Close a socket and invalidate the handle.
void close_socket(socket_t& s);

// Return a human-readable string describing the last socket error.
auto last_error() -> std::string;

}  // namespace ctf::net
