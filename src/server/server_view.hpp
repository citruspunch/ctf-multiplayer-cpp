#pragma once

#include "game.hpp"

#include <string>

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
    // phase_text is a readable string like "LOBBY", "COUNTDOWN 3",
    // "PLAYING", "GAME OVER — Winner: X".
    void render(const std::string& phase_text);

    // Returns true when the user closes the window.
    auto should_close() -> bool;

private:
    const game::GameState& game_state_;
    bool initialised_{false};
};

}  // namespace ctf::server
