#include "poller.hpp"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <poll.h>
    #include <sys/select.h>
    #include <unistd.h>
#endif

namespace ctf::net {

Poller::Poller() = default;
Poller::~Poller() = default;
Poller::Poller(Poller&&) noexcept = default;
auto Poller::operator=(Poller&&) noexcept -> Poller& = default;

void Poller::add_fd(socket_t fd, bool want_read, bool want_write) {
    // Don't add duplicates.
    auto it = std::find_if(fds_.begin(), fds_.end(),
                           [fd](const FdEntry& e) { return e.fd == fd; });
    if (it != fds_.end()) {
        it->want_read = want_read;
        it->want_write = want_write;
        return;
    }
    fds_.push_back({fd, want_read, want_write});
}

void Poller::remove_fd(socket_t fd) {
    auto it = std::find_if(fds_.begin(), fds_.end(),
                           [fd](const FdEntry& e) { return e.fd == fd; });
    if (it != fds_.end()) {
        fds_.erase(it);
    }
}

auto Poller::poll(int timeout_ms) -> int {
    readable_.assign(fds_.size(), false);
    writable_.assign(fds_.size(), false);

    if (fds_.empty()) return 0;

#ifdef _WIN32
    // Windows: use select() — WSAPoll is buggy on some versions.
    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    for (const auto& e : fds_) {
        if (e.want_read)  FD_SET(e.fd, &read_fds);
        if (e.want_write) FD_SET(e.fd, &write_fds);
    }

    TIMEVAL tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = ::select(0, &read_fds, &write_fds, nullptr,
                       timeout_ms >= 0 ? &tv : nullptr);
    if (ret <= 0) return ret;

    int count = 0;
    for (std::size_t i = 0; i < fds_.size(); ++i) {
        bool r = FD_ISSET(fds_[i].fd, &read_fds);
        bool w = FD_ISSET(fds_[i].fd, &write_fds);
        if (r || w) ++count;
        readable_[i] = r;
        writable_[i] = w;
    }
    return count;

#else
    // POSIX: use poll().
    std::vector<pollfd> pfds(fds_.size());
    for (std::size_t i = 0; i < fds_.size(); ++i) {
        pfds[i].fd = fds_[i].fd;
        pfds[i].events  = (fds_[i].want_read  ? POLLIN  : 0)
                        | (fds_[i].want_write ? POLLOUT : 0);
        pfds[i].revents = 0;
    }

    int ret = ::poll(pfds.data(), pfds.size(), timeout_ms);
    if (ret <= 0) return ret;

    for (std::size_t i = 0; i < pfds.size(); ++i) {
        readable_[i] = (pfds[i].revents & POLLIN) != 0;
        writable_[i] = (pfds[i].revents & POLLOUT) != 0;
    }
    return ret;
#endif
}

auto Poller::is_readable(socket_t fd) const -> bool {
    for (std::size_t i = 0; i < fds_.size(); ++i) {
        if (fds_[i].fd == fd) return readable_[i];
    }
    return false;
}

auto Poller::is_writable(socket_t fd) const -> bool {
    for (std::size_t i = 0; i < fds_.size(); ++i) {
        if (fds_[i].fd == fd) return writable_[i];
    }
    return false;
}

}  // namespace ctf::net
