#include "client.hpp"

#include "constants.hpp"
#include "game_view.hpp"
#include "json.hpp"

#ifdef __APPLE__
#include "app_activation.hpp"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif

#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

namespace ctf::client {

namespace {

constexpr int WINDOW_W = 800;
constexpr int WINDOW_H = 800;

const Color BG_COLOR{20, 22, 28, 255};
const Color PANEL_COLOR{35, 38, 47, 255};
const Color ROW_ALT_COLOR{30, 33, 41, 255};
const Color SELECT_COLOR{52, 94, 168, 255};
const Color BTN_COLOR{62, 68, 84, 255};
const Color BTN_HOVER_COLOR{82, 90, 112, 255};

// Left/right whitespace trim (spec names are validated post-trim).
auto trimmed(const std::string& s) -> std::string {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

auto is_clicked(float x, float y, float w, float h) -> bool {
    Rectangle r{x, y, w, h};
    return CheckCollisionPointRec(GetMousePosition(), r) &&
           IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

}  // namespace

// ── Construction / destruction ───────────────────────────────────────────

Client::Client() {
    manual_ip_.max_len = 64;
    direct_addr_.max_len = 64;
    name_field_.max_len = constants::name_max_length;
}

Client::~Client() {
    // Wait for any in-flight discovery work before tearing down members.
    if (broadcast_future_.valid()) broadcast_future_.wait();
    if (unicast_future_.valid()) unicast_future_.wait();
    if (window_open_) CloseWindow();
}

// ── Main loop ────────────────────────────────────────────────────────────

void Client::run() {
#ifdef __APPLE__
    // Promote the process from a background terminal tool to a
    // foreground GUI app so it can connect to the WindowServer.
    // This is a no-op when launched via `open ctf.app`.
    activate_macos_app();
#endif

    // ── Window creation ─────────────────────────────────────────────
    // On macOS Apple Silicon, VSYNC_HINT + TOPMOST are broken (black
    // window).  Use ONLY FLAG_WINDOW_RESIZABLE on macOS.
#if defined(__APPLE__)
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#endif
    InitWindow(WINDOW_W, WINDOW_H, "CTF — Client");
    window_open_ = true;

    // ── macOS: bring window to front, install menu ──────────────────
#ifdef __APPLE__
    activate_macos_app_after_init();
    install_macos_menu();
    clear_macos_quit_request();
#endif

    // ── Framerate cap ───────────────────────────────────────────────
    SetTargetFPS(60);
    SetExitKey(0);  // ESC handled manually in update()

    start_broadcast_discovery();

    while (!WindowShouldClose()) {
#ifdef __APPLE__
        if (macos_quit_requested()) { window_open_ = false; break; }
#endif

        // Manual frame limiter (60 FPS ≈ 16.67 ms per frame).
        // SetTargetFPS doesn't work reliably on macOS GL→Metal bridge
        // without VSYNC_HINT (which caused black windows).
        double frame_start = GetTime();

        update();

        // ESC handler inside update() sets window_open_ = false.
        if (!window_open_) break;

        BeginDrawing();
        ClearBackground(BG_COLOR);
        draw();

        // ── Apple Silicon: GL → Metal bridge pipeline ────────────
        // OpenGL on Apple Silicon translates to Metal.  The
        // pipeline operates in two distinct phases:
        //
        //   PHASE A — Immediate GL commands:
        //     ClearBackground() → glClear() — goes straight to
        //     OpenGL, buffered in a Metal command buffer.
        //
        //   PHASE B — Batched draw calls:
        //     DrawText(), DrawRectangle(), etc. are queued in
        //     Raylib's vertex batch (RAM), NOT sent to GL yet.
        //     They are flushed to GL inside EndDrawing() via
        //     rlDrawRenderBatchActive().
        //
        // Strategy:
        //   1. glFlush() — commits PHASE-A's Metal buffer so
        //      the clear is ready, but does NOT present.
        //   2. EndDrawing() — sends PHASE-B vertices to GL
        //      (new Metal buffer), then SwapBuffers presents.
        //   3. CGLFlushDrawable AFTER EndDrawing — belt-and-
        //      suspenders: if the swap inside EndDrawing
        //      silently no-ops, this guarantees presentation
        //      of the fully-rendered frame.
        //
        // ⚠ Do NOT call CGLFlushDrawable BEFORE EndDrawing —
        //    it PRESENTS the drawable, so EndDrawing's swap
        //    becomes a no-op and PHASE-B draws are lost.
#ifdef __APPLE__
        glFlush();  // commit Metal buffer, DON'T present
#endif
        EndDrawing();

#ifdef __APPLE__
        // Belt-and-suspenders: guarantee the frame is presented.
        {
            CGLContextObj ctx = CGLGetCurrentContext();
            if (ctx) CGLFlushDrawable(ctx);
        }
        // Drain Cocoa's main queue so Cmd+Q dispatches and the
        // Dock doesn't show "Not Responding".
        pump_cocoa_main_queue();
#endif

        // Manual frame cap: sleep for the remainder of 16.67 ms.
        constexpr double TARGET_FRAME = 1.0 / 60.0;
        double elapsed = GetTime() - frame_start;
        double remaining = TARGET_FRAME - elapsed;
        if (remaining > 0.0) {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(remaining));
        }
    }
}

void Client::update() {
    if (socket_) {
        poll_network();
        flush_send();
    }

    // Global ESC-to-quit: only when no text field is focused, so it
    // doesn't fire while the user is typing a server IP, a name, etc.
    // Cmd+Q is the primary macOS quit path (handled in run()); this is
    // a cross-platform fallback that also works on Linux/Windows.
    const bool any_text_focused = manual_ip_.focused ||
                                  direct_addr_.focused ||
                                  name_field_.focused;
    if (!any_text_focused && IsKeyPressed(KEY_ESCAPE)) {
        // If help overlay is open, close it first.
        if (show_help_) {
            show_help_ = false;
        } else {
            window_open_ = false;
            return;
        }
    }

    // Detect state transition for fade effect.
    // We snapshot the current state before the per-state update,
    // then compare after to see if handle_message changed it.
    ClientState state_before = state_;

    switch (state_) {
        case ClientState::Discovery:    update_discovery();    break;
        case ClientState::JoinName:     update_join_name();    break;
        case ClientState::Lobby:        update_lobby();        break;
        case ClientState::Countdown:    update_countdown();    break;
        case ClientState::Playing:      update_playing();      break;
        case ClientState::GameOver:     update_game_over();    break;
        case ClientState::Disconnected: update_disconnected(); break;
    }

    if (state_ != state_before) {
        state_transition_time_ = GetTime();
        // Close the help overlay on state change.
        show_help_ = false;
    }
}

void Client::draw() {
    switch (state_) {
        case ClientState::Discovery:    draw_discovery();    break;
        case ClientState::JoinName:     draw_join_name();    break;
        case ClientState::Lobby:        draw_lobby();        break;
        case ClientState::Countdown:    draw_countdown();    break;
        case ClientState::Playing:      draw_playing();      break;
        case ClientState::GameOver:     draw_game_over();    break;
        case ClientState::Disconnected: draw_disconnected(); break;
    }

    // ── Fade transition overlay ────────────────────────────────────
    constexpr double FADE_DURATION = 0.4;
    double elapsed = GetTime() - state_transition_time_;
    if (elapsed < FADE_DURATION) {
        unsigned char alpha = static_cast<unsigned char>(
            255.0 * (1.0 - elapsed / FADE_DURATION));
        DrawRectangle(0, 0, WINDOW_W, WINDOW_H,
                      Color{0, 0, 0, alpha});
    }

    // ── How-to-Play overlay (only in Lobby, on top of everything) ──
    if (show_help_ && state_ == ClientState::Lobby) {
        draw_help_overlay();
    }
}

// ── Networking ───────────────────────────────────────────────────────────

void Client::connect_to(const std::string& ip, int port) {
    socket_ = net::TcpSocket::connect(ip, port, 3000);
    if (!socket_) {
        discovery_status_ =
            "Could not connect to " + ip + ":" + std::to_string(port);
        return;
    }
    recv_buf_.reset();
    send_buf_.clear();
    join_error_.clear();
    join_sent_ = false;
    name_field_.focused = false;
    state_ = ClientState::JoinName;
}

void Client::poll_network() {
    char buf[4096];
    for (;;) {
        auto n = socket_.recv(buf, sizeof(buf));
        if (n > 0) {
            try {
                recv_buf_.append(buf, static_cast<std::size_t>(n));
            } catch (const message_too_large_error&) {
                on_server_disconnected("Message too large");
                return;
            }
        } else if (n == 0) {
            on_server_disconnected("Server closed the connection");
            return;
        } else {
            break;  // EAGAIN — no more data right now.
        }
    }

    while (auto line = recv_buf_.extract()) {
        handle_message(*line);
        if (!socket_) return;  // Disconnected while handling.
    }
}

void Client::flush_send() {
    while (!send_buf_.empty() && socket_) {
        auto n = socket_.send(send_buf_.data(), send_buf_.size());
        if (n > 0) {
            send_buf_.erase(0, static_cast<std::size_t>(n));
        } else if (n == -1) {
            break;  // EAGAIN — retry next frame.
        } else {
            // n == -2: real send error — connection is broken.
            on_server_disconnected("Connection lost");
            return;
        }
    }
}

void Client::send_message(const nlohmann::json& j) {
    if (!socket_) return;
    send_buf_ += framing::encode(j);
    flush_send();
}

void Client::handle_message(const std::string& line) {
    auto j = json::parse_line(line);
    if (!j) return;  // Never expected from a spec-compliant server; ignore.

    auto msg = msg::from_json(*j);

    if (auto* err = std::get_if<Error>(&msg)) {
        if (state_ == ClientState::JoinName) {
            // NAME_INVALID keeps the connection open — show and retry.
            join_error_ = err->reason;
            join_sent_ = false;  // Allow retry after error
        } else {
            status_line_ = "Server error: " + err->reason;
        }
        return;
    }

    if (auto* welcome = std::get_if<Welcome>(&msg)) {
        player_id_ = welcome->player_id;
        config_ = welcome->config;
        join_error_.clear();
        join_sent_ = false;
        send_buf_.clear();  // Drop stale join data that wasn't flushed
        state_ = ClientState::Lobby;
        return;
    }

    if (auto* lobby = std::get_if<Lobby>(&msg)) {
        lobby_players_ = lobby->players;
        // Keep an id → name map for the game view's player labels.
        player_names_.clear();
        for (const auto& p : lobby->players) {
            player_names_[p.id] = p.name;
        }
        // A lobby update during countdown/playing/game-over means the round
        // aborted or the post-game cycle restarted — return to the lobby.
        if (state_ == ClientState::JoinName ||
            state_ == ClientState::Lobby ||
            state_ == ClientState::Countdown ||
            state_ == ClientState::Playing ||
            state_ == ClientState::GameOver) {
            latest_state_.reset();
            known_players_.clear();
            departure_notice_.clear();
            winner_id_.clear();
            countdown_particles_init_ = false;
            send_buf_.clear();  // Drop stale messages on state change
            state_ = ClientState::Lobby;
        }
        return;
    }

    if (auto* cd = std::get_if<Countdown>(&msg)) {
        countdown_particles_init_ = false;
        countdown_seconds_ = cd->seconds;
        if (state_ == ClientState::Lobby ||
            state_ == ClientState::Countdown) {
            state_ = ClientState::Countdown;
        }
        return;
    }

    if (std::get_if<Start>(&msg)) {
        if (state_ == ClientState::Countdown ||
            state_ == ClientState::Lobby) {
            latest_state_.reset();
            known_players_.clear();  // Fresh round — rebuild from states.
            departure_notice_.clear();
            input_.reset();  // Fresh round — neutral direction.
            match_start_time_ = GetTime();
            state_ = ClientState::Playing;
        }
        return;
    }

    if (auto* st = std::get_if<State>(&msg)) {
        if (state_ == ClientState::Playing) {
            // Infer departures: players present in the previous state but
            // missing from the new one have left the round.
            std::map<std::string, ctf::Player> next;
            for (const auto& p : st->players) {
                next.emplace(p.id, p);
            }
            if (!known_players_.empty()) {
                for (const auto& [id, p] : known_players_) {
                    if (next.find(id) == next.end()) {
                        departure_notice_ =
                            name_of(id) + " left the game";
                        departure_notice_time_ = GetTime();
                    }
                }
            }
            known_players_ = std::move(next);
            latest_state_ = *st;
        }
        return;
    }

    if (auto* over = std::get_if<GameOver>(&msg)) {
        if (state_ == ClientState::Playing) {
            winner_id_ = over->winner;
            match_end_time_ = GetTime();
            state_ = ClientState::GameOver;
        }
        return;
    }
}

void Client::on_server_disconnected(const std::string& reason) {
    socket_.close();
    recv_buf_.reset();
    send_buf_.clear();
    disconnect_reason_ = reason;
    state_ = ClientState::Disconnected;
}

void Client::return_to_discovery(const std::string& status) {
    socket_.close();
    recv_buf_.reset();
    send_buf_.clear();
    player_id_.clear();
    config_.reset();
    lobby_players_.clear();
    player_names_.clear();
    known_players_.clear();
    departure_notice_.clear();
    latest_state_.reset();
    winner_id_.clear();
    selected_server_ = -1;
    join_error_.clear();
    join_sent_ = false;
    discovery_status_ = status;
    state_ = ClientState::Discovery;
    start_broadcast_discovery();
}

// ── Discovery actions ────────────────────────────────────────────────────

void Client::start_broadcast_discovery() {
    if (discovering_) return;
    discovery_.clear();
    selected_server_ = -1;
    discovering_ = true;
    broadcast_future_ = std::async(std::launch::async, [this] {
        discovery_.discover(constants::discovery_port, 2000);
    });
}

void Client::start_manual_discovery(const std::string& ip) {
    if (unicast_future_.valid()) return;  // One manual query at a time.
    discovery_status_ = "Contacting " + ip + "...";
    unicast_future_ = std::async(std::launch::async, [this, ip] {
        return discovery_.discover_unicast(ip, constants::discovery_port,
                                           1500);
    });
}

void Client::check_discovery_futures() {
    if (discovering_ && broadcast_future_.valid() &&
        broadcast_future_.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        broadcast_future_.get();
        discovering_ = false;
        if (discovery_.servers().empty()) {
            discovery_status_ =
                "No servers found — try manual or direct connect";
        }
    }

    if (unicast_future_.valid() &&
        unicast_future_.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        auto entry = unicast_future_.get();
        if (entry) {
            connect_to(entry->ip, entry->tcp_port);
        } else {
            discovery_status_ = "No response from that IP";
        }
    }
}

// ── UI helpers ───────────────────────────────────────────────────────────

auto Client::button(const char* label, float x, float y, float w, float h)
    -> bool {
    Rectangle r{x, y, w, h};
    const bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, hover ? BTN_HOVER_COLOR : BTN_COLOR);
    DrawRectangleLinesEx(r, 1.0f, GRAY);

