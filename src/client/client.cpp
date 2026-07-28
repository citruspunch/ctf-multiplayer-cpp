#include "client.hpp"

#include "constants.hpp"
#include "game_view.hpp"
#include "json.hpp"

#ifdef __APPLE__
#include "app_activation.hpp"
#endif

#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

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
    // Step 1: promote process to foreground *before* InitWindow (needed
    // for WindowServer access).  No-op on non-Apple and harmless when
    // launched via `open ctf.app`.
    activate_macos_app();
#endif
#ifdef __APPLE__
    // On macOS, the GLFW NSWindow has a known issue where it initialises
    // in a hidden/inactive state and doesn't come to the front when the
    // process is launched outside LaunchServices.  Requesting the window
    // to be topmost and resizable up-front (BEFORE InitWindow) makes
    // GLFW create the NSWindow with the correct level and order it to
    // the front automatically.  Without this, the window may render
    // fine but stay behind other windows or be marked "Not Responding"
    // by the system because it never responds to activation events.
    SetConfigFlags(FLAG_WINDOW_TOPMOST | FLAG_WINDOW_RESIZABLE);
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#endif
    InitWindow(WINDOW_W, WINDOW_H, "CTF — Client");
#ifdef __APPLE__
    // Step 2: [NSApp activateIgnoringOtherApps:YES] *after* InitWindow
    // brings the window to front.  Without this the NSWindow is created
    // but never ordered forward.
    activate_macos_app_after_init();
#endif
    window_open_ = true;
    SetTargetFPS(60);
    SetExitKey(0);  // ESC should not kill the app while typing.

    start_broadcast_discovery();

    while (!WindowShouldClose()) {
        update();
        BeginDrawing();
        ClearBackground(BG_COLOR);
        draw();
        EndDrawing();
    }
}

