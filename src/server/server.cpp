#include "server.hpp"

#include "json.hpp"
#include "net/platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace ctf::server {

// ── Session ──────────────────────────────────────────────────────────────

Session::Session(net::socket_t fd)
    : fd(fd) {}

void Session::queue_message(const std::string& msg) {
    // Detect whether this is a state message (for coalescing).
    // We check for "\"type\":\"state\"" as a lightweight heuristic.
    bool is_state = msg.find("\"type\":\"state\"") != std::string::npos;

    if (is_state) {
        // Replace any pending coalesced state.
        coalesced_state = msg;
    } else {
        // Non-state message: flush any pending coalesced state first.
        if (coalesced_state.has_value()) {
            send_queue.push(std::move(*coalesced_state));
            coalesced_state.reset();
        }
        send_queue.push(msg);
    }
    wants_write = true;
}

auto Session::try_send() -> bool {
    while (!send_queue.empty()) {
        auto& msg = send_queue.front();
        auto ret = ::send(fd, msg.data(), msg.size(), 0);
        if (ret < 0) {
            // EAGAIN / EWOULDBLOCK — try again later.
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) break;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
            return false;  // connection broken
        }
        send_queue.pop();
    }

    // If the queue is empty and there's a coalesced state, promote it.
    if (send_queue.empty() && coalesced_state.has_value()) {
        send_queue.push(std::move(*coalesced_state));
        coalesced_state.reset();
        // Try to send immediately.
        return try_send();
    }

    if (send_queue.empty()) {
        wants_write = false;
    }
    return true;
}

// ── Server ───────────────────────────────────────────────────────────────

Server::Server(int port, bool headless)
    : headless_(headless),
      server_name_("CTF Server") {
    listener_ = net::TcpSocket::listen(port);
    if (listener_) {
        poller_.add_fd(listener_.native_handle(), true, false);
    }

    // ── UDP discovery on port 8888 ───────────────────────────────────
    // Bind a UDP socket with SO_REUSEADDR + SO_REUSEPORT for local
    // multi-process testing, then create the DiscoveryServer that
    // answers `discover` queries with `server_info`.
    auto udp_sock = net::UdpSocket::bind(constants::discovery_port);
    if (udp_sock.native_handle() != net::invalid_socket) {
        discovery_ = std::make_unique<discovery::DiscoveryServer>(
            std::move(udp_sock), *this);
        std::cout << "[server] UDP discovery listening on port "
                  << constants::discovery_port << "\n";
    } else {
        std::cerr << "[server] WARNING: failed to bind UDP discovery port "
                  << constants::discovery_port << " (port in use?)\n";
    }
    std::cout << "[server] TCP listening on port " << port << "\n";
}

// Destructor is defined in server_view.cpp where ServerView is complete
// (unique_ptr<ServerView> needs the full type for its destructor).

void Server::stop() {
    running_.store(false);
}

auto Server::port() const -> int {
    return listener_ ? listener_.local_port() : 0;
}

void Server::run() {
    if (!listener_) return;

    // Create the observer view (skipped in headless mode).
    init_observer();

    while (true) {
        // ── UDP discovery tick ──────────────────────────────────────
        if (discovery_) discovery_->tick();

        int ret = poller_.poll(50);  // ~20 Hz

        if (ret < 0) continue;  // poll error, retry

        // 0. Check exit conditions.
        if (headless_) {
            if (!running_.load()) break;
        } else if (should_close_observer()) {
            break;
        }

        // 1. Accept new connections.
        accept_new();

        // 2. Process readable sessions.
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            auto& [fd, session] = *it;
            if (poller_.is_readable(fd)) {
                read_session(*session);
            }
            if (poller_.is_writable(fd)) {
                if (!session->try_send()) {
                    queue_disconnect(fd);
                }
            }
            ++it;
        }

        // 3. Phase-specific logic.
        if (phase_ == Phase::Lobby) {
            // Start condition:
            //   headless mode -> auto-start when min_players reached (tests)
            //   observer mode -> manual start via SPACE key in the view
            bool should_start = false;
            if (headless_) {
                should_start = (session_count() >= constants::min_players);
            } else {
                should_start = observer_start_requested()
                               && (session_count() >= constants::min_players);
            }
            if (should_start) {
                std::cout << "[server] Starting game with "
                          << session_count() << " players\n";
                start_countdown();
            }
        } else if (phase_ == Phase::Countdown) {
            process_countdown();
        } else if (phase_ == Phase::Playing) {
            game_tick();
        } else if (phase_ == Phase::PostGame) {
            process_post_game();
        }

        // 4. Clean up disconnected sessions (drains pending_disconnects_).
        cleanup_disconnected();

        // 5. Render the observer window.
        render_observer();
    }
}

