#pragma once

#include "socket.hpp"

#include <optional>
#include <string>

struct sockaddr_in;

namespace ctf::net {

// RAII, non-copyable, movable UDP socket.
class UdpSocket {
public:
    // Create a UDP socket bound to the given port on INADDR_ANY.
    // SO_REUSEADDR and SO_REUSEPORT are enabled.
    static auto bind(int port) -> UdpSocket;

    // Create a UDP socket with SO_BROADCAST enabled (for client discovery).
    static auto make_broadcast() -> UdpSocket;

    // Default constructor — yields an invalid (empty) socket.
    UdpSocket() noexcept;

    // Wrap an existing native socket.
    explicit UdpSocket(socket_t native) noexcept;

    // Non-copyable.
    UdpSocket(const UdpSocket&) = delete;
    auto operator=(const UdpSocket&) -> UdpSocket& = delete;

    // Movable.
    UdpSocket(UdpSocket&& other) noexcept;
    auto operator=(UdpSocket&& other) noexcept -> UdpSocket&;

    ~UdpSocket();

    // Send data to a specific address.
    auto send_to(const std::string& data, const sockaddr_in& addr) -> bool;

    // Receive data and record the sender address.
    // Returns -1 on EAGAIN/EWOULDBLOCK.
    auto recv_from(char* buf, std::size_t len, sockaddr_in& sender) -> ssize_t;

    // Access the native handle.
    auto native_handle() const -> socket_t;

    // Close the socket explicitly.
    void close();

    // Whether the socket is valid (open).
    explicit operator bool() const noexcept;

private:
    socket_t fd_{invalid_socket};
};

}  // namespace ctf::net
