#include "server_view.hpp"
#include "server.hpp"
#include "constants.hpp"
#include "gui_helpers.hpp"

#ifdef __APPLE__
#include "app_activation.hpp"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif

#include <raylib.h>

#include <string>

namespace ctf::server {

static constexpr int WINDOW_W = 800;
static constexpr int WINDOW_H = 800;

// ── Layout constants for the lobby view ──────────────────────────────────

namespace lobby_layout {

constexpr float TITLE_Y          = 50.0f;
constexpr int   TITLE_FONT       = 38;
constexpr float IP_Y             = 110.0f;
constexpr int   IP_FONT          = 18;
constexpr float STATUS_Y         = 160.0f;
constexpr int   STATUS_FONT      = 16;
constexpr float TABLE_Y          = 210.0f;
constexpr float TABLE_W          = 620.0f;
constexpr float ROW_H            = 30.0f;
constexpr int   MAX_ROWS         = 12;
constexpr float BTN_Y            = 620.0f;
constexpr float BTN_W            = 240.0f;
constexpr float BTN_H            = 56.0f;

constexpr auto table_x() -> float { return (WINDOW_W - TABLE_W) / 2.0f; }
constexpr auto btn_x() -> float { return (WINDOW_W - BTN_W) / 2.0f; }
constexpr auto btn_rect() -> Rectangle {
    return Rectangle{btn_x(), BTN_Y, BTN_W, BTN_H};
}

}  // namespace lobby_layout

// ── Server destructor (defined here where ServerView is complete) ─────────

Server::~Server() {
    sessions_.clear();
    if (listener_) {
        poller_.remove_fd(listener_.native_handle());
    }
}

// ── Observer view lifecycle ──────────────────────────────────────────────

void ServerViewDeleter::operator()(ServerView* p) const noexcept {
    delete p;
}

void Server::init_observer() {
    if (headless_) return;
    view_.reset(new ServerView(game_state_));
}

void Server::render_observer() {
    if (!view_) return;

    std::string phase_text;
    switch (phase_) {
        case Phase::Lobby:
            phase_text = "LOBBY";
            break;
        case Phase::Countdown:
            phase_text = "COUNTDOWN " + std::to_string(countdown_remaining_);
            break;
        case Phase::Playing:
            phase_text = "PLAYING";
            break;
        case Phase::PostGame:
            if (game_over_sent_) {
                std::string winner_name;
                for (const auto& ps : game_state_.players) {
                    if (game_state_.flag.owner.has_value() &&
                        ps.id == game_state_.flag.owner.value()) {
                        winner_name = ps.name;
                        break;
                    }
                }
                if (winner_name.empty())
                    winner_name = game_state_.flag.owner.value_or("?");
                phase_text = "GAME OVER — Winner: " + winner_name;
            } else {
                phase_text = "POST GAME";
            }
            break;
    }

    // Build lobby player list.
    std::vector<std::pair<std::string, std::string>> names;
    for (const auto& [fd, s] : sessions_) {
        if (s->joined)
            names.emplace_back(s->player_id, s->player_name);
    }

    view_->render(phase_text, names, session_count(), server_ip_);
}

auto Server::should_close_observer() -> bool {
    if (!view_) return false;
    if (view_->should_close()) return true;
#ifdef __APPLE__
    if (macos_quit_requested()) {
        return true;
    }
#endif
    return false;
}

auto Server::observer_start_requested() -> bool {
    if (!view_) return false;
    return view_->start_requested();
}

ServerView::ServerView(const game::GameState& game_state)
    : game_state_(game_state) {}

ServerView::~ServerView() {
    if (initialised_) {
        CloseWindow();
    }
}

auto ServerView::should_close() -> bool {
    if (!initialised_) return false;
    return WindowShouldClose();
}

auto ServerView::start_requested() -> bool {
    if (!initialised_) return false;

    // 1. Edge-detect SPACE keypress (silent keyboard fallback).
    bool cur_space = IsKeyDown(KEY_SPACE);
    bool space_pressed = cur_space && !last_space_state_;
    last_space_state_ = cur_space;
    if (space_pressed) {
        start_pressed_ = true;
        return true;
    }

    // 2. Edge-detect mouse click on the START GAME button.
    //    The button rect is defined in lobby_layout — same position
    //    that draw_lobby_view() uses, so the hit-test matches what
    //    the user sees.
    bool cur_mouse = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool mouse_just_pressed = cur_mouse && !last_mouse_left_;
    last_mouse_left_ = cur_mouse;

    if (mouse_just_pressed) {
        Rectangle btn = lobby_layout::btn_rect();
        if (CheckCollisionPointRec(GetMousePosition(), btn)) {
            start_pressed_ = true;
            return true;
        }
    }

    return false;
}

void ServerView::render(const std::string& phase_text,
                        const std::vector<std::pair<std::string, std::string>>& lobby_names,
                        int lobby_count,
                        const std::string& server_ip) {
    if (!initialised_) {
#ifdef __APPLE__
        activate_macos_app();
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#else
        SetConfigFlags(FLAG_VSYNC_HINT);
#endif
        InitWindow(WINDOW_W, WINDOW_H, "CTF Server — Observer");
#ifdef __APPLE__
        activate_macos_app_after_init();
        install_macos_menu();
        clear_macos_quit_request();
#endif
        SetTargetFPS(60);
        SetExitKey(0);
        initialised_ = true;
    }

    BeginDrawing();
    ClearBackground(gui::BG_COLOR);

    // Branch: lobby view (no game field) vs game field (in-game phases).
    if (phase_text == "LOBBY") {
        draw_lobby_view(server_ip, lobby_names, lobby_count);
    } else {
        draw_game_field(phase_text);
    }

    // ── Apple Silicon: GL → Metal bridge pipeline ──────────────────
#ifdef __APPLE__
    glFlush();
#endif
    EndDrawing();
#ifdef __APPLE__
    {
        CGLContextObj ctx = CGLGetCurrentContext();
        if (ctx) CGLFlushDrawable(ctx);
    }
    pump_cocoa_main_queue();
#endif
}

// ── Lobby view: centered panel with IP, player roster, and START button ──

void ServerView::draw_lobby_view(
    const std::string& server_ip,
    const std::vector<std::pair<std::string, std::string>>& lobby_names,
    int lobby_count)
{
    using namespace lobby_layout;

    // Initialise ambient particles on first lobby render.
    if (!particles_initted_) {
        gui::init_particles(particles_, 40,
                            static_cast<float>(WINDOW_W),
                            static_cast<float>(WINDOW_H));
        particles_initted_ = true;
    }

    // ── Ambient particle background ─────────────────────────────────
    gui::draw_ambient_particles(particles_,
                                static_cast<float>(WINDOW_W),
                                static_cast<float>(WINDOW_H),
                                GetFrameTime());

    // ── Title ────────────────────────────────────────────────────────
    const char* title = "CTF SERVER";
    int tw = MeasureText(title, TITLE_FONT);
    DrawText(title, (WINDOW_W - tw) / 2,
             static_cast<int>(TITLE_Y), TITLE_FONT, WHITE);

    // ── Server IP with status dot ────────────────────────────────────
    // Green dot if ≥2 players (ready), yellow if waiting.
    bool ready = lobby_count >= constants::min_players;
    Color dot_col = ready ? GREEN : YELLOW;
    constexpr float IP_DOT_X = (WINDOW_W / 2.0f) - 180.0f;
    DrawCircle(static_cast<int>(IP_DOT_X),
               static_cast<int>(IP_Y + IP_FONT / 2.0f - 1.0f),
               5.0f, dot_col);

    std::string ip_label = "Server: " + server_ip;
    DrawText(ip_label.c_str(),
             static_cast<int>(IP_DOT_X + 14.0f),
             static_cast<int>(IP_Y), IP_FONT, gui::TEXT_BRIGHT);

    // ── Status line ─────────────────────────────────────────────────
    std::string status;
    if (lobby_count == 0) {
        status = "Waiting for connections...";
    } else if (lobby_count < constants::min_players) {
        status = "Waiting for players (" +
                 std::to_string(lobby_count) + "/" +
                 std::to_string(constants::min_players) + " minimum)";
    } else {
        status = "Ready to start! (" +
                 std::to_string(lobby_count) + " players connected)";
    }
    int status_tw = MeasureText(status.c_str(), STATUS_FONT);
    DrawText(status.c_str(), (WINDOW_W - status_tw) / 2,
             static_cast<int>(STATUS_Y), STATUS_FONT,
             ready ? GREEN : gui::TEXT_DIM);

    // ── Player table ─────────────────────────────────────────────────
    int visible = std::min(static_cast<int>(lobby_names.size()),
                           MAX_ROWS);
    float tx = table_x();
    float table_h = ROW_H * (static_cast<float>(visible) + 1.0f);

    // Header
    DrawRectangle(static_cast<int>(tx), static_cast<int>(TABLE_Y),
                  static_cast<int>(TABLE_W), static_cast<int>(ROW_H),
                  gui::PANEL_COLOR);
    DrawText("#",   static_cast<int>(tx + 12), static_cast<int>(TABLE_Y + 6),
             16, gui::ACCENT_BLUE);
    DrawText("ID",  static_cast<int>(tx + 60), static_cast<int>(TABLE_Y + 6),
             16, gui::ACCENT_BLUE);
    DrawText("Name", static_cast<int>(tx + 280), static_cast<int>(TABLE_Y + 6),
             16, gui::ACCENT_BLUE);
    DrawRectangleLines(static_cast<int>(tx), static_cast<int>(TABLE_Y),
                       static_cast<int>(TABLE_W), static_cast<int>(ROW_H),
                       Color{80, 80, 90, 200});

    // Player rows
    for (int i = 0; i < visible; ++i) {
        float ry = TABLE_Y + ROW_H * (static_cast<float>(i) + 1.0f);
        const auto& [id, name] = lobby_names[static_cast<std::size_t>(i)];

        // Alternating row colours with a subtle selection tint.
        if (i % 2 == 0) {
            DrawRectangle(static_cast<int>(tx), static_cast<int>(ry),
                          static_cast<int>(TABLE_W), static_cast<int>(ROW_H),
                          gui::ROW_ALT_COLOR);
        }

        // Row #, ID, Name
        std::string num = std::to_string(i + 1);
        DrawText(num.c_str(),  static_cast<int>(tx + 12), static_cast<int>(ry + 6),
                 15, gui::TEXT_DIM);
        DrawText(id.c_str(),   static_cast<int>(tx + 60), static_cast<int>(ry + 6),
                 15, SKYBLUE);
        DrawText(name.c_str(), static_cast<int>(tx + 280), static_cast<int>(ry + 6),
                 15, WHITE);
    }

    // Table border
    if (visible > 0) {
        DrawRectangleLines(static_cast<int>(tx), static_cast<int>(TABLE_Y),
                           static_cast<int>(TABLE_W),
                           static_cast<int>(ROW_H * (static_cast<float>(visible) + 1.0f)),
                           Color{80, 80, 90, 200});
    }

    // Player count summary below the table
    std::string count_str = "Players: " + std::to_string(lobby_count) +
                            " / " + std::to_string(constants::max_players);
    int count_tw = MeasureText(count_str.c_str(), 15);
    DrawText(count_str.c_str(), (WINDOW_W - count_tw) / 2,
             static_cast<int>(TABLE_Y + table_h + 12.0f),
             15, gui::TEXT_DIM);

    // ── START GAME button ────────────────────────────────────────────
    bool can_start = lobby_count >= constants::min_players;
    gui::button("START GAME", btn_x(), BTN_Y, BTN_W, BTN_H, can_start);
}

// ── Game field: circle, flag, players, phase overlay ─────────────────────

void ServerView::draw_game_field(const std::string& phase_text) {
    double scale = static_cast<double>(WINDOW_W) / constants::map_size;

    // ── Draw centre circle ───────────────────────────────────────────
    int cx = static_cast<int>(constants::circle_center_x * scale);
    int cy = static_cast<int>(constants::circle_center_y * scale);
    int radius = static_cast<int>(constants::circle_radius * scale);

    // Translucent fill + outline.
    DrawCircle(cx, cy, static_cast<float>(radius),
               Color{180, 180, 180, 40});
    DrawCircleLines(cx, cy, static_cast<float>(radius),
                    Color{160, 160, 170, 180});

    // ── Draw flag ────────────────────────────────────────────────────
    int fx = static_cast<int>(game_state_.flag.x * scale);
    int fy = static_cast<int>(game_state_.flag.y * scale);

    Color flag_col = game_state_.flag.owner.has_value() ? RED : WHITE;
    DrawTriangle(
        Vector2{static_cast<float>(fx), static_cast<float>(fy - 10)},
        Vector2{static_cast<float>(fx - 8), static_cast<float>(fy + 6)},
        Vector2{static_cast<float>(fx + 8), static_cast<float>(fy + 6)},
        flag_col);

    // ── Draw players ─────────────────────────────────────────────────
    for (const auto& ps : game_state_.players) {
        int px = static_cast<int>(ps.x * scale);
        int py = static_cast<int>(ps.y * scale);
        int pr = static_cast<int>(constants::player_radius * scale);

        Color col = BLUE;
        if (game_state_.flag.owner.has_value() &&
            game_state_.flag.owner.value() == ps.id) {
            col = RED;
        }

        // Player body.
        DrawCircle(px, py, static_cast<float>(pr), col);

        // Name label.
        int text_w = MeasureText(ps.name.c_str(), 10);
        DrawText(ps.name.c_str(),
                 px - text_w / 2, py - pr - 14,
                 10, WHITE);
    }

    // ── Phase overlay ─────────────────────────────────────────────────
    int text_w = MeasureText(phase_text.c_str(), 20);
    DrawText(phase_text.c_str(),
             (WINDOW_W - text_w) / 2, 10,
             20, YELLOW);
}

}  // namespace ctf::server
