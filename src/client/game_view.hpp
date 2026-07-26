#pragma once

#include "messages.hpp"

#include <map>
#include <string>

namespace ctf::client {

// Renders a game `state` to the Raylib window.  Pure drawing — no state,
// no input.  Must be called inside BeginDrawing()/EndDrawing().
//
//   state    — the latest server state (flag + players).
//   self_id  — this client's player id (drawn green).
//   names    — id → display name map (from lobby rosters).  IDs absent
//              from the map are drawn using their id.
void draw_game_view(const ctf::State& state, const std::string& self_id,
                    const std::map<std::string, std::string>& names);

}  // namespace ctf::client