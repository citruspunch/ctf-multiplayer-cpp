#pragma once

#include "socket.hpp"

#include <optional>
#include <string>

namespace ctf::net {

// RAII, non-copyable, movable TCP socket.
class TcpSocket {
public:
    // Create a listening TCP socket bound to the given port.
    // SO_REUSEADDR is enabled.  The socket is set to non-blocking.
    static auto listen(int port) -> TcpSocket;

    // Connect to a remote host (IP or hostname) and TCP port.
    // Waits up to `timeout_ms` for the connection to be established.
    // The returned socket is non-blocking.  Returns an invalid socket
    // on failure.
    static auto connect(const std::string& host, int port,
                        int timeout_ms = 3000) -> TcpSocket;

    // Default constructor — yields an invalid (empty) socket.
    TcpSocket() noexcept;

    // Wrap an existing native socket.
    explicit TcpSocket(socket_t native) noexcept;

    // Non-copyable.
    TcpSocket(const TcpSocket&) = delete;
    auto operator=(const TcpSocket&) -> TcpSocket& = delete;

    // Movable.
    TcpSocket(TcpSocket&& other) noexcept;
    auto operator=(TcpSocket&& other) noexcept -> TcpSocket&;

    ~TcpSocket();

    // Accept a new incoming connection (non-blocking).
    // Returns nullopt if no connection is pending (EAGAIN / EWOULDBLOCK).
    auto accept() -> std::optional<TcpSocket>;

    // Receive up to `len` bytes into `buf`.
    // Returns -1 on EAGAIN/EWOULDBLOCK, 0 on closed.
    auto recv(char* buf, std::size_t len) -> ssize_t;

    // Send `len` bytes from `data`.
    // Returns the number of bytes sent (>= 0), -1 on EAGAIN/EWOULDBLOCK
    // (retry later), or -2 on a real error (connection broken).
    auto send(const char* data, std::size_t len) -> ssize_t;

    // Access the native handle.
    auto native_handle() const -> socket_t;

    // Return the locally-bound port (useful after listen(0)).
    // Returns 0 on failure.
    auto local_port() const -> int;

    // Close the socket explicitly.
    void close();

    // Release ownership of the native handle (prevents close on destruct).
    auto release() -> socket_t;

    // Whether the socket is valid (open).
    explicit operator bool() const noexcept;

private:
    socket_t fd_{invalid_socket};
};

}  // namespace ctf::net
