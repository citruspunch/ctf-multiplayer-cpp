#pragma once

#include "game.hpp"

#include <string>
#include <vector>

namespace ctf::server {

// Raylib observer view for the server.  Renders the game state and phase
// info in a dedicated window.
class ServerView {
public:
    ServerView(const game::GameState& game_state);
    ~ServerView();

    ServerView(const ServerView&) = delete;
    auto operator=(const ServerView&) -> ServerView& = delete;

    // Draw one frame.  Must be called each event-loop iteration.
    // Initialises the Raylib window on first call.
    //
    // phase_text    — e.g. "LOBBY" or "COUNTDOWN 3" or "PLAYING"
    // lobby_names   — (id, name) pairs for connected lobby players
    // lobby_count   — total joined players
    void render(const std::string& phase_text,
                const std::vector<std::pair<std::string, std::string>>& lobby_names,
                int lobby_count);

    // Returns true when the user closes the window.
    auto should_close() -> bool;

    // Returns true once when the server operator presses SPACE to
    // manually start the game (edge-detected).  Only meaningful
    // during LOBBY phase.
    auto start_requested() -> bool;

private:
    const game::GameState& game_state_;
    bool initialised_{false};
    bool start_pressed_{false};
    bool last_space_state_{false};
};

}  // namespace ctf::server
