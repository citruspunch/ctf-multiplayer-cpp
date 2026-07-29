#include "game_view.hpp"

#include "constants.hpp"

#include <raylib.h>

#include <cmath>
#include <map>
#include <string>

namespace ctf::client {

namespace {

constexpr int WINDOW_W = 800;
constexpr int WINDOW_H = 800;

// Grid spacing in map units.
constexpr double GRID_STEP = 50.0;

// Look up a display name, falling back to the id.
auto name_of(const std::string& id,
             const std::map<std::string, std::string>& names) -> std::string {
    if (auto it = names.find(id); it != names.end()) {
        return it->second;
    }
    return id;
}

void draw_background_grid(double scale) {
    // Subtle grid lines.
    const Color grid_col{40, 42, 48, 80};
    const int map = constants::map_size;
    for (double g = 0.0; g <= static_cast<double>(map); g += GRID_STEP) {
        int pos = static_cast<int>(g * scale);
        // Vertical
        DrawLine(pos, 0, pos, static_cast<int>(static_cast<double>(map) * scale),
                 grid_col);
        // Horizontal
        DrawLine(0, pos,
                 static_cast<int>(static_cast<double>(map) * scale), pos,
                 grid_col);
    }

    // Map border
    DrawRectangleLines(0, 0,
                       static_cast<int>(static_cast<double>(map) * scale),
                       static_cast<int>(static_cast<double>(map) * scale),
                       Color{100, 100, 110, 160});
}

void draw_centre_circle(double scale) {
    const int cx = static_cast<int>(constants::circle_center_x * scale);
    const int cy = static_cast<int>(constants::circle_center_y * scale);
    const int cr = static_cast<int>(constants::circle_radius * scale);

    // Subtle radial gradient (multiple concentric rings).
    for (int r = cr; r > 0; r -= 8) {
        float t = static_cast<float>(r) / static_cast<float>(cr);
        unsigned char a = static_cast<unsigned char>((1.0f - t) * 30.0f);
        DrawCircle(cx, cy, static_cast<float>(r),
                   Color{180, 180, 190, a});
    }

    // Translucent fill.
    DrawCircle(cx, cy, static_cast<float>(cr),
               Color{180, 180, 190, 30});

    // Bold outline.
    DrawCircleLines(cx, cy, static_cast<float>(cr),
                    Color{140, 150, 170, 200});

    // "BASE" label centred.
    const char* base_text = "BASE";
    int bt = MeasureText(base_text, 20);
    DrawText(base_text, cx - bt / 2, cy - 10, 20,
             Color{140, 150, 170, 120});
}

void draw_flag(const ctf::State& state, double scale) {
    const bool  owned = state.flag.owner.has_value();
    const int   fx = static_cast<int>(state.flag.x * scale);
    const int   fy = static_cast<int>(state.flag.y * scale);

    // Bobbing animation — only when carried by a player.
    double bob = owned ? (std::sin(GetTime() * 3.0) * 3.0) : 0.0;
    int fy_bob = fy + static_cast<int>(bob);

    // ── Pulsing glow ring behind the flag ────────────────────────────
    float glow_r = 14.0f + std::sin(static_cast<float>(GetTime()) * 2.5f) * 3.0f;
    Color glow_col = owned ? Color{255, 60, 60, 100} : Color{255, 200, 50, 120};
    DrawCircleLines(fx, fy_bob, glow_r, glow_col);
    DrawCircle(fx, fy_bob, glow_r - 2.0f,
               Color{glow_col.r, glow_col.g, glow_col.b,
                     static_cast<unsigned char>(glow_col.a / 2)});

    if (!owned) {
        // Free flag — orange/gold for high contrast against gray circle.
        // Flag pole (visible against all backgrounds).
        DrawLine(fx - 1, fy - 14, fx - 1, fy + 12,
                 Color{80, 60, 20, 180});
        DrawLine(fx, fy - 14, fx, fy + 12,
                 Color{200, 160, 50, 255});
        DrawLine(fx + 1, fy - 14, fx + 1, fy + 12,
                 Color{80, 60, 20, 180});

        // Flag fabric: gold/orange waving triangles.
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
    } else {
        // Carried flag — red with bobbing animation.
        DrawTriangle(
            Vector2{static_cast<float>(fx), static_cast<float>(fy_bob - 10)},
            Vector2{static_cast<float>(fx + 8), static_cast<float>(fy_bob + 2)},
            Vector2{static_cast<float>(fx - 8), static_cast<float>(fy_bob + 2)},
            Color{255, 80, 80, 255});

        // Red glow when carried.
        DrawCircleLines(fx, fy_bob, 16.0f, Color{255, 40, 40, 60});
    }

    // "FLAG" label (orange when free, red when carried).
    const char* flag_label = "FLAG";
    int fl_font = owned ? 9 : 10;
    DrawText(flag_label, fx + 20, (owned ? fy_bob : fy) - fl_font / 2,
             fl_font, owned ? Color{255, 150, 150, 220}
                            : Color{255, 200, 50, 240});
}

void draw_player(const ctf::Player& p, const std::string& self_id,
                 const std::map<std::string, std::string>& names,
                 const ctf::Flag& flag, double scale,
                 int dir_x, int dir_y) {
    const int px = static_cast<int>(p.x * scale);
    const int py = static_cast<int>(p.y * scale);
    const int pr = static_cast<int>(constants::player_radius * scale);
    const bool is_self = (p.id == self_id);
    const bool is_carrier = flag.owner.has_value() && flag.owner.value() == p.id;

    // Colour.
    Color fill;
    if (is_carrier) {
        fill = Color{220, 60, 60, 255};       // carrier — bright red
    } else if (is_self) {
        fill = Color{60, 200, 90, 255};        // self — bright green
    } else {
        fill = Color{70, 120, 220, 255};       // others — blue
    }

    // Outer ring (subtle).
    DrawCircleLines(px, py, static_cast<float>(pr + 3),
                    Color{fill.r, fill.g, fill.b, 80});

    // Carrier glow (pulsating).
    if (is_carrier) {
        float glow_r = static_cast<float>(pr) + 6.0f +
                       std::sin(static_cast<float>(GetTime()) * 4.0f) * 3.0f;
        DrawCircleLines(px, py, glow_r,
                        Color{220, 60, 60, 120});
    }

    // Player body.
    DrawCircle(px, py, static_cast<float>(pr), fill);

    // Direction indicator (self only, when moving).
    if (is_self && (dir_x != 0 || dir_y != 0)) {
        float tip_x = static_cast<float>(px) +
                      static_cast<float>(dir_x) * static_cast<float>(pr + 8);
        float tip_y = static_cast<float>(py) +
                      static_cast<float>(dir_y) * static_cast<float>(pr + 8);
        float back_x = static_cast<float>(px) +
                       static_cast<float>(dir_x) * static_cast<float>(pr + 2);
        float back_y = static_cast<float>(py) +
                       static_cast<float>(dir_y) * static_cast<float>(pr + 2);

        // Perpendicular for the triangle base.
        float perp_x = -static_cast<float>(dir_y);
        float perp_y = static_cast<float>(dir_x);

        DrawTriangle(
            Vector2{tip_x, tip_y},
            Vector2{back_x + perp_x * 4.0f, back_y + perp_y * 4.0f},
            Vector2{back_x - perp_x * 4.0f, back_y - perp_y * 4.0f},
            Color{255, 255, 255, 200});
    }

    // Name label with semi-transparent background.
    const std::string label = name_of(p.id, names);
    const int ts = 10;
    const int tw = MeasureText(label.c_str(), ts);
    const float lx = static_cast<float>(px - tw / 2 - 2);
    const float ly = static_cast<float>(py - pr - 12);
    const float lw = static_cast<float>(tw + 4);
    const float lh = static_cast<float>(ts + 2);
    DrawRectangle(static_cast<int>(lx), static_cast<int>(ly),
                  static_cast<int>(lw), static_cast<int>(lh),
                  Color{0, 0, 0, 140});
    DrawText(label.c_str(), px - tw / 2, py - pr - 12, ts, WHITE);
}

void draw_hud(const ctf::State& state,
              const std::map<std::string, std::string>& names,
              double elapsed_sec) {
    // Top bar background.
    DrawRectangle(0, 0, WINDOW_W, 30, Color{0, 0, 0, 170});

    // FPS counter using per-frame timing (more reliable than GetFPS on macOS).
    float dt = GetFrameTime();
    int fps = (dt > 0.0f) ? static_cast<int>(1.0f / dt + 0.5f) : 0;
    std::string fps_str = std::to_string(fps) + " FPS";
    DrawText(fps_str.c_str(), 6, 7, 14, LIME);

    // Elapsed time.
    int mins = static_cast<int>(elapsed_sec) / 60;
    int secs = static_cast<int>(elapsed_sec) % 60;
    std::string time_str = std::to_string(mins) + ":" +
                           (secs < 10 ? "0" : "") + std::to_string(secs);
    DrawText(time_str.c_str(), 110, 7, 16, WHITE);

    // Players alive.
    std::string count_str = "Players: " + std::to_string(state.players.size());
    DrawText(count_str.c_str(), 210, 7, 16, WHITE);

    // Flag status.
    bool owned = state.flag.owner.has_value();
    std::string flag_str;
    if (owned) {
        flag_str = "Flag: " + name_of(state.flag.owner.value(), names);
    } else {
        flag_str = "Flag: free";
    }
    DrawText(flag_str.c_str(), 390, 7, 16, owned ? RED : Color{180, 180, 180, 255});

    // Bottom bar — control hints.
    DrawRectangle(0, WINDOW_H - 24, WINDOW_W, 24, Color{0, 0, 0, 160});
    DrawText("WASD/Arrows: Move   |   E: Interact   |   ESC: Quit",
             8, WINDOW_H - 20, 12, Color{120, 130, 140, 200});
}

}  // namespace

void draw_game_view(const ctf::State& state, const std::string& self_id,
                    const std::map<std::string, std::string>& names,
                    double elapsed_sec, int dir_x, int dir_y) {
    const double scale =
        static_cast<double>(WINDOW_W) / constants::map_size;

    // ── Background grid ─────────────────────────────────────────────
    draw_background_grid(scale);

    // ── Centre circle ───────────────────────────────────────────────
    draw_centre_circle(scale);

    // ── Flag ─────────────────────────────────────────────────────────
    draw_flag(state, scale);

    // ── Players ──────────────────────────────────────────────────────
    for (const auto& p : state.players) {
        draw_player(p, self_id, names, state.flag, scale, dir_x, dir_y);
    }

    // ── HUD ──────────────────────────────────────────────────────────
    draw_hud(state, names, elapsed_sec);
}

}  // namespace ctf::client