#pragma once

#include "socket.hpp"

#include <vector>

namespace ctf::net {

// Wraps poll() (POSIX) / select() (Windows, since WSAPoll has known bugs).
// Tracks a set of sockets for readability and writability.
class Poller {
public:
    Poller();
    ~Poller();

    // Non-copyable, movable.
    Poller(const Poller&) = delete;
    auto operator=(const Poller&) -> Poller& = delete;
    Poller(Poller&&) noexcept;
    auto operator=(Poller&&) noexcept -> Poller&;

    // Add a socket to the poll set.
    void add_fd(socket_t fd, bool want_read, bool want_write);

    // Remove a socket from the poll set.
    void remove_fd(socket_t fd);

    // Poll for events.  Returns the number of ready sockets, or -1 on error.
    auto poll(int timeout_ms) -> int;

    // Check if a socket has data to read after poll().
    auto is_readable(socket_t fd) const -> bool;

    // Check if a socket can be written to after poll().
    auto is_writable(socket_t fd) const -> bool;

private:
    struct FdEntry {
        socket_t fd;
        bool want_read;
        bool want_write;
    };

    std::vector<FdEntry> fds_;

    // Per-fd results after poll().
    std::vector<bool> readable_;
    std::vector<bool> writable_;
};

}  // namespace ctf::net
