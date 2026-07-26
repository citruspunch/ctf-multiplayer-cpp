#pragma once

namespace ctf {
inline namespace constants {

// Map dimensions (units)
inline constexpr int map_size = 1000;

// Central circle (the "base")
inline constexpr int circle_radius = 300;
inline constexpr int circle_center_x = 500;
inline constexpr int circle_center_y = 500;

// Player
inline constexpr int player_radius = 15;

// Interaction (capture / steal)
inline constexpr int interact_radius = 40;

// Movement
inline constexpr int speed = 200;  // units/second

// Simulation
inline constexpr int tick_rate = 20;  // Hz

// Phases
inline constexpr int countdown_seconds = 5;
inline constexpr int post_game_seconds = 5;

// Lobby
inline constexpr int min_players = 2;
inline constexpr int max_players = 100;

// Spawn ring (annulus)
inline constexpr int spawn_radius_min = 350;
inline constexpr int spawn_radius_max = 450;

// Victory: carrier transitions from ≤315 to >315 from center
inline constexpr int victory_distance = circle_radius + player_radius;  // 315

// Discovery (UDP)
inline constexpr int discovery_port = 8888;

// TCP
inline constexpr int default_tcp_port = 8889;

// Protocol limits
inline constexpr int name_max_length = 20;
inline constexpr int message_max_size = 64 * 1024;  // 64 KB

}  // namespace constants
}  // namespace ctf
