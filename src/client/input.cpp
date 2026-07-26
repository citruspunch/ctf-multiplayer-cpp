#include "input.hpp"

#include <raylib.h>

namespace ctf::client {

void InputSampler::sample() {
    // ── Direction from WASD / arrows ────────────────────────────────
    int dx = 0;
    int dy = 0;

    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  dx -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) dx += 1;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))   dy -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  dy += 1;

    // The protocol forbids diagonal magnitudes > 1; the server normalises
    // diagonals (÷√2), so sending raw (-1,-1) etc. is correct.
    if (dx != dir_x_ || dy != dir_y_) {
        dir_x_ = dx;
        dir_y_ = dy;
        dir_changed_ = true;
    }

    // ── Interact (E) — edge detected ───────────────────────────────
    if (IsKeyPressed(KEY_E)) {
        interact_pressed_ = true;
    }
}

auto InputSampler::take_dir_changed() noexcept -> bool {
    const bool c = dir_changed_;
    dir_changed_ = false;
    return c;
}

auto InputSampler::take_interact() noexcept -> bool {
    const bool p = interact_pressed_;
    interact_pressed_ = false;
    return p;
}

void InputSampler::reset() noexcept {
    dir_x_ = 0;
    dir_y_ = 0;
    dir_changed_ = false;
    interact_pressed_ = false;
}

}  // namespace ctf::client