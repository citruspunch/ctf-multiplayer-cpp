#include "socket.hpp"

namespace ctf::net {

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
