#pragma once

#include <optional>
#include <random>
#include <string>
#include <vector>

namespace ctf::game {

// ── Data structures ──────────────────────────────────────────────────────

struct PlayerState {
    std::string id;
    std::string name;
    double x{500.0};
    double y{500.0};
    int dir_x{0};
    int dir_y{0};
    bool inside_on_pickup{false};
};

struct FlagState {
    std::optional<std::string> owner;
    double x{500.0};
    double y{500.0};
};

struct GameState {
    FlagState flag;
    std::vector<PlayerState> players;
};

// ── Pure domain functions ────────────────────────────────────────────────

// Spawn a new player on the ring outside the circle.
// θ ∈ [0, 2π), R ∈ [spawn_radius_min, spawn_radius_max].
// Uses the provided RNG engine so callers can control determinism.
auto spawn_player(const std::string& id, const std::string& name,
                  std::mt19937& rng) -> PlayerState;

// Move a player by integrating its direction for dt seconds.
// Normalises diagonals (÷√2).  Clamps position to [15, 985].
// Uses constants::speed for velocity.
void move_player(PlayerState& p, double dt);

// Euclidean distance from the player to the centre (500, 500).
auto distance_to_centre(const PlayerState& p) -> double;

// Whether the player is inside (or on) the victory circle.
auto is_inside_circle(const PlayerState& p) -> bool;

// Process an interact action by a player.
// - If flag is free and actor is within interact_radius → capture.
// - If flag is owned by someone else and actor is within interact_radius of
//   the carrier → steal.
// - Updates `inside_on_pickup` for the new carrier.
// Modifies `gs` in place.
void process_interact(GameState& gs, const std::string& actor_id);

// Check whether the given carrier meets the victory condition:
// they were inside on pickup and are now strictly outside.
auto check_victory(const GameState& gs,
                   const std::string& carrier_id) -> bool;

// Reset the flag to the centre with no owner.
void reset_flag(GameState& gs);

// Find a player by ID, or nullptr if not found.
auto find_player(GameState& gs,
                 const std::string& id) -> PlayerState*;
auto find_player(const GameState& gs,
                 const std::string& id) -> const PlayerState*;

// Round a double to 1 decimal place (half-away-from-zero).
auto round_1dp(double v) -> double;

}  // namespace ctf::game