    const int font_size = 18;
    const int tw = MeasureText(label, font_size);
    DrawText(label,
             static_cast<int>(x + (w - static_cast<float>(tw)) / 2.0f),
             static_cast<int>(y + (h - static_cast<float>(font_size)) / 2.0f),
             font_size, WHITE);
    return hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

void Client::text_field(TextField& field, float x, float y, float w,
                        float h) {
    Rectangle r{x, y, w, h};

    // Focus follows mouse clicks.
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        field.focused = CheckCollisionPointRec(GetMousePosition(), r);
    }

    if (field.focused) {
        int cp = GetCharPressed();
        while (cp > 0) {
            // Printable ASCII only (names forbid control characters).
            if (cp >= 32 && cp <= 126 &&
                static_cast<int>(field.text.size()) < field.max_len) {
                field.text.push_back(static_cast<char>(cp));
            }
            cp = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (!field.text.empty()) field.text.pop_back();
        }
    }

    DrawRectangleRec(r, PANEL_COLOR);
    DrawRectangleLinesEx(r, 1.0f, field.focused ? SKYBLUE : GRAY);

    const int font_size = 18;
    DrawText(field.text.c_str(), static_cast<int>(x + 8),
             static_cast<int>(y + (h - static_cast<float>(font_size)) / 2.0f),
             font_size, WHITE);

