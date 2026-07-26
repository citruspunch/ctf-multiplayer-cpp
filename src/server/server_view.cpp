#include "server_view.hpp"
#include "constants.hpp"

#include <raylib.h>

#include <string>

namespace ctf::server {

static constexpr int WINDOW_W = 800;
static constexpr int WINDOW_H = 800;

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
        InitWindow(WINDOW_W, WINDOW_H, "CTF Server — Observer");
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
}

}  // namespace ctf::server