void Client::update() {
    if (socket_) {
        poll_network();
        flush_send();
    }

    switch (state_) {
        case ClientState::Discovery:    update_discovery();    break;
        case ClientState::JoinName:     update_join_name();    break;
        case ClientState::Lobby:        update_lobby();        break;
        case ClientState::Countdown:    update_countdown();    break;
        case ClientState::Playing:      update_playing();      break;
        case ClientState::GameOver:     update_game_over();    break;
        case ClientState::Disconnected: update_disconnected(); break;
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
        } else {
            status_line_ = "Server error: " + err->reason;
        }
        return;
    }

    if (auto* welcome = std::get_if<Welcome>(&msg)) {
        player_id_ = welcome->player_id;
        config_ = welcome->config;
        join_error_.clear();
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
            state_ = ClientState::Lobby;
        }
        return;
    }

    if (auto* cd = std::get_if<Countdown>(&msg)) {
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
    DrawText("CTF — Server Discovery", 60, 30, 30, WHITE);

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
    if (IsKeyPressed(KEY_ENTER)) {
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

    if (button("Join", 60, 270, 140, 40)) {
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
        }
    }

    if (button("Back", 220, 270, 140, 40)) {
        return_to_discovery("Left the server");
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

void Client::update_lobby() {}

void Client::draw_lobby() {
    DrawText("Lobby", 60, 40, 30, WHITE);

    const std::string id_line = "In waiting room — ID: " + player_id_;
    DrawText(id_line.c_str(), 60, 92, 20, SKYBLUE);

    // ── Server config panel (from welcome) ──────────────────────────
    constexpr float CX = 470, CY = 40, CW = 280, CH = 170;
    DrawRectangle(CX, CY, CW, CH, PANEL_COLOR);
    DrawRectangleLines(CX, CY, CW, CH, GRAY);
    DrawText("Server config", CX + 12, CY + 10, 18, YELLOW);
    if (config_) {
        const auto line = [](const char* key, int value, float y) {
            DrawText(key, CX + 12, static_cast<int>(y), 16, LIGHTGRAY);
            const std::string v = std::to_string(value);
            DrawText(v.c_str(), CX + 200, static_cast<int>(y), 16, WHITE);
        };
        line("map_size",        config_->map_size,        CY + 40);
        line("circle_radius",   config_->circle_radius,   CY + 62);
        line("player_radius",   config_->player_radius,   CY + 84);
        line("interact_radius", config_->interact_radius, CY + 106);
        line("speed",           config_->speed,           CY + 128);
        line("tick_rate",       config_->tick_rate,       CY + 150);
    } else {
        DrawText("(no config)", CX + 12, CY + 40, 16, GRAY);
    }

    // ── Player table ────────────────────────────────────────────────
    const std::string header = "Players (" +
                               std::to_string(lobby_players_.size()) + "/" +
                               std::to_string(constants::max_players) + ")";
    DrawText(header.c_str(), 60, 150, 20, YELLOW);

    constexpr float TX = 60, TY = 185, TW = 680, ROW_H = 28;
    constexpr int MAX_ROWS = 16;
    DrawRectangle(TX, TY, TW, ROW_H, PANEL_COLOR);
    DrawText("ID", TX + 12, TY + 6, 16, YELLOW);
    DrawText("Name", TX + 140, TY + 6, 16, YELLOW);

    int row = 0;
    for (const auto& p : lobby_players_) {
        if (row >= MAX_ROWS) break;
        const float ry = TY + ROW_H * (static_cast<float>(row) + 1.0f);
        const bool is_self = (p.id == player_id_);
        if (is_self) {
            DrawRectangle(TX, ry, TW, ROW_H, SELECT_COLOR);
        } else if (row % 2 == 0) {
            DrawRectangle(TX, ry, TW, ROW_H, ROW_ALT_COLOR);
        }
        DrawText(p.id.c_str(), TX + 12, ry + 6, 16,
                 is_self ? WHITE : LIGHTGRAY);
        DrawText(p.name.c_str(), TX + 140, ry + 6, 16, WHITE);
        if (is_self) {
            DrawText("(you)", TX + 620, ry + 6, 16, SKYBLUE);
        }
        ++row;
    }
    DrawRectangleLines(TX, TY, TW, ROW_H * (MAX_ROWS + 1), GRAY);

    // No start button — the countdown triggers automatically.
    if (lobby_players_.size() <
        static_cast<std::size_t>(constants::min_players)) {
        DrawText("Waiting for more players — the game starts automatically "
                 "when 2+ players are connected.",
                 60, 700, 16, YELLOW);
    } else {
        DrawText("Enough players — get ready!", 60, 700, 16, GREEN);
    }

    if (!status_line_.empty()) {
        DrawText(status_line_.c_str(), 60, 730, 16, RED);
    }
}

// ── Screen: Countdown ────────────────────────────────────────────────────

void Client::update_countdown() {}

void Client::draw_countdown() {
    DrawText("Game starting", (WINDOW_W - MeasureText("Game starting", 28)) / 2,
             220, 28, LIGHTGRAY);

    // Large centred number, pulse once per second change.
    const std::string num = std::to_string(countdown_seconds_);
    const float pulse =
        1.0f + 0.15f * static_cast<float>(std::fmod(GetTime(), 1.0));
    const int font_size = static_cast<int>(220.0f * pulse);
    const int tw = MeasureText(num.c_str(), font_size);
    DrawText(num.c_str(), (WINDOW_W - tw) / 2, (WINDOW_H - font_size) / 2,
             font_size, YELLOW);

    const std::string info =
        std::to_string(lobby_players_.size()) + " players in this round";
    DrawText(info.c_str(), (WINDOW_W - MeasureText(info.c_str(), 20)) / 2, 560,
             20, LIGHTGRAY);
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
        draw_game_view(*latest_state_, player_id_, player_names_);
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
    // Resolve the winner's display name from the lobby roster.
    std::string line;
    if (winner_id_ == player_id_) {
        line = "You won!";
    } else {
        line = name_of(winner_id_) + " won!";
    }
    const int font_size = 48;
    DrawText(line.c_str(),
             (WINDOW_W - MeasureText(line.c_str(), font_size)) / 2,
             320, font_size, GOLD);

    // The server sends `lobby` after the post-game pause (~5 s), which
    // transitions the client back to the Lobby screen automatically.
    DrawText("Returning to lobby...",
             (WINDOW_W - MeasureText("Returning to lobby...", 20)) / 2,
             400, 20, LIGHTGRAY);
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