    // Blinking cursor while focused.
    if (field.focused &&
        static_cast<int>(GetTime() * 2.0) % 2 == 0) {
        const int tw = MeasureText(field.text.c_str(), font_size);
        DrawText("|", static_cast<int>(x + 10.0f + static_cast<float>(tw)),
                 static_cast<int>(y + (h - static_cast<float>(font_size)) /
                                          2.0f),
                 font_size, SKYBLUE);
    }
}

void Client::draw_spinner(float cx, float cy, const char* label) {
    constexpr float RADIUS = 12.0f;
    DrawCircleLines(static_cast<int>(cx), static_cast<int>(cy), RADIUS, GRAY);
    const float angle = static_cast<float>(GetTime()) * 4.0f;
    DrawLine(static_cast<int>(cx), static_cast<int>(cy),
             static_cast<int>(cx + std::cos(angle) * RADIUS),
             static_cast<int>(cy + std::sin(angle) * RADIUS), WHITE);
    DrawText(label, static_cast<int>(cx + RADIUS + 10),
             static_cast<int>(cy - 10), 20, LIGHTGRAY);
}

auto Client::name_of(const std::string& id) const -> std::string {
    if (auto it = player_names_.find(id); it != player_names_.end()) {
        return it->second;
    }
    return id;
}

// ── Screen: Discovery ────────────────────────────────────────────────────

void Client::update_discovery() {
    check_discovery_futures();

    // Enter in the manual field triggers manual connect.
    if (manual_ip_.focused && IsKeyPressed(KEY_ENTER)) {
        const auto ip = trimmed(manual_ip_.text);
        if (!ip.empty()) start_manual_discovery(ip);
    }
    // Enter in the direct field triggers direct connect.
    if (direct_addr_.focused && IsKeyPressed(KEY_ENTER)) {
        const auto addr = trimmed(direct_addr_.text);
        if (auto pp = discovery::parse_ip_port(addr)) {
            connect_to(pp->first, pp->second);
        } else if (!addr.empty()) {
            discovery_status_ = "Invalid format — use IP:port";
        }
    }
}

void Client::draw_discovery() {
    DrawText("CTF - Server Discovery", 60, 30, 30, WHITE);

    if (discovering_) {
        draw_spinner(610, 45, "Discovering servers...");
    }

    // ── Server table ────────────────────────────────────────────────
    constexpr float TX = 60, TY = 90, TW = 680, ROW_H = 30;
    constexpr int MAX_ROWS = 12;
    DrawRectangle(TX, TY, TW, ROW_H, PANEL_COLOR);
    DrawText("IP", TX + 10, TY + 7, 16, YELLOW);
    DrawText("Name", TX + 200, TY + 7, 16, YELLOW);
    DrawText("State", TX + 420, TY + 7, 16, YELLOW);
    DrawText("Players", TX + 560, TY + 7, 16, YELLOW);

    const auto& servers = discovery_.servers();
    if (discovering_ && servers.empty()) {
        DrawText("Searching the local network...", TX + 10, TY + 45, 18,
                 GRAY);
    }

    int row = 0;
    for (const auto& s : servers) {
        if (row >= MAX_ROWS) break;
        const float ry = TY + ROW_H * (static_cast<float>(row) + 1.0f);
        if (row % 2 == 0) {
            DrawRectangle(TX, ry, TW, ROW_H, ROW_ALT_COLOR);
        }
        if (row == selected_server_) {
            DrawRectangle(TX, ry, TW, ROW_H, SELECT_COLOR);
        }
        if (is_clicked(TX, ry, TW, ROW_H)) {
            selected_server_ = row;
        }

        DrawText(s.ip.c_str(), TX + 10, ry + 7, 16, WHITE);
        DrawText(s.name.c_str(), TX + 200, ry + 7, 16, WHITE);
        DrawText(s.state.c_str(), TX + 420, ry + 7, 16,
                 s.state == "lobby" ? GREEN : ORANGE);
        DrawText(std::to_string(s.players).c_str(), TX + 580, ry + 7, 16,
                 WHITE);
        ++row;
    }
    DrawRectangleLines(TX, TY, TW, ROW_H * (MAX_ROWS + 1), GRAY);

    // ── Table actions ───────────────────────────────────────────────
    if (button("Search again", TX, 530, 160, 36) && !discovering_) {
        start_broadcast_discovery();
    }

    const bool can_connect =
        !discovering_ && selected_server_ >= 0 &&
        selected_server_ < static_cast<int>(servers.size());
    if (button("Connect", 240, 530, 160, 36) && can_connect) {
        const auto& s = servers[static_cast<std::size_t>(selected_server_)];
        connect_to(s.ip, s.tcp_port);
    }

    // ── Manual unicast ──────────────────────────────────────────────
    DrawText("Server IP:", 60, 592, 18, LIGHTGRAY);
    text_field(manual_ip_, 160, 585, 210, 36);
    if (button("Manual connect", 390, 585, 180, 36)) {
        const auto ip = trimmed(manual_ip_.text);
        if (ip.empty()) {
            discovery_status_ = "Enter an IP address first";
        } else {
            start_manual_discovery(ip);
        }
    }

    // ── Direct IP:port ──────────────────────────────────────────────
    DrawText("IP:port:", 60, 652, 18, LIGHTGRAY);
    text_field(direct_addr_, 160, 645, 210, 36);
    if (button("Direct connect", 390, 645, 180, 36)) {
        const auto addr = trimmed(direct_addr_.text);
        if (auto pp = discovery::parse_ip_port(addr)) {
            connect_to(pp->first, pp->second);
        } else {
            discovery_status_ = "Invalid format — use IP:port";
        }
    }

    // ── Status line ─────────────────────────────────────────────────
    if (!discovery_status_.empty()) {
        DrawText(discovery_status_.c_str(), 60, 710, 18, YELLOW);
    }
}

// ── Screen: Join name ────────────────────────────────────────────────────

void Client::update_join_name() {
    if (!join_sent_ && IsKeyPressed(KEY_ENTER)) {
        const auto name = trimmed(name_field_.text);
        if (name.empty() ||
            name.size() >
                static_cast<std::size_t>(constants::name_max_length)) {
            join_error_ = NAME_INVALID;
        } else {
            nlohmann::json j;
            j["type"] = "join";
            j["v"] = 1;
            j["name"] = name;
            send_message(j);
            join_error_.clear();
            join_sent_ = true;
        }
    }
}

void Client::draw_join_name() {
    DrawText("Join Server", 60, 60, 30, WHITE);

    DrawText("Connected. Choose your display name.", 60, 115, 18,
             LIGHTGRAY);

    DrawText("Your name:", 60, 175, 18, LIGHTGRAY);
    text_field(name_field_, 60, 200, 300, 40);
    DrawText("1-20 characters, no control characters", 375, 211, 14, GRAY);

    if (!join_sent_ && button("Join", 60, 270, 140, 40)) {
        const auto name = trimmed(name_field_.text);
        if (name.empty() ||
            name.size() >
                static_cast<std::size_t>(constants::name_max_length)) {
            join_error_ = NAME_INVALID;
        } else {
            nlohmann::json j;
            j["type"] = "join";
            j["v"] = 1;
            j["name"] = name;
            send_message(j);
            join_error_.clear();
            join_sent_ = true;
        }
    }

    if (button("Back", 220, 270, 140, 40)) {
        return_to_discovery("Left the server");
    }

    // Visual feedback when join was sent but not yet acknowledged.
    if (join_sent_ && join_error_.empty()) {
        DrawText("Sending join request...", 60, 320, 16, YELLOW);
    }

    if (!join_error_.empty()) {
        std::string msg = "Join rejected: " + join_error_;
        if (join_error_ == NAME_INVALID) {
            msg += " — pick another name";
        }
        DrawText(msg.c_str(), 60, 340, 18, RED);
    }
}

// ── Screen: Lobby ────────────────────────────────────────────────────────

void Client::update_lobby() {
    // Toggle help overlay with H.
    if (IsKeyPressed(KEY_H)) {
        show_help_ = !show_help_;
    }

    if (show_help_) {
        // Click outside the help panel to close it.
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            constexpr float PX = 100, PY = 60, PW = 600, PH = 660;
            Rectangle panel_rect{PX, PY, PW, PH};
            if (!CheckCollisionPointRec(GetMousePosition(), panel_rect)) {
                show_help_ = false;
            }
        }
        // Don't process lobby UI clicks while help is open.
        return;
    }
}

