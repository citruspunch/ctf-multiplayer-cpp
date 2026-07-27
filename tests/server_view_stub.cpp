// No-op stubs for the ServerView lifecycle methods, used by the test
// target so it can link libserver.a without Raylib.  In headless mode
// (the only mode used by tests) these methods are never called with
// a real view, so no-op implementations are safe.

#include "server.hpp"

namespace ctf::server {

Server::~Server() {
    sessions_.clear();
    if (listener_) {
        poller_.remove_fd(listener_.native_handle());
    }
}

void ServerViewDeleter::operator()(ServerView*) const noexcept {
    // No-op: in headless mode the view is never created.
}

void Server::init_observer() {}
void Server::render_observer() {}
auto Server::should_close_observer() -> bool { return false; }

}  // namespace ctf::server