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

#include <cmath>
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

namespace {

void draw_server_grid(double scale) {
    const Color grid_col{40, 42, 48, 80};
    const int map = constants::map_size;
    constexpr double GRID_STEP = 50.0;
    for (double g = 0.0; g <= static_cast<double>(map); g += GRID_STEP) {
        int pos = static_cast<int>(g * scale);
        DrawLine(pos, 0, pos, static_cast<int>(static_cast<double>(map) * scale),
                 grid_col);
        DrawLine(0, pos,
                 static_cast<int>(static_cast<double>(map) * scale), pos,
                 grid_col);
    }
    DrawRectangleLines(0, 0,
                       static_cast<int>(static_cast<double>(map) * scale),
                       static_cast<int>(static_cast<double>(map) * scale),
                       Color{100, 100, 110, 160});
}

void draw_server_circle(double scale) {
    int cx = static_cast<int>(constants::circle_center_x * scale);
    int cy = static_cast<int>(constants::circle_center_y * scale);
    int cr = static_cast<int>(constants::circle_radius * scale);

    for (int r = cr; r > 0; r -= 8) {
        float t = static_cast<float>(r) / static_cast<float>(cr);
        unsigned char a = static_cast<unsigned char>((1.0f - t) * 30.0f);
        DrawCircle(cx, cy, static_cast<float>(r),
                   Color{180, 180, 190, a});
    }
    DrawCircle(cx, cy, static_cast<float>(cr),
               Color{180, 180, 190, 30});
    DrawCircleLines(cx, cy, static_cast<float>(cr),
                    Color{140, 150, 170, 200});

    const char* base = "BASE";
    int bt = MeasureText(base, 20);
    DrawText(base, cx - bt / 2, cy - 10, 20,
             Color{140, 150, 170, 120});
}

void draw_server_flag(const ctf::game::FlagState& flag, double scale) {
    int fx = static_cast<int>(flag.x * scale);
    int fy = static_cast<int>(flag.y * scale);
    bool owned = flag.owner.has_value();

    // Bobbing animation — only when carried by a player.
    double bob = owned ? (std::sin(GetTime() * 3.0) * 3.0) : 0.0;
    int fy_bob = fy + static_cast<int>(bob);

    // Pulsing glow ring behind the flag.
    float glow_r = 14.0f + std::sin(static_cast<float>(GetTime()) * 2.5f) * 3.0f;
    Color glow_col = owned ? Color{255, 60, 60, 100} : Color{255, 200, 50, 120};
    DrawCircleLines(fx, fy_bob, glow_r, glow_col);
    DrawCircle(fx, fy_bob, glow_r - 2.0f,
               Color{glow_col.r, glow_col.g, glow_col.b,
                     static_cast<unsigned char>(glow_col.a / 2)});

    if (owned) {
        DrawTriangle(
            Vector2{static_cast<float>(fx), static_cast<float>(fy_bob - 10)},
            Vector2{static_cast<float>(fx + 8), static_cast<float>(fy_bob + 2)},
            Vector2{static_cast<float>(fx - 8), static_cast<float>(fy_bob + 2)},
            Color{255, 80, 80, 255});
        DrawCircleLines(fx, fy_bob, 16.0f, Color{255, 40, 40, 60});
    } else {
        // Free flag — orange/gold for high contrast.
        DrawLine(fx - 1, fy - 14, fx - 1, fy + 12,
                 Color{80, 60, 20, 180});
        DrawLine(fx, fy - 14, fx, fy + 12,
                 Color{200, 160, 50, 255});
        DrawLine(fx + 1, fy - 14, fx + 1, fy + 12,
                 Color{80, 60, 20, 180});
        DrawTriangle(
            Vector2{static_cast<float>(fx), static_cast<float>(fy - 14)},
            Vector2{static_cast<float>(fx + 16), static_cast<float>(fy - 2)},
            Vector2{static_cast<float>(fx), static_cast<float>(fy + 6)},
            Color{255, 200, 50, 255});
        DrawTriangle(
            Vector2{static_cast<float>(fx), static_cast<float>(fy + 6)},
            Vector2{static_cast<float>(fx + 12), static_cast<float>(fy + 14)},
            Vector2{static_cast<float>(fx), static_cast<float>(fy + 14)},
            Color{200, 150, 30, 220});
    }

    const char* flag_label = "FLAG";
    int fl_font = owned ? 9 : 10;
    int label_y = owned ? fy_bob : fy;
    DrawText(flag_label, fx + 20, label_y - fl_font / 2,
             fl_font, owned ? Color{255, 150, 150, 220}
                            : Color{255, 200, 50, 240});
}

void draw_server_player(const ctf::game::PlayerState& ps,
                        const ctf::game::FlagState& flag, double scale) {
    int px = static_cast<int>(ps.x * scale);
    int py = static_cast<int>(ps.y * scale);
    int pr = static_cast<int>(constants::player_radius * scale);
    bool is_carrier = flag.owner.has_value() && flag.owner.value() == ps.id;

    Color fill = is_carrier ? Color{220, 60, 60, 255}
                            : Color{70, 120, 220, 255};

    DrawCircleLines(px, py, static_cast<float>(pr + 3),
                    Color{fill.r, fill.g, fill.b, 80});

    if (is_carrier) {
        float glow_r = static_cast<float>(pr) + 6.0f +
                       std::sin(static_cast<float>(GetTime()) * 4.0f) * 3.0f;
        DrawCircleLines(px, py, glow_r,
                        Color{220, 60, 60, 120});
    }

    DrawCircle(px, py, static_cast<float>(pr), fill);

    int text_w = MeasureText(ps.name.c_str(), 10);
    float lx = static_cast<float>(px - text_w / 2 - 2);
    float ly = static_cast<float>(py - pr - 12);
    DrawRectangle(static_cast<int>(lx), static_cast<int>(ly),
                  static_cast<int>(text_w + 4), 12,
                  Color{0, 0, 0, 140});
    DrawText(ps.name.c_str(), px - text_w / 2, py - pr - 12, 10, WHITE);
}

}  // namespace

void ServerView::draw_game_field(const std::string& phase_text) {
    double scale = static_cast<double>(WINDOW_W) / constants::map_size;

    // ── Background grid ──────────────────────────────────────────────
    draw_server_grid(scale);

    // ── Centre circle ────────────────────────────────────────────────
    draw_server_circle(scale);

    // ── Flag ─────────────────────────────────────────────────────────
    draw_server_flag(game_state_.flag, scale);

    // ── Players ──────────────────────────────────────────────────────
    for (const auto& ps : game_state_.players) {
        draw_server_player(ps, game_state_.flag, scale);
    }

    // ── Phase overlay ────────────────────────────────────────────────
    DrawRectangle(0, 0, WINDOW_W, 30, Color{0, 0, 0, 170});
    int text_w = MeasureText(phase_text.c_str(), 20);
    DrawText(phase_text.c_str(),
             (WINDOW_W - text_w) / 2, 5,
             20, YELLOW);
    // FPS counter using per-frame timing.
    float dt = GetFrameTime();
    int fps = (dt > 0.0f) ? static_cast<int>(1.0f / dt + 0.5f) : 0;
    std::string fps_str = std::to_string(fps) + " FPS";
    DrawText(fps_str.c_str(), 6, 7, 14, LIME);

    // Player count overlay.
    std::string count_str = "Players: " + std::to_string(game_state_.players.size());
    DrawText(count_str.c_str(), WINDOW_W - 150, 7, 16, WHITE);
}

}  // namespace ctf::server