void Client::draw_lobby() {
    // ── Ambient particles background ────────────────────────────────
    if (!countdown_particles_init_) {
        gui::init_particles(countdown_particles_, 30,
                            static_cast<float>(WINDOW_W),
                            static_cast<float>(WINDOW_H));
        countdown_particles_init_ = true;
    }
    gui::draw_ambient_particles(countdown_particles_,
                                static_cast<float>(WINDOW_W),
                                static_cast<float>(WINDOW_H),
                                GetFrameTime());

    // ── Title ────────────────────────────────────────────────────────
    const char* title = "GAME LOBBY";
    int tw = MeasureText(title, 34);
    DrawText(title, (WINDOW_W - tw) / 2, 35, 34, WHITE);

    // Player ID subtitle.
    std::string id_line = "ID: " + player_id_;
    DrawText(id_line.c_str(), (WINDOW_W - MeasureText(id_line.c_str(), 16)) / 2,
             80, 16, gui::TEXT_DIM);

    // ── Compact server config panel (top-right, shorter to avoid overlap) ─
    constexpr float CX = 580, CY = 5, CW = 200, CH = 105;
    DrawRectangleRounded(Rectangle{CX, CY, CW, CH}, 0.12f, 6,
                         gui::PANEL_COLOR);
    DrawRectangleRoundedLines(Rectangle{CX, CY, CW, CH}, 0.12f, 6,
                              Color{60, 65, 75, 200});
    DrawText("Server Config", CX + 10, CY + 8, 14, gui::ACCENT_BLUE);
    if (config_) {
        auto line = [&](const char* key, int value, float y) {
            DrawText(key, static_cast<int>(CX + 10), static_cast<int>(y),
                     13, gui::TEXT_DIM);
            DrawText(std::to_string(value).c_str(),
                     static_cast<int>(CX + 110), static_cast<int>(y),
                     13, WHITE);
        };
        line("Map",       config_->map_size,        CY + 32);
        line("Speed",     config_->speed,           CY + 50);
        line("Tick",      config_->tick_rate,       CY + 68);
        line("Radius",    config_->player_radius,   CY + 86);
    }

    // ── Player table (moved down to avoid overlap with config panel) ─
    constexpr float TX = 80, TY = 140, TW = 560, ROW_H = 30;
    constexpr int MAX_ROWS = 14;

    // Header
    DrawRectangle(static_cast<int>(TX), static_cast<int>(TY),
                  static_cast<int>(TW), static_cast<int>(ROW_H),
                  gui::PANEL_COLOR);
    DrawText("#",    static_cast<int>(TX + 12), static_cast<int>(TY + 6),
             16, gui::ACCENT_BLUE);
    DrawText("ID",   static_cast<int>(TX + 60), static_cast<int>(TY + 6),
             16, gui::ACCENT_BLUE);
    DrawText("Name", static_cast<int>(TX + 280), static_cast<int>(TY + 6),
             16, gui::ACCENT_BLUE);
    DrawText("Status", static_cast<int>(TX + 440), static_cast<int>(TY + 6),
             16, gui::ACCENT_BLUE);
    DrawRectangleLines(static_cast<int>(TX), static_cast<int>(TY),
                       static_cast<int>(TW), static_cast<int>(ROW_H),
                       Color{60, 65, 75, 200});

    int row = 0;
    for (const auto& p : lobby_players_) {
        if (row >= MAX_ROWS) break;
        const float ry = TY + ROW_H * (static_cast<float>(row) + 1.0f);
        const bool is_self = (p.id == player_id_);

        if (is_self) {
            DrawRectangle(static_cast<int>(TX), static_cast<int>(ry),
                          static_cast<int>(TW), static_cast<int>(ROW_H),
                          gui::SELECT_COLOR);
        } else if (row % 2 == 0) {
            DrawRectangle(static_cast<int>(TX), static_cast<int>(ry),
                          static_cast<int>(TW), static_cast<int>(ROW_H),
                          gui::ROW_ALT_COLOR);
        }

        // Row #
        DrawText(std::to_string(row + 1).c_str(),
                 static_cast<int>(TX + 12), static_cast<int>(ry + 6),
                 15, gui::TEXT_DIM);
        // ID
        DrawText(p.id.c_str(),
                 static_cast<int>(TX + 60), static_cast<int>(ry + 6),
                 15, is_self ? WHITE : SKYBLUE);
        // Name
        DrawText(p.name.c_str(),
                 static_cast<int>(TX + 280), static_cast<int>(ry + 6),
                 15, WHITE);

        // Status dot + label
        DrawCircle(static_cast<int>(TX + 450), static_cast<int>(ry + ROW_H / 2),
                   4.0f, GREEN);
        DrawText("ready",
                 static_cast<int>(TX + 460), static_cast<int>(ry + 6),
                 13, GREEN);

        if (is_self) {
            DrawText("(you)",
                     static_cast<int>(TX + TW - 58), static_cast<int>(ry + 6),
                     13, gui::ACCENT_BLUE);
        }
        ++row;
    }

    // Table border (only if rows exist)
    if (static_cast<int>(lobby_players_.size()) > 0) {
        int vis = std::min(static_cast<int>(lobby_players_.size()), MAX_ROWS);
        DrawRectangleLines(static_cast<int>(TX), static_cast<int>(TY),
                           static_cast<int>(TW),
                           static_cast<int>(ROW_H * (static_cast<float>(vis) + 1.0f)),
                           Color{60, 65, 75, 200});
    }

    // ── Bottom status bar ────────────────────────────────────────────
    // Semi-transparent bar across the full width.
    DrawRectangle(0, WINDOW_H - 48, WINDOW_W, 48,
                  Color{0, 0, 0, 180});

    std::string status;
    Color status_col;
    auto count = lobby_players_.size();
    if (count < static_cast<std::size_t>(constants::min_players)) {
        status = "[WAITING] " +
                 std::to_string(count) + "/" +
                 std::to_string(constants::min_players) +
                 " players";
        status_col = YELLOW;
    } else {
        status = "[READY] " +
                 std::to_string(count) + " players connected - starting soon!";
        status_col = GREEN;
    }
    int status_tw = MeasureText(status.c_str(), 16);
    DrawText(status.c_str(), (WINDOW_W - status_tw) / 2,
             WINDOW_H - 35, 16, status_col);

    // Error text overlay (if any).
    if (!status_line_.empty()) {
        DrawText(status_line_.c_str(), 60, 500, 16, RED);
    }
}

