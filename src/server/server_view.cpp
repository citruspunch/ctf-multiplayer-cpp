#include "server_view.hpp"
#include "server.hpp"
#include "constants.hpp"

#ifdef __APPLE__
#include "app_activation.hpp"
#endif

#include <raylib.h>

#include <chrono>
#include <string>
#include <thread>

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
    view_->render(phase_text);
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

void ServerView::render(const std::string& phase_text) {
    if (!initialised_) {
#ifdef __APPLE__
        // Step 1: promote process to foreground *before* InitWindow.
        activate_macos_app();
#endif
#ifdef __APPLE__
        // macOS / Apple Silicon + Raylib 6.0 has a chronic issue where
        // FLAG_WINDOW_TOPMOST (and the VSync+TOPMOST combo) leaves the
        // GL backbuffer in a state where it is never presented to the
        // screen — the window shows a solid black background even
        // though the render loop runs and OpenGL commands execute.
        // Drop TOPMOST entirely on macOS.  We also drop VSync and rely
        // on SetTargetFPS(60) below for framerate limiting; the main
        // thread stays responsive via Raylib's internal wait inside
        // the server's poll loop.
        SetConfigFlags(0);
#else
        SetConfigFlags(FLAG_VSYNC_HINT);
#endif
        InitWindow(WINDOW_W, WINDOW_H, "CTF Server — Observer");
#ifdef __APPLE__
        // Step 2: bring window to front *after* InitWindow.
        activate_macos_app_after_init();

        // Step 3: install a real menu bar with Quit (Cmd+Q).  Without
        // a menu bar, Cmd+Q has nothing to dispatch to in the
        // server's tight glfwPollEvents loop.
        install_macos_menu();
        clear_macos_quit_request();

        // Step 4: warmup frame.  On Apple Silicon, the first
        // EndDrawing can paint into an unrealised backbuffer that the
        // windowserver never presents.  Triggering a full Begin/End
        // cycle here, then pumping the Cocoa run loop, forces the GL
        // context to fully initialise before the main loop starts.
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();
        pump_cocoa_main_queue();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pump_cocoa_main_queue();
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
        // Red triangle pointing up — owned by someone.
        DrawTriangle(
            Vector2{static_cast<float>(fx), static_cast<float>(fy - 10)},
            Vector2{static_cast<float>(fx - 8), static_cast<float>(fy + 6)},
            Vector2{static_cast<float>(fx + 8), static_cast<float>(fy + 6)},
            RED);
    } else {
        // White triangle — free.
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

        // Choose colour: red if carrier, blue otherwise.
        Color col = BLUE;
        if (game_state_.flag.owner.has_value() &&
            game_state_.flag.owner.value() == ps.id) {
            col = RED;
        }

        DrawCircle(px, py, static_cast<float>(pr), col);

        // Draw player name above the circle.
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

    EndDrawing();
#ifdef __APPLE__
    // Drain Cocoa's main run loop AFTER the frame is presented so the
    // GL swap is not disturbed.  Must come after EndDrawing so the
    // GL backbuffer swap is committed before the main run loop runs.
    pump_cocoa_main_queue();
#endif
}

}  // namespace ctf::server
