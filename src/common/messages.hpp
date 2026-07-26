#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ctf {

// ── Error codes (SPEC §5.1) ──────────────────────────────────────────────

inline constexpr auto INVALID_JSON      = "INVALID_JSON";
inline constexpr auto UNKNOWN_TYPE      = "UNKNOWN_TYPE";
inline constexpr auto MISSING_FIELD     = "MISSING_FIELD";
inline constexpr auto INVALID_FIELD     = "INVALID_FIELD";
inline constexpr auto INVALID_PHASE     = "INVALID_PHASE";
inline constexpr auto VERSION_MISMATCH  = "VERSION_MISMATCH";
inline constexpr auto LOBBY_FULL        = "LOBBY_FULL";
inline constexpr auto NAME_INVALID      = "NAME_INVALID";
inline constexpr auto GAME_STARTED      = "GAME_STARTED";
inline constexpr auto MESSAGE_TOO_LARGE = "MESSAGE_TOO_LARGE";
inline constexpr auto NOT_JOINED        = "NOT_JOINED";

// Returned by validation functions when a field check fails.
struct ErrorParams {
    std::string reason;
};

// ── Data structures (all 12 message types + helpers) ────────────────────

// 1. discover  (C → UDP)
struct Discover {
    int v;
};

// 2. server_info  (S → UDP)
struct ServerInfo {
    int         v;
    std::string name;
    int         tcp_port;
    std::string state;    // "lobby" | "playing"
    int         players;
};

// 3. join  (C → S)
struct Join {
    int         v;
    std::string name;
};

// 4. input  (C → S)
struct Input {
    int dir_x;
    int dir_y;
};

// 5. interact  (C → S)
struct Interact {};

// Nested config sent inside welcome
struct Config {
    int map_size;
    int circle_radius;
    int player_radius;
    int interact_radius;
    int speed;
    int tick_rate;
};

// 6. welcome  (S → C)
struct Welcome {
    std::string player_id;
    Config      config;
};

// Helper for lobby / state player lists
struct PlayerInfo {
    std::string id;
    std::string name;
};

// 7. lobby  (S → C)
struct Lobby {
    std::vector<PlayerInfo> players;
};

// 8. countdown  (S → C)
struct Countdown {
    int seconds;
};

// 9. start  (S → C)
struct Start {};

// Helper for state's flag
struct Flag {
    std::optional<std::string> owner;
    double x;
    double y;
};

// Helper for state's player entry
struct Player {
    std::string id;
    double x;
    double y;
};

// 10. state  (S → C)
struct State {
    Flag                flag;
    std::vector<Player> players;
};

// 11. game_over  (S → C)
struct GameOver {
    std::string winner;
};

// 12. error  (S → C)
struct Error {
    std::string reason;
};

// Variant covering all 12 message types.
using Message = std::variant<
    Discover, ServerInfo, Join, Input, Interact,
    Welcome, Lobby, Countdown, Start, State, GameOver,
    ctf::Error
>;

} // namespace ctf

// ── Serialization / deserialization ──────────────────────────────────────

namespace ctf::msg {

// Parse a JSON object and return the corresponding message variant.
// Returns ctf::Error{reason} if parsing or validation fails.
auto from_json(const nlohmann::json& j) -> ctf::Message;

// Serialize server-sent messages to JSON.
auto to_json(const ctf::ServerInfo&) -> nlohmann::json;
auto to_json(const ctf::Welcome&)   -> nlohmann::json;
auto to_json(const ctf::Lobby&)     -> nlohmann::json;
auto to_json(const ctf::Countdown&) -> nlohmann::json;
auto to_json(const ctf::Start&)     -> nlohmann::json;
auto to_json(const ctf::State&)     -> nlohmann::json;
auto to_json(const ctf::GameOver&)  -> nlohmann::json;
auto to_json(const ctf::Error&)     -> nlohmann::json;

// Validation per message type — returns std::nullopt on success, or
// an ErrorParams describing the problem.
auto validate_discover(const nlohmann::json& j)     -> std::optional<ctf::ErrorParams>;
auto validate_server_info(const nlohmann::json& j)  -> std::optional<ctf::ErrorParams>;
auto validate_join(const nlohmann::json& j)         -> std::optional<ctf::ErrorParams>;
auto validate_input(const nlohmann::json& j)        -> std::optional<ctf::ErrorParams>;
auto validate_interact(const nlohmann::json& j)     -> std::optional<ctf::ErrorParams>;

} // namespace ctf::msg