// ── Screen: Countdown ────────────────────────────────────────────────────

void Client::update_countdown() {}

void Client::draw_countdown() {
    // ── Particle background (reuse lobby particles, reinit each countdown) ─
    if (!countdown_particles_init_) {
        gui::init_particles(countdown_particles_, 35,
                            static_cast<float>(WINDOW_W),
                            static_cast<float>(WINDOW_H));
        countdown_particles_init_ = true;
    }
    gui::draw_ambient_particles(countdown_particles_,
                                static_cast<float>(WINDOW_W),
                                static_cast<float>(WINDOW_H),
                                GetFrameTime());

    // ── "Game starting" label ────────────────────────────────────────
    const char* start_label = "Game starting";
    DrawText(start_label,
             (WINDOW_W - MeasureText(start_label, 28)) / 2,
             180, 28, LIGHTGRAY);

    // ── Big number with shadow and color transition ──────────────────
    const std::string num = std::to_string(countdown_seconds_);

    // Colour: green (5-4) → yellow (3-2) → red (1)
    Color num_col;
    if (countdown_seconds_ >= 4) {
        num_col = Color{100, 220, 80, 255};   // green
    } else if (countdown_seconds_ >= 2) {
        num_col = Color{240, 220, 60, 255};    // yellow
    } else {
        num_col = Color{240, 70, 50, 255};     // red
    }

    const float pulse =
        1.0f + 0.15f * static_cast<float>(std::fmod(GetTime(), 1.0));
    const int font_size = static_cast<int>(220.0f * pulse);
    const int tw = MeasureText(num.c_str(), font_size);

    // Shadow (black, offset 3px).
    DrawText(num.c_str(), (WINDOW_W - tw) / 2 + 3,
             (WINDOW_H - font_size) / 2 + 3,
             font_size, Color{0, 0, 0, 120});

    // Main number.
    DrawText(num.c_str(), (WINDOW_W - tw) / 2, (WINDOW_H - font_size) / 2,
             font_size, num_col);

    // ── Expanding rings around the number ────────────────────────────
    constexpr float RING_BASE = 120.0f;
    float ring_r = RING_BASE + std::fmod(static_cast<float>(GetTime()) * 60.0f,
                                         60.0f);
    DrawCircleLines(WINDOW_W / 2, WINDOW_H / 2,
                    ring_r, Color{255, 255, 255, 20});
    DrawCircleLines(WINDOW_W / 2, WINDOW_H / 2,
                    ring_r + 15.0f, Color{255, 255, 255, 12});

    // ── Player roster below ──────────────────────────────────────────
    std::string player_list;
    for (std::size_t i = 0; i < lobby_players_.size(); ++i) {
        if (i > 0) player_list += ", ";
        player_list += lobby_players_[i].name;
    }
    if (player_list.empty()) {
        player_list = "No players";
    }
    // Truncate if too long.
    if (player_list.size() > 60) {
        player_list.resize(60);
        player_list += "...";
    }

    int roster_tw = MeasureText(player_list.c_str(), 14);
    DrawText(player_list.c_str(), (WINDOW_W - roster_tw) / 2,
             610, 14, gui::TEXT_DIM);

    std::string info =
        std::to_string(lobby_players_.size()) + " players in this round";
    DrawText(info.c_str(), (WINDOW_W - MeasureText(info.c_str(), 18)) / 2,
             640, 18, LIGHTGRAY);
}

