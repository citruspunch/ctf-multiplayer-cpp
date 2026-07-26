#include "game_view.hpp"

#include "constants.hpp"

#include <raylib.h>

#include <string>

namespace ctf::client {

namespace {

constexpr int WINDOW_W = 800;
constexpr int WINDOW_H = 800;

// Look up a display name, falling back to the id.
auto name_of(const std::string& id,
             const std::map<std::string, std::string>& names) -> std::string {
    if (auto it = names.find(id); it != names.end()) {
        return it->second;
    }
    return id;
}

}  // namespace

void draw_game_view(const ctf::State& state, const std::string& self_id,
                    const std::map<std::string, std::string>& names) {
    const double scale =
        static_cast<double>(WINDOW_W) / constants::map_size;

    // ── Centre circle ──────────────────────────────────────────────
    const int cx = static_cast<int>(constants::circle_center_x * scale);
    const int cy = static_cast<int>(constants::circle_center_y * scale);
    const int cr = static_cast<int>(constants::circle_radius * scale);
    DrawCircle(cx, cy, static_cast<float>(cr), LIGHTGRAY);

    // ── Flag ────────────────────────────────────────────────────────
    const bool  owned = state.flag.owner.has_value();
    const int   fx = static_cast<int>(state.flag.x * scale);
    const int   fy = static_cast<int>(state.flag.y * scale);
    const Color flag_col = owned ? RED : WHITE;
    DrawTriangle(
        Vector2{static_cast<float>(fx), static_cast<float>(fy - 10)},
        Vector2{static_cast<float>(fx - 8), static_cast<float>(fy + 6)},
        Vector2{static_cast<float>(fx + 8), static_cast<float>(fy + 6)},
        flag_col);

    // ── Players ─────────────────────────────────────────────────────
    const int pr = static_cast<int>(constants::player_radius * scale);
    for (const auto& p : state.players) {
        const int px = static_cast<int>(p.x * scale);
        const int py = static_cast<int>(p.y * scale);

        Color col = BLUE;  // others
        if (owned && state.flag.owner.value() == p.id) {
            col = RED;     // carrier
        } else if (p.id == self_id) {
            col = GREEN;   // self
        }

        DrawCircle(px, py, static_cast<float>(pr), col);

        // Name label above the circle.
        const std::string label = name_of(p.id, names);
        const int tw = MeasureText(label.c_str(), 10);
        DrawText(label.c_str(), px - tw / 2, py - pr - 14, 10, WHITE);
    }

    // ── Overlay ─────────────────────────────────────────────────────
    DrawRectangle(0, 0, WINDOW_W, 28, Color{0, 0, 0, 160});
    DrawFPS(8, 6);

    const std::string count =
        "Players: " + std::to_string(state.players.size());
    DrawText(count.c_str(), 90, 6, 16, WHITE);

    std::string flag_line;
    if (owned) {
        flag_line = "Flag: " + name_of(state.flag.owner.value(), names);
    } else {
        flag_line = "Flag: free";
    }
    DrawText(flag_line.c_str(), 250, 6, 16,
             owned ? RED : LIGHTGRAY);
}

}  // namespace ctf::client