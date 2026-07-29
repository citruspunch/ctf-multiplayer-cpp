#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace ctf::gui {

// ── Colours ────────────────────────────────────────────────────────────

inline constexpr Color BG_COLOR{20, 22, 28, 255};
inline constexpr Color PANEL_COLOR{10, 12, 18, 220};
inline constexpr Color BTN_COLOR{45, 50, 60, 255};
inline constexpr Color BTN_HOVER_COLOR{65, 70, 85, 255};
inline constexpr Color BTN_GREEN{40, 160, 60, 255};
inline constexpr Color BTN_GREEN_HOVER{55, 190, 80, 255};
inline constexpr Color BTN_DISABLED{50, 50, 50, 255};
inline constexpr Color SELECT_COLOR{30, 60, 100, 200};
inline constexpr Color ROW_ALT_COLOR{25, 28, 35, 200};
inline constexpr Color ACCENT_BLUE{70, 130, 220, 255};
inline constexpr Color TEXT_DIM{140, 140, 150, 255};
inline constexpr Color TEXT_BRIGHT{220, 220, 230, 255};

// ── GUI Button ─────────────────────────────────────────────────────────

// Draw a styled rounded-rectangle button.
// Returns true once when the button is clicked with the mouse.
//
// label       – text displayed (UTF-8)
// x, y, w, h  – rectangle bounds
// enabled     – when false, button is greyed out and not clickable
//
// When the button is hovered and enabled, the hover colour variant is used
// (auto-darkened if not provided).
auto button(const char* label, float x, float y, float w, float h,
            bool enabled,
            Color enabled_color = BTN_GREEN,
            Color disabled_color = BTN_DISABLED) -> bool;

// ── Ambient particles (decorative) ────────────────────────────────────

struct Particle {
    float x, y;
    float speed;
    float alpha;
    float size;
};

// Initialise `count` particles with random positions, speeds, and sizes
// within the given viewport dimensions.
void init_particles(std::vector<Particle>& particles, int count,
                    float view_w, float view_h);

// Update and draw particles — animated floating circles that drift upward
// and fade out, recycling at the bottom of the viewport.
// Call once per frame during lobby rendering.
void draw_ambient_particles(std::vector<Particle>& particles,
                            float view_w, float view_h, float dt);

}  // namespace ctf::gui