// ── Screen: Playing ──────────────────────────────────────────────────────

void Client::update_playing() {
    input_.sample();

    // Send `input` only when the direction actually changes.
    if (input_.take_dir_changed()) {
        nlohmann::json j;
        j["type"] = "input";
        j["dir"] = {{"x", input_.dir_x()}, {"y", input_.dir_y()}};
        send_message(j);
    }

    // Send `interact` on E press (edge-detected).
    if (input_.take_interact()) {
        nlohmann::json j;
        j["type"] = "interact";
        send_message(j);
    }
}

void Client::draw_playing() {
    if (latest_state_) {
        double elapsed = GetTime() - match_start_time_;
        draw_game_view(*latest_state_, player_id_, player_names_,
                       elapsed, input_.dir_x(), input_.dir_y());
    } else {
        DrawText("Waiting for first state...", 60, 95, 20, GRAY);
    }

    // Transient departure notice (fades after ~3 s).
    if (!departure_notice_.empty() &&
        GetTime() - departure_notice_time_ < 3.0) {
        const int tw = MeasureText(departure_notice_.c_str(), 18);
        DrawText(departure_notice_.c_str(),
                 (WINDOW_W - tw) / 2, WINDOW_H - 40, 18, ORANGE);
    }
}

// ── Screen: Game over ────────────────────────────────────────────────────
// Full flow lands in a later task.

