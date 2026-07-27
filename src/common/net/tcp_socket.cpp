#include "tcp_socket.hpp"

#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <cerrno>
    #include <netdb.h>
    #include <sys/select.h>
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

auto TcpSocket::connect(const std::string& host, int port,
                        int timeout_ms) -> TcpSocket {
    // Resolve host (numeric IP or hostname) to an IPv4 address.
    addrinfo hints{};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        return TcpSocket{};
    }

    socket_t fd = invalid_socket;
    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == invalid_socket) continue;

        if (!set_non_blocking(fd)) {
            close_socket(fd);
            fd = invalid_socket;
            continue;
        }

        int rc = ::connect(fd, rp->ai_addr, static_cast<int>(rp->ai_addrlen));
        if (rc == 0) {
            break;  // Connected immediately.
        }

#ifdef _WIN32
        bool in_progress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
        bool in_progress = (errno == EINPROGRESS);
#endif
        if (!in_progress) {
            close_socket(fd);
            fd = invalid_socket;
            continue;
        }

        // Wait for the connection with select() (avoids WSAPoll issues).
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(fd, &write_fds);
        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ready = ::select(static_cast<int>(fd) + 1, nullptr, &write_fds,
                             nullptr, &tv);
        if (ready <= 0) {
            close_socket(fd);
            fd = invalid_socket;
            continue;
        }

        int so_error = 0;
#ifdef _WIN32
        int opt_len = sizeof(so_error);
#else
        socklen_t opt_len = sizeof(so_error);
#endif
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&so_error), &opt_len) < 0 ||
            so_error != 0) {
            close_socket(fd);
            fd = invalid_socket;
            continue;
        }
        break;  // Connected.
    }

    ::freeaddrinfo(result);
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
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return -1;  // retry later
        return -2;                              // real error
    }
    return static_cast<ssize_t>(ret);
#else
    auto ret = ::write(fd_, data, len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;  // retry later
        return -2;                                               // real error
    }
    return ret;
#endif
}

auto TcpSocket::native_handle() const -> socket_t { return fd_; }

auto TcpSocket::local_port() const -> int {
    if (fd_ == invalid_socket) return 0;
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        return ntohs(addr.sin_port);
    }
    return 0;
}

void TcpSocket::close() {
    close_socket(fd_);
}

auto TcpSocket::release() -> socket_t {
    socket_t fd = fd_;
    fd_ = invalid_socket;
    return fd;
}

TcpSocket::operator bool() const noexcept {
    return fd_ != invalid_socket;
}

}  // namespace ctf::net
