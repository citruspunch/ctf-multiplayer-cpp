#include "tcp_socket.hpp"

#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace ctf::net {

// ── TcpSocket ────────────────────────────────────────────────────────────

TcpSocket::TcpSocket() noexcept {}

TcpSocket::TcpSocket(socket_t native) noexcept
    : fd_(native) {}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    : fd_(other.fd_)
{
    other.fd_ = invalid_socket;
}

auto TcpSocket::operator=(TcpSocket&& other) noexcept -> TcpSocket& {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = invalid_socket;
    }
    return *this;
}

TcpSocket::~TcpSocket() { close(); }

auto TcpSocket::listen(int port) -> TcpSocket {
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == invalid_socket) return TcpSocket{};

    // SO_REUSEADDR
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_socket(fd);
        return TcpSocket{};
    }

    if (::listen(fd, 128) < 0) {
        close_socket(fd);
        return TcpSocket{};
    }

    if (!set_non_blocking(fd)) {
        close_socket(fd);
        return TcpSocket{};
    }

    return TcpSocket{fd};
}

auto TcpSocket::accept() -> std::optional<TcpSocket> {
    sockaddr_in client_addr{};
#ifdef _WIN32
    int addr_len = sizeof(client_addr);
#else
    socklen_t addr_len = sizeof(client_addr);
#endif
    socket_t client = ::accept(fd_,
                                reinterpret_cast<sockaddr*>(&client_addr),
                                &addr_len);
    if (client == invalid_socket) {
        // EAGAIN / EWOULDBLOCK is normal for non-blocking.
        return std::nullopt;
    }
    // Make the accepted client socket non-blocking too.
    set_non_blocking(client);
    return TcpSocket{client};
}

auto TcpSocket::recv(char* buf, std::size_t len) -> ssize_t {
#ifdef _WIN32
    auto ret = ::recv(fd_, buf, static_cast<int>(len), 0);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return -1;
        return -1;
    }
    return static_cast<ssize_t>(ret);
#else
    auto ret = ::read(fd_, buf, len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    }
    return ret;
#endif
}

auto TcpSocket::send(const char* data, std::size_t len) -> ssize_t {
#ifdef _WIN32
    auto ret = ::send(fd_, data, static_cast<int>(len), 0);
    if (ret == SOCKET_ERROR) return -1;
    return static_cast<ssize_t>(ret);
#else
    auto ret = ::write(fd_, data, len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    }
    return ret;
#endif
}

auto TcpSocket::native_handle() const -> socket_t { return fd_; }

void TcpSocket::close() {
    close_socket(fd_);
}

TcpSocket::operator bool() const noexcept {
    return fd_ != invalid_socket;
}

}  // namespace ctf::net
