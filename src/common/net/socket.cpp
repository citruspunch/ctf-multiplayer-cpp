#include "socket.hpp"

#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace ctf::net {

auto set_non_blocking(socket_t fd) -> bool {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void close_socket(socket_t& s) {
    if (s != invalid_socket) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        s = invalid_socket;
    }
}

auto last_error() -> std::string {
#ifdef _WIN32
    int err = WSAGetLastError();
    switch (err) {
        case WSAEWOULDBLOCK: return "EWOULDBLOCK";
        case WSAECONNRESET:  return "ECONNRESET";
        case WSAEINTR:       return "EINTR";
        default:             return "WSA error " + std::to_string(err);
    }
#else
    int err = errno;
    switch (err) {
        case EAGAIN:    return "EAGAIN";
        case ECONNRESET: return "ECONNRESET";
        case EINTR:     return "EINTR";
        default:        return strerror(err);
    }
#endif
}

}  // namespace ctf::net
