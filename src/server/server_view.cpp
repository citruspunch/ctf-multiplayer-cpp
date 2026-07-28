#include "server_view.hpp"
#include "server.hpp"
#include "constants.hpp"

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

    // Build lobby player list (for the right-hand panel in the view).
    std::vector<std::pair<std::string, std::string>> names;
    for (const auto& [fd, s] : sessions_) {
        if (s->joined)
            names.emplace_back(s->player_id, s->player_name);
    }

    view_->render(phase_text, names, session_count());
}

auto Server::should_close_observer() -> bool {
    if (!view_) return false;
    if (view_->should_close()) return true;
#ifdef __APPLE__
    // macOS Quit (Cmd+Q) is delivered to the NSMenuItem installed in
    // app_activation.mm.  The action sets an atomic flag; check it
    // here so the server loop can break cleanly and run its
    // destructors (including CloseWindow) instead of exit(0).
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
    // Edge-detect SPACE keypress — returns true exactly once per press.
    bool cur = IsKeyDown(KEY_SPACE);
    bool pressed = cur && !last_space_state_;
    last_space_state_ = cur;
    if (pressed) {
        start_pressed_ = true;
        return true;
    }
    return false;
}

void ServerView::render(const std::string& phase_text,
                        const std::vector<std::pair<std::string, std::string>>& lobby_names,
                        int lobby_count) {
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
        SetExitKey(0);  // ESC handled by the caller (Server::run
                        // checks WindowShouldClose()).
        initialised_ = true;
    }

    BeginDrawing();
    ClearBackground(BLACK);

    double scale = static_cast<double>(WINDOW_W) / constants::map_size;

    // ── Draw centre circle ───────────────────────────────────────────
    int cx = static_cast<int>(constants::circle_center_x * scale);
    int cy = static_cast<int>(constants::circle_center_y * scale);
    int radius = static_cast<int>(constants::circle_radius * scale);
    DrawCircle(cx, cy, static_cast<float>(radius), LIGHTGRAY);

    // ── Draw flag ────────────────────────────────────────────────────
    int fx = static_cast<int>(game_state_.flag.x * scale);
    int fy = static_cast<int>(game_state_.flag.y * scale);

    if (game_state_.flag.owner.has_value()) {
        DrawTriangle(
            Vector2{static_cast<float>(fx), static_cast<float>(fy - 10)},
            Vector2{static_cast<float>(fx - 8), static_cast<float>(fy + 6)},
            Vector2{static_cast<float>(fx + 8), static_cast<float>(fy + 6)},
            RED);
    } else {
        DrawTriangle(
            Vector2{static_cast<float>(fx), static_cast<float>(fy - 10)},
            Vector2{static_cast<float>(fx - 8), static_cast<float>(fy + 6)},
            Vector2{static_cast<float>(fx + 8), static_cast<float>(fy + 6)},
            WHITE);
    }

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

        DrawCircle(px, py, static_cast<float>(pr), col);

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

    // ── Lobby panel: connected players (only during LOBBY) ───────────
    if (!lobby_names.empty()) {
        // Semi-transparent dark panel on the right side.
        constexpr float PX = 580, PY = 40, PW = 200, PH = 280;
        DrawRectangle(PX, PY, PW, PH, Color{10, 12, 18, 220});
        DrawRectangleLines(PX, PY, PW, PH, GRAY);
        DrawText("LOBBY", PX + 8, PY + 6, 16, YELLOW);
        std::string cnt = std::to_string(lobby_count) + " players";
        DrawText(cnt.c_str(), PX + 8, PY + 26, 13, LIGHTGRAY);

        // Player table.
        constexpr float RY0 = PY + 50;
        constexpr float RH  = 16;
        int row = 0;
        for (const auto& [id, name] : lobby_names) {
            if (row >= 12) break;  // max 12 visible rows
            float ry = RY0 + row * RH;
            DrawText(id.c_str(), PX + 8, ry, 12, SKYBLUE);
            DrawText(name.c_str(), PX + 70, ry, 12, WHITE);
            ++row;
        }

        // Hint: press SPACE to start.
        if (lobby_count >= 2) {
            DrawText("Press SPACE", PX + 8, PY + PH - 24, 13, GREEN);
            DrawText("to start game", PX + 8, PY + PH - 12, 13, GREEN);
        } else {
            DrawText("Waiting for", PX + 8, PY + PH - 24, 13, GRAY);
            DrawText("2+ players...", PX + 8, PY + PH - 12, 13, GRAY);
        }
    }

    // ── Apple Silicon: GL → Metal bridge pipeline ──────────────────
    // Same strategy as the client (see client.cpp for full rationale):
    //   glFlush()  — commit immediate GL commands (ClearBackground),
    //                do NOT present.
    //   EndDrawing — flush batched draws + SwapBuffers (presents).
    //   CGLFlushDrawable — belt-and-suspenders present.
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

}  // namespace ctf::server
