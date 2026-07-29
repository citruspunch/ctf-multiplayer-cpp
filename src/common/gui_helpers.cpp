#include "gui_helpers.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace ctf::gui {

auto button(const char* label, float x, float y, float w, float h,
            bool enabled, Color enabled_color, Color disabled_color) -> bool {
    Rectangle rect{x, y, w, h};
    const bool hover = CheckCollisionPointRec(GetMousePosition(), rect);

    Color fill;
    if (!enabled) {
        fill = disabled_color;
    } else if (hover) {
        // Slightly brighten the enabled colour.
        fill = Color{
            static_cast<unsigned char>(std::min(255, enabled_color.r + 20)),
            static_cast<unsigned char>(std::min(255, enabled_color.g + 20)),
            static_cast<unsigned char>(std::min(255, enabled_color.b + 20)),
            enabled_color.a
        };
    } else {
        fill = enabled_color;
    }

    constexpr float roundness = 0.25f;
    DrawRectangleRounded(rect, roundness, 8, fill);
    DrawRectangleRoundedLines(rect, roundness, 8,
                              hover && enabled ? WHITE : Color{100, 100, 100, 150});

    const int font_size = static_cast<int>(h * 0.45f);
    const int tw = MeasureText(label, font_size);
    DrawText(label,
             static_cast<int>(x + (w - static_cast<float>(tw)) / 2.0f),
             static_cast<int>(y + (h - static_cast<float>(font_size)) / 2.0f),
             font_size,
             enabled ? WHITE : Color{120, 120, 120, 255});

    return enabled && hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

void init_particles(std::vector<Particle>& particles, int count,
                    float view_w, float view_h) {
    particles.resize(static_cast<std::size_t>(count));
    for (auto& p : particles) {
        p.x = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * view_w;
        p.y = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * view_h;
        p.speed = 10.0f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 30.0f;
        p.alpha = 0.05f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 0.15f;
        p.size = 1.5f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 3.0f;
    }
}

void draw_ambient_particles(std::vector<Particle>& particles,
                            float view_w, float view_h, float dt) {
    for (auto& p : particles) {
        // Drift upward.
        p.y -= p.speed * dt;
        // Slight horizontal drift.
        p.x += std::sin(p.y * 0.01f) * dt * 5.0f;

        // Reset when off-screen top.
        if (p.y < -5.0f) {
            p.y = view_h + 5.0f;
            p.x = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * view_w;
        }

        DrawCircle(static_cast<int>(p.x), static_cast<int>(p.y),
                   p.size,
                   Color{255, 255, 255,
                         static_cast<unsigned char>(p.alpha * 255.0f)});
    }
}

}  // namespace ctf::gui
