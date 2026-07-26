#pragma once

#include "framing.hpp"
#include "messages.hpp"
#include "net/poller.hpp"
#include "net/tcp_socket.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <string>

namespace ctf::server {

enum class Phase { Lobby, Countdown, Playing, PostGame };

// ── Session: per-connection state ────────────────────────────────────────

class Session {
public:
    explicit Session(net::socket_t fd);

    net::socket_t fd;
    framing::LineBuffer recv_buf;
    std::queue<std::string> send_queue;
    std::optional<std::string> coalesced_state;
    bool wants_write = false;

    // Set to true after a successful join.
    bool joined = false;
    std::string player_id;
    std::string player_name;

    // Enqueue a message for sending.  If the message is a 'state' it is
    // coalesced (replaces any pending state).  Non-state messages flush
    // any pending coalesced state first, then are enqueued normally.
    void queue_message(const std::string& msg);

    // Drain the send queue.  Returns false on error (connection broken).
    auto try_send() -> bool;
};

// ── Server: authoritative game server ────────────────────────────────────

class Server {
public:
    explicit Server(int port = constants::default_tcp_port);
    ~Server();

    Server(const Server&) = delete;
    auto operator=(const Server&) -> Server& = delete;
    Server(Server&&) = delete;
    auto operator=(Server&&) -> Server& = delete;

    // Run the main event loop.  Returns after the user closes the window
    // or an unrecoverable error occurs.
    void run();

private:
    net::TcpSocket listener_;
    net::Poller poller_;
    std::map<net::socket_t, std::unique_ptr<Session>> sessions_;
    Phase phase_{Phase::Lobby};

    // Player ID counter.
    int next_player_id_{1};

    // Countdown timer state.
    int countdown_remaining_{constants::countdown_seconds};
    std::chrono::steady_clock::time_point countdown_start_;
    bool countdown_active_{false};

    // ── Event loop helpers ───────────────────────────────────────────

    void accept_new();
    void read_session(Session& session);
    void dispatch_message(Session& session, const std::string& raw);
    void disconnect(net::socket_t fd);
    void cleanup_disconnected();

    // ── Message handlers ─────────────────────────────────────────────

    void handle_join(Session& session, const ctf::Join& msg);
    void handle_input(Session& session, const ctf::Input& msg);
    void handle_interact(Session& session);
    void handle_error_result(Session& session, const ctf::Error& err);
    void handle_unknown_type(Session& session);

    // ── Sending ──────────────────────────────────────────────────────

    void send_to(Session& session, const std::string& msg);
    void send_error(Session& session, const std::string& reason, bool close_conn);
    void broadcast(const std::string& msg);
    void broadcast_lobby();
    void broadcast_countdown(int sec);

    // ── Countdown ────────────────────────────────────────────────────

    void start_countdown();
    void abort_countdown();
    void process_countdown();
    auto session_count() const -> int;

    // ── Config ───────────────────────────────────────────────────────

    static auto make_config() -> ctf::Config;
};

}  // namespace ctf::server