void Client::update_game_over() {}

void Client::draw_game_over() {
    // ── Compute match duration ───────────────────────────────────────
    double match_duration = match_end_time_ - match_start_time_;
    int mins = static_cast<int>(match_duration) / 60;
    int secs = static_cast<int>(match_duration) % 60;
    std::string duration_str = "Match duration: " +
                               std::to_string(mins) + ":" +
                               (secs < 10 ? "0" : "") + std::to_string(secs);

    // ── Winner banner ────────────────────────────────────────────────
    std::string winner_line;
    if (winner_id_ == player_id_) {
        winner_line = "*** YOU WIN! ***";
    } else {
        winner_line = "*** " + name_of(winner_id_) + " WINS! ***";
    }

    // Shadow
    DrawText(winner_line.c_str(),
             (WINDOW_W - MeasureText(winner_line.c_str(), 48)) / 2 + 3,
             323, 48, Color{0, 0, 0, 120});

    // Main banner in gold.
    DrawText(winner_line.c_str(),
             (WINDOW_W - MeasureText(winner_line.c_str(), 48)) / 2,
             320, 48, GOLD);

    // ── Subtitle ─────────────────────────────────────────────────────
    std::string sub;
    if (winner_id_ == player_id_) {
        sub = "Congratulations, " + name_of(player_id_) + "!";
    } else {
        sub = "Better luck next time!";
    }
    int sub_tw = MeasureText(sub.c_str(), 22);
    DrawText(sub.c_str(), (WINDOW_W - sub_tw) / 2, 385, 22, LIGHTGRAY);

    // ── Match info ───────────────────────────────────────────────────
    DrawText(duration_str.c_str(),
             (WINDOW_W - MeasureText(duration_str.c_str(), 18)) / 2,
             430, 18, gui::TEXT_DIM);

    // ── Player roster ────────────────────────────────────────────────
    std::string roster = "Players: ";
    for (std::size_t i = 0; i < lobby_players_.size(); ++i) {
        if (i > 0) roster += ", ";
        roster += lobby_players_[i].name;
        if (lobby_players_[i].id == winner_id_) roster += " (WINNER)";
    }
    if (roster.size() > 80) {
        roster.resize(80);
        roster += "...";
    }
    int roster_tw = MeasureText(roster.c_str(), 14);
    DrawText(roster.c_str(), (WINDOW_W - roster_tw) / 2,
             470, 14, gui::TEXT_DIM);

    // ── Return countdown ─────────────────────────────────────────────
    // The server sends `lobby` after the post-game pause (~5 s), which
    // transitions the client back to the Lobby screen automatically.
    // We show a pulsing "Returning to lobby..." message.
    float pulse = 1.0f + 0.08f * std::sin(static_cast<float>(GetTime()) * 3.0f);
    int ret_font = static_cast<int>(20.0f * pulse);
    const char* ret_msg = "Returning to lobby...";
    DrawText(ret_msg,
             (WINDOW_W - MeasureText(ret_msg, ret_font)) / 2,
             530, ret_font, Color{180, 180, 190, 200});
}