// ── Accept new connections ───────────────────────────────────────────────

void Server::accept_new() {
    if (!poller_.is_readable(listener_.native_handle())) return;

    while (auto client = listener_.accept()) {
        // Release the fd from the TcpSocket so its destructor doesn't
        // close it — the Session now owns the fd.
        auto fd = client->release();
        auto session = std::make_unique<Session>(fd);

        poller_.add_fd(fd, true, true);  // want read and write
        sessions_[fd] = std::move(session);
    }
}

// ── Read and dispatch ────────────────────────────────────────────────────

void Server::read_session(Session& session) {
    char buf[4096];
#ifdef _WIN32
    auto n = ::recv(session.fd, buf, sizeof(buf), 0);
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return;
        queue_disconnect(session.fd);
        return;
    }
#else
    auto n = ::read(session.fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        queue_disconnect(session.fd);
        return;
    }
#endif

    if (n == 0) {
        // Client closed the connection.
        queue_disconnect(session.fd);
        return;
    }

    try {
        session.recv_buf.append(buf, static_cast<std::size_t>(n));
    } catch (const framing::message_too_large_error&) {
        send_error(session, MESSAGE_TOO_LARGE, false);
        queue_disconnect(session.fd);
        return;
    }

    // Extract all complete messages from the buffer.
    while (auto msg = session.recv_buf.extract()) {
        dispatch_message(session, *msg);
    }
}

void Server::dispatch_message(Session& session, const std::string& raw) {
    // Parse the raw line as JSON.
    auto json = json::parse_line(raw);
    if (!json) {
        send_error(session, INVALID_JSON, false);
        return;
    }

    // Convert to a typed message variant.
    auto msg = msg::from_json(*json);

    // Dispatch based on variant type.
    if (auto* err = std::get_if<ctf::Error>(&msg)) {
        handle_error_result(session, *err);
        return;
    }

    // -- Phase-dependent dispatch --

    if (phase_ == Phase::Lobby) {
        // Only 'join' is allowed in lobby.
        if (auto* join = std::get_if<ctf::Join>(&msg)) {
            handle_join(session, *join);
        } else {
            handle_unknown_type(session);
        }
        return;
    }

    if (phase_ == Phase::Countdown || phase_ == Phase::Playing) {
        // 'join' → GAME_STARTED + close.
        if (std::get_if<ctf::Join>(&msg)) {
            send_error(session, GAME_STARTED, true);
            return;
        }

        // Not joined yet → NOT_JOINED.
        if (!session.joined) {
            send_error(session, NOT_JOINED, false);
            return;
        }
    }

    if (phase_ == Phase::Countdown) {
        // In countdown, only input/interact after start are invalid.
        if (std::get_if<ctf::Input>(&msg) || std::get_if<ctf::Interact>(&msg)) {
            send_error(session, INVALID_PHASE, false);
        } else {
            handle_unknown_type(session);
        }
        return;
    }

    if (phase_ == Phase::Playing) {
        if (auto* input = std::get_if<ctf::Input>(&msg)) {
            handle_input(session, *input);
        } else if (std::get_if<ctf::Interact>(&msg)) {
            handle_interact(session);
        } else {
            handle_unknown_type(session);
        }
        return;
    }

    // PostGame: ignore all client messages.
    if (phase_ == Phase::PostGame) {
        return;
    }
}

