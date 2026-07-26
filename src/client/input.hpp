#pragma once

namespace ctf::client {

// Samples keyboard input each frame and exposes edge-detected events.
//
// Direction is read from WASD and arrow keys and mapped to
// dir_x / dir_y ∈ {-1, 0, 1}.  A direction change is reported once
// (via take_dir_changed) so the caller can send a single `input`
// message only when the value actually changes.
//
// The interact action (E) is edge-detected: take_interact() returns
// true at most once per physical key press.
class InputSampler {
public:
    // Read the keyboard and update internal state.  Call once per frame.
    void sample();

    // Current direction components, each in {-1, 0, 1}.
    int dir_x() const noexcept { return dir_x_; }
    int dir_y() const noexcept { return dir_y_; }

    // Returns true once if the direction changed since the last call,
    // then resets the flag.  Use this to decide whether to send `input`.
    auto take_dir_changed() noexcept -> bool;

    // Returns true once if E was pressed since the last call,
    // then resets the flag.  Use this to decide whether to send `interact`.
    auto take_interact() noexcept -> bool;

    // Reset to a neutral state (e.g. when entering/leaving a round).
    void reset() noexcept;

private:
    int  dir_x_{0};
    int  dir_y_{0};
    bool dir_changed_{false};
    bool interact_pressed_{false};
};

}  // namespace ctf::client