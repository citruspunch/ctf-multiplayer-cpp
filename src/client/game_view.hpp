#pragma once

#include "messages.hpp"

#include <map>
#include <string>

namespace ctf::client {

// Renders a game `state` to the Raylib window.  Pure drawing — no state,
// no input.  Must be called inside BeginDrawing()/EndDrawing().
//
//   state       — the latest server state (flag + players).
//   self_id     — this client's player id (drawn green).
//   names       — id → display name map (from lobby rosters).  IDs absent
//                 from the map are drawn using their id.
//   elapsed_sec — elapsed match time in seconds (for HUD display).
//   dir_x, dir_y —  player's current direction (-1, 0, 1) for direction
//                  indicator triangle (only drawn for self).
void draw_game_view(const ctf::State& state, const std::string& self_id,
                    const std::map<std::string, std::string>& names,
                    double elapsed_sec = 0.0,
                    int dir_x = 0, int dir_y = 0);

}  // namespace ctf::client