// ── Message handlers ─────────────────────────────────────────────────────

void Server::handle_join(Session& session, const ctf::Join& msg) {
    // Already joined → INVALID_PHASE (no close).
    if (session.joined) {
        send_error(session, INVALID_PHASE, false);
        return;
    }

    // Name already validated by msg::from_json (NAME_INVALID).
    // Version already validated (VERSION_MISMATCH).

    // Lobby full → LOBBY_FULL + close.
    if (sessions_.size() >= static_cast<std::size_t>(constants::max_players)) {
        send_error(session, LOBBY_FULL, true);
        return;
    }

    // Assign player ID.
    session.player_id = "p" + std::to_string(next_player_id_++);
    session.player_name = msg.name;
    session.joined = true;

    std::cout << "[server] " << session.player_name << " (" << session.player_id
              << ") joined. Total players: " << session_count() << "\n";

    // Send welcome with the fixed config.
    ctf::Welcome welcome{session.player_id, make_config()};
    auto welcome_json = msg::to_json(welcome);
    send_to(session, framing::encode(welcome_json));

    // Broadcast updated lobby to all.
    broadcast_lobby();

    // Countdown trigger is deferred to the main loop (after all messages
    // in this iteration are processed) so that simultaneous joins from
    // multiple bots all land in the Lobby phase.
}

void Server::handle_input(Session& session, const ctf::Input& msg) {
    // Update the player's direction in the game state.
    auto* ps = game::find_player(game_state_, session.player_id);
    if (ps) {
        ps->dir_x = msg.dir_x;
        ps->dir_y = msg.dir_y;
    }
}

void Server::handle_interact(Session& session) {
    if (!session.joined) return;
    pending_interacts_.push(session.player_id);
}

void Server::handle_error_result(Session& session, const ctf::Error& err) {
    // Forward validation errors to the client.
    send_to(session, framing::encode(msg::to_json(err)));
}

void Server::handle_unknown_type(Session& session) {
    send_error(session, UNKNOWN_TYPE, false);
}

// ── Sending ──────────────────────────────────────────────────────────────

void Server::send_to(Session& session, const std::string& msg) {
    session.queue_message(msg);
    if (session.wants_write) {
        poller_.add_fd(session.fd, true, true);
    }
}

void Server::send_error(Session& session, const std::string& reason, bool close_conn) {
    std::cerr << "[server] ERROR → " << (session.joined ? session.player_id : "?")
              << " reason=" << reason << " phase=";
    switch (phase_) {
        case Phase::Lobby:      std::cerr << "LOBBY"; break;
        case Phase::Countdown:  std::cerr << "COUNTDOWN"; break;
        case Phase::Playing:    std::cerr << "PLAYING"; break;
        case Phase::PostGame:   std::cerr << "POST_GAME"; break;
    }
    std::cerr << " close=" << close_conn << "\n";

    ctf::Error err{reason};
    auto json = msg::to_json(err);
    send_to(session, framing::encode(json));

    if (close_conn) {
        queue_disconnect(session.fd);
    }
}

void Server::broadcast(const std::string& msg) {
    for (auto& [fd, session] : sessions_) {
        send_to(*session, msg);
    }
}

void Server::broadcast_lobby() {
    ctf::Lobby lobby_msg;
    for (auto& [fd, session] : sessions_) {
        if (!session->joined) continue;
        lobby_msg.players.push_back(
            PlayerInfo{session->player_id, session->player_name});
    }

    auto json = msg::to_json(lobby_msg);
    broadcast(framing::encode(json));
}

void Server::broadcast_countdown(int sec) {
    ctf::Countdown cd{sec};
    auto json = msg::to_json(cd);
    broadcast(framing::encode(json));
}

// ── Countdown ────────────────────────────────────────────────────────────

auto Server::session_count() const -> int {
    int count = 0;
    for (const auto& [fd, session] : sessions_) {
        if (session->joined) ++count;
    }
    return count;
}

