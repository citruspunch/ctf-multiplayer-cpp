#pragma once

#include "framing.hpp"
#include "game.hpp"
#include "messages.hpp"
#include "net/poller.hpp"
#include "net/tcp_socket.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ctf::server {

// Forward declaration (Raylib-heavy class defined in server_view.hpp).
class ServerView;

// Custom deleter so unique_ptr<ServerView> can live in server.cpp without
// the full ServerView definition (pimpl + custom deleter pattern).
struct ServerViewDeleter {
    void operator()(ServerView* p) const noexcept;
};

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
    // `headless` skips the Raylib observer window — used by integration
    // tests so the server can run in a background thread without a GUI.
    explicit Server(int port = constants::default_tcp_port,
                    bool headless = false);
    ~Server();

    Server(const Server&) = delete;
    auto operator=(const Server&) -> Server& = delete;
    Server(Server&&) = delete;
    auto operator=(Server&&) -> Server& = delete;

    // Run the main event loop.  Returns after the user closes the window
    // (non-headless) or stop() is called (headless).
    void run();

    // Signal the event loop to exit (headless mode only).
    void stop();

    // The TCP port the listener is bound to (0 if listen failed).
    auto port() const -> int;

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

    // ── Game domain state ───────────────────────────────────────────

    game::GameState game_state_;
    std::mt19937 rng_{std::random_device{}()};

    // Interact actions received during Playing phase, in TCP arrival order.
    std::queue<std::string> pending_interacts_;

    // Simulation timing.
    std::chrono::steady_clock::time_point last_tick_time_;
    bool tick_initialised_{false};

    // Post-game timer.
    std::chrono::steady_clock::time_point post_game_start_;
    bool game_over_sent_{false};

    // Deferred disconnect queue — collected during event processing,
    // drained in cleanup_disconnected() to avoid iterator invalidation.
    std::vector<net::socket_t> pending_disconnects_;

    // Optional Raylib observer window.
    std::unique_ptr<ServerView, ServerViewDeleter> view_;
    bool headless_{false};
    std::atomic<bool> running_{true};

    // ── Event loop helpers ───────────────────────────────────────────

    void accept_new();
    void read_session(Session& session);
    void dispatch_message(Session& session, const std::string& raw);
    void queue_disconnect(net::socket_t fd);
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
    void broadcast_state();
    void broadcast_game_over(const std::string& winner_id);

    // ── Countdown ────────────────────────────────────────────────────

    void start_countdown();
    void abort_countdown();
    void process_countdown();
    auto session_count() const -> int;

    // ── Game tick ────────────────────────────────────────────────────

    void game_tick();
    void remove_player_from_game(const std::string& player_id);
    void process_post_game();

    // ── Observer view lifecycle ──────────────────────────────────────
    // Defined in server_view.cpp to keep Raylib symbols out of the
    // server library (so tests can link without Raylib).

    // Create the observer window (no-op in headless mode).
    void init_observer();
    // Render one frame (no-op when headless or no view).
    void render_observer();
    // Returns true when the user closed the observer window.
    auto should_close_observer() -> bool;

    // ── Config ───────────────────────────────────────────────────────

    static auto make_config() -> ctf::Config;
};

}  // namespace ctf::server