void Client::draw_help_overlay() {
    // Dim background overlay.
    DrawRectangle(0, 0, WINDOW_W, WINDOW_H,
                  Color{0, 0, 0, 200});

    constexpr float PX = 100, PY = 60, PW = 600, PH = 660;
    DrawRectangleRounded(Rectangle{PX, PY, PW, PH}, 0.08f, 8,
                         Color{25, 30, 40, 250});
    DrawRectangleRoundedLines(Rectangle{PX, PY, PW, PH}, 0.08f, 8,
                              Color{80, 90, 110, 200});

    float y = PY + 28;

    // Title
    const char* title = "How to Play";
    int tw = MeasureText(title, 32);
    DrawText(title, (WINDOW_W - tw) / 2, static_cast<int>(y), 32, GOLD);
    y += 50;

    // ── Objective ──────────────────────────────────────────────────
    DrawText("Objective", static_cast<int>(PX + 30),
             static_cast<int>(y), 20, gui::ACCENT_BLUE);
    y += 28;
    DrawText("- Capture the flag and carry it outside the", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("  centre circle to score for your team.", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("- Steal the flag from opponents by touching them", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("  while they carry it.", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 38;

    // ── Controls ───────────────────────────────────────────────────
    DrawText("Controls", static_cast<int>(PX + 30),
             static_cast<int>(y), 20, gui::ACCENT_BLUE);
    y += 28;
    DrawText("W A S D  or  Arrow Keys  —  Move", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, WHITE);
    y += 22;
    DrawText("E  —  Interact (capture / steal flag)", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, WHITE);
    y += 22;
    DrawText("ESC  —  Quit game  /  Close this screen", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, WHITE);
    y += 22;
    DrawText("H  —  Toggle this help screen", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, WHITE);
    y += 38;

    // ── Rules ──────────────────────────────────────────────────────
    DrawText("Rules", static_cast<int>(PX + 30),
             static_cast<int>(y), 20, gui::ACCENT_BLUE);
    y += 28;
    DrawText("- Each player spawns outside the centre circle.", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("- Touch the flag to pick it up (E key).", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("- Run outside the circle to score (distance > 315).", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("- If you are carrying the flag and get touched,", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("  the flag is stolen by the opponent.", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 22;
    DrawText("- Players leave the game when they disconnect.", static_cast<int>(PX + 30),
             static_cast<int>(y), 16, LIGHTGRAY);
    y += 38;

    // ── Legend ─────────────────────────────────────────────────────
    DrawText("Colour Legend", static_cast<int>(PX + 30),
             static_cast<int>(y), 20, gui::ACCENT_BLUE);
    y += 28;
    DrawCircle(static_cast<int>(PX + 38),
               static_cast<int>(y + 6), 6, GREEN);
    DrawText("= You", static_cast<int>(PX + 52),
             static_cast<int>(y), 16, WHITE);
    y += 24;
    DrawCircle(static_cast<int>(PX + 38),
               static_cast<int>(y + 6), 6, BLUE);
    DrawText("= Other player", static_cast<int>(PX + 52),
             static_cast<int>(y), 16, WHITE);
    y += 24;
    DrawCircle(static_cast<int>(PX + 38),
               static_cast<int>(y + 6), 6, RED);
    DrawText("= Flag carrier", static_cast<int>(PX + 52),
             static_cast<int>(y), 16, WHITE);
    y += 40;

    // ── Close hint ─────────────────────────────────────────────────
    const char* close_hint = "Press H or ESC to close";
    int ch_tw = MeasureText(close_hint, 16);
    DrawText(close_hint, (WINDOW_W - ch_tw) / 2,
             static_cast<int>(PY + PH - 30), 16, gui::TEXT_DIM);
}

// ── Screen: Disconnected ─────────────────────────────────────────────────

void Client::update_disconnected() {}

void Client::draw_disconnected() {
    DrawText("Server disconnected", 60, 60, 30, RED);
    DrawText(disconnect_reason_.c_str(), 60, 115, 20, LIGHTGRAY);
    DrawText("The TCP connection was closed or lost.", 60, 145, 16, GRAY);

    if (button("Back to menu", 60, 200, 200, 40)) {
        return_to_discovery("");
    }
}

}  // namespace ctf::client