void Server::start_countdown() {
    if (phase_ != Phase::Lobby) return;

    phase_ = Phase::Countdown;
    countdown_remaining_ = constants::countdown_seconds;
    countdown_start_ = std::chrono::steady_clock::now();
    countdown_active_ = true;

    std::cout << "[server] Countdown started: " << countdown_remaining_
              << " seconds, " << session_count() << " players\n";

    broadcast_countdown(countdown_remaining_);
}

void Server::abort_countdown() {
    countdown_active_ = false;
    phase_ = Phase::Lobby;
    broadcast_lobby();
}

void Server::process_countdown() {
    if (!countdown_active_) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - countdown_start_).count();
    int elapsed_sec = static_cast<int>(elapsed_ms / 1000);

    // Check for player drop below minimum.
    if (session_count() < constants::min_players) {
        abort_countdown();
        return;
    }

    int remaining = constants::countdown_seconds - elapsed_sec;
    if (remaining <= 0) {
        // Countdown finished — broadcast start and transition to playing.
        countdown_active_ = false;

        std::cout << "[server] Countdown finished — starting game with "
                  << session_count() << " players\n";

        ctf::Start start_msg;
        auto json = msg::to_json(start_msg);
        broadcast(framing::encode(json));

        // Spawn all joined players.
        game_state_ = game::GameState{};
        for (auto& [fd, session] : sessions_) {
            if (!session->joined) continue;
            auto ps = game::spawn_player(session->player_id,
                                         session->player_name, rng_);
            game_state_.players.push_back(std::move(ps));
        }

        last_tick_time_ = std::chrono::steady_clock::now();
        tick_initialised_ = true;
        phase_ = Phase::Playing;
    } else if (remaining < countdown_remaining_) {
        // Second ticked — broadcast new countdown value.
        countdown_remaining_ = remaining;
        std::cout << "[server] Countdown: " << countdown_remaining_
                  << " seconds remaining\n";
        broadcast_countdown(countdown_remaining_);
    }
}

// ── Disconnect ───────────────────────────────────────────────────────────

void Server::disconnect(net::socket_t fd) {
    auto it = sessions_.find(fd);
    if (it == sessions_.end()) return;

    std::string player_id = it->second->player_id;
    bool was_joined = it->second->joined;
    bool was_in_countdown = (phase_ == Phase::Countdown);
    bool was_in_game = was_joined && (phase_ == Phase::Playing || phase_ == Phase::PostGame);

    // Close and remove.
    poller_.remove_fd(fd);
    it->second->fd = net::invalid_socket;
    sessions_.erase(it);

    // Remove from game state if needed (handles carrier reset).
    if (was_in_game) {
        remove_player_from_game(player_id);
    }

    // If no sessions left, reset to lobby.
    if (sessions_.empty()) {
        phase_ = Phase::Lobby;
        game_state_ = game::GameState{};
        countdown_active_ = false;
        game_over_sent_ = false;
        return;
    }

    // Broadcast updated lobby if needed.
    if (was_joined) {
        if (phase_ == Phase::Lobby || was_in_countdown) {
            broadcast_lobby();
        }

        // Abort countdown if below minimum.
        if (was_in_countdown && session_count() < constants::min_players) {
            abort_countdown();
        }
    }
}

void Server::queue_disconnect(net::socket_t fd) {
    // Avoid duplicates.
    if (std::find(pending_disconnects_.begin(), pending_disconnects_.end(), fd)
        == pending_disconnects_.end()) {
        pending_disconnects_.push_back(fd);
    }
}

void Server::cleanup_disconnected() {
    for (auto fd : pending_disconnects_) {
        disconnect(fd);
    }
    pending_disconnects_.clear();
}

// ── Config ───────────────────────────────────────────────────────────────

auto Server::make_config() -> ctf::Config {
    return ctf::Config{
        constants::map_size,
        constants::circle_radius,
        constants::player_radius,
        constants::interact_radius,
        constants::speed,
        constants::tick_rate
    };
}

// ── Game tick (called during Playing phase) ──────────────────────────────

