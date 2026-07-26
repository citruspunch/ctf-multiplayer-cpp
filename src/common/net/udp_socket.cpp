#include "udp_socket.hpp"

#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace ctf::net {

// ── UdpSocket ────────────────────────────────────────────────────────────

UdpSocket::UdpSocket() noexcept {}

UdpSocket::UdpSocket(socket_t native) noexcept
    : fd_(native) {}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : fd_(other.fd_)
{
    other.fd_ = invalid_socket;
}

auto UdpSocket::operator=(UdpSocket&& other) noexcept -> UdpSocket& {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = invalid_socket;
    }
    return *this;
}

UdpSocket::~UdpSocket() { close(); }

auto UdpSocket::bind(int port) -> UdpSocket {
    socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == invalid_socket) return UdpSocket{};

    // SO_REUSEADDR
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

#ifdef SO_REUSEPORT
    // SO_REUSEPORT (macOS/Linux) — allows multiple processes on same port.
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif

    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_socket(fd);
        return UdpSocket{};
    }

    set_non_blocking(fd);
    return UdpSocket{fd};
}

auto UdpSocket::make_broadcast() -> UdpSocket {
    socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == invalid_socket) return UdpSocket{};

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    set_non_blocking(fd);
    return UdpSocket{fd};
}

auto UdpSocket::send_to(const std::string& data, const sockaddr_in& addr) -> bool {
#ifdef _WIN32
    auto ret = ::sendto(fd_, data.data(), static_cast<int>(data.size()), 0,
                        reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return ret != SOCKET_ERROR;
#else
    auto ret = ::sendto(fd_, data.data(), data.size(), 0,
                        reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return ret >= 0;
#endif
}

auto UdpSocket::recv_from(char* buf, std::size_t len, sockaddr_in& sender) -> ssize_t {
#ifdef _WIN32
    int sender_len = sizeof(sender);
    auto ret = ::recvfrom(fd_, buf, static_cast<int>(len), 0,
                          reinterpret_cast<sockaddr*>(&sender), &sender_len);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return -1;
        return -1;
    }
    return static_cast<ssize_t>(ret);
#else
    socklen_t sender_len = sizeof(sender);
    auto ret = ::recvfrom(fd_, buf, len, 0,
                          reinterpret_cast<sockaddr*>(&sender), &sender_len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    }
    return ret;
#endif
}

auto UdpSocket::native_handle() const -> socket_t { return fd_; }

auto UdpSocket::local_port() const -> int {
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

void UdpSocket::close() {
    close_socket(fd_);
}

UdpSocket::operator bool() const noexcept {
    return fd_ != invalid_socket;
}

}  // namespace ctf::net