void Server::game_tick() {
    auto now = std::chrono::steady_clock::now();
    if (!tick_initialised_) {
        last_tick_time_ = now;
        tick_initialised_ = true;
        return;
    }

    double dt = std::chrono::duration<double>(now - last_tick_time_).count();

    // Limit to 20 Hz (50 ms per tick) per SPEC.  The poll loop may return
    // early when there is I/O activity, so we skip ticks that are too soon.
    if (dt < 0.05) return;

    last_tick_time_ = now;

    // Clamp dt to prevent large jumps.
    if (dt > 1.0) dt = 1.0;

    // 1. Move all players.
    for (auto& ps : game_state_.players) {
        game::move_player(ps, dt);
    }

    // 2. Check victory for the carrier BEFORE interactions.
    std::string winner_id;
    if (game_state_.flag.owner.has_value()) {
        if (game::check_victory(game_state_, game_state_.flag.owner.value())) {
            winner_id = game_state_.flag.owner.value();
        }
    }

    if (!winner_id.empty()) {
        game_over_sent_ = true;
        broadcast_game_over(winner_id);
        phase_ = Phase::PostGame;
        post_game_start_ = std::chrono::steady_clock::now();
        return;
    }

    // 3. Process pending interacts in arrival order.
    while (!pending_interacts_.empty()) {
        auto actor_id = pending_interacts_.front();
        pending_interacts_.pop();
        game::process_interact(game_state_, actor_id);
    }

    // 4. Update flag position to carrier position.
    if (game_state_.flag.owner.has_value()) {
        auto* carrier = game::find_player(game_state_, game_state_.flag.owner.value());
        if (carrier) {
            game_state_.flag.x = carrier->x;
            game_state_.flag.y = carrier->y;
        } else {
            // Carrier disconnected — reset flag (handled in remove_player).
            game::reset_flag(game_state_);
        }
    }

    // 5. Broadcast state.
    broadcast_state();
}

// ── State broadcast ──────────────────────────────────────────────────────

void Server::broadcast_state() {
    ctf::State state_msg;

    // Flag.
    if (game_state_.flag.owner.has_value()) {
        state_msg.flag.owner = game_state_.flag.owner;
    }
    state_msg.flag.x = game::round_1dp(game_state_.flag.x);
    state_msg.flag.y = game::round_1dp(game_state_.flag.y);

    // Players (only connected ones).
    for (const auto& ps : game_state_.players) {
        // Skip players whose session has disconnected.
        bool connected = false;
        for (const auto& [fd, s] : sessions_) {
            if (s->player_id == ps.id) { connected = true; break; }
        }
        if (!connected) continue;

        state_msg.players.push_back(ctf::Player{
            ps.id,
            game::round_1dp(ps.x),
            game::round_1dp(ps.y)
        });
    }

    auto json = msg::to_json(state_msg);
    broadcast(framing::encode(json));
}

// ── Game over broadcast ──────────────────────────────────────────────────

void Server::broadcast_game_over(const std::string& winner_id) {
    ctf::GameOver go;
    go.winner = winner_id;
    auto json = msg::to_json(go);
    broadcast(framing::encode(json));
}

// ── Remove player from game state ────────────────────────────────────────

void Server::remove_player_from_game(const std::string& player_id) {
    // If carrier, reset flag to centre.
    if (game_state_.flag.owner.has_value() &&
        game_state_.flag.owner.value() == player_id) {
        game::reset_flag(game_state_);
    }

    // Remove from players vector.
    auto it = std::remove_if(game_state_.players.begin(),
                             game_state_.players.end(),
                             [&](const game::PlayerState& ps) {
                                 return ps.id == player_id;
                             });
    game_state_.players.erase(it, game_state_.players.end());
}

// ── Post-game processing ─────────────────────────────────────────────────

void Server::process_post_game() {
    if (!game_over_sent_) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - post_game_start_).count();

    if (elapsed >= constants::post_game_seconds) {
        // Clear game state, return to lobby.
        game_state_ = game::GameState{};
        game_over_sent_ = false;
        phase_ = Phase::Lobby;
        broadcast_lobby();
    }
}

}  // namespace ctf::server
