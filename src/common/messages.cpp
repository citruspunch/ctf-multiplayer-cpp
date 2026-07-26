#include "messages.hpp"
#include "constants.hpp"

#include <algorithm>
#include <cctype>

namespace ctf {
namespace {

// ── Helpers ──────────────────────────────────────────────────────────────

// Check that `j` contains `field` and it is a string.
auto require_string(const nlohmann::json& j, const char* field)
    -> std::optional<ErrorParams>
{
    if (!j.contains(field)) {
        return ErrorParams{MISSING_FIELD};
    }
    if (!j[field].is_string()) {
        return ErrorParams{INVALID_FIELD};
    }
    return std::nullopt;
}

// Check that `j` contains `field` and it is an integer (not a float).
auto require_integer(const nlohmann::json& j, const char* field)
    -> std::optional<ErrorParams>
{
    if (!j.contains(field)) {
        return ErrorParams{MISSING_FIELD};
    }
    if (!j[field].is_number_integer()) {
        return ErrorParams{INVALID_FIELD};
    }
    return std::nullopt;
}

// Check that `j` contains `field` and it is a number (int or float).
auto require_number(const nlohmann::json& j, const char* field)
    -> std::optional<ErrorParams>
{
    if (!j.contains(field)) {
        return ErrorParams{MISSING_FIELD};
    }
    if (!j[field].is_number()) {
        return ErrorParams{INVALID_FIELD};
    }
    return std::nullopt;
}

// Trim leading and trailing whitespace (space, tab, etc.).
auto trimmed(std::string s) -> std::string {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto front = std::find_if(s.begin(), s.end(), not_space);
    if (front == s.end()) return {};
    auto back  = std::find_if(s.rbegin(), s.rend(), not_space);
    return {front, back.base()};
}

// Validate a player name post-trim: 1..max_length, no control chars.
auto validate_name_string(const std::string& raw) -> std::optional<ErrorParams> {
    auto t = trimmed(raw);
    if (t.empty() || t.size() > static_cast<std::size_t>(constants::name_max_length)) {
        return ErrorParams{NAME_INVALID};
    }
    for (unsigned char c : t) {
        if (c < 0x20 || c == 0x7F) {  // control characters
            return ErrorParams{NAME_INVALID};
        }
    }
    return std::nullopt;
}

// Validate `v` field: must be integer equal to 1.
auto validate_v(const nlohmann::json& j) -> std::optional<ErrorParams> {
    auto err = require_integer(j, "v");
    if (err) return err;
    if (j["v"].get<int>() != 1) {
        return ErrorParams{VERSION_MISMATCH};
    }
    return std::nullopt;
}

// Validate dir.x and dir.y inside a `dir` sub-object.
auto validate_dir(const nlohmann::json& j) -> std::optional<ErrorParams> {
    if (!j.contains("dir") || !j["dir"].is_object()) {
        return ErrorParams{MISSING_FIELD};
    }
    const auto& dir = j["dir"];

    auto ex = require_integer(dir, "x");
    if (ex) return ex;
    ex = require_integer(dir, "y");
    if (ex) return ex;

    int dx = dir["x"].get<int>();
    int dy = dir["y"].get<int>();
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
        return ErrorParams{INVALID_FIELD};
    }
    return std::nullopt;
}

} // anonymous namespace
} // namespace ctf

// ── Validation public functions ──────────────────────────────────────────

namespace ctf::msg {

auto validate_discover(const nlohmann::json& j) -> std::optional<ErrorParams> {
    return validate_v(j);
}

auto validate_server_info(const nlohmann::json& j) -> std::optional<ErrorParams> {
    {
        auto err = validate_v(j);
        if (err) return err;
    }
    {
        auto err = require_string(j, "name");
        if (err) return err;
    }
    {
        auto err = require_integer(j, "tcp_port");
        if (err) return err;
    }
    {
        auto err = require_string(j, "state");
        if (err) return err;
    }
    {
        auto err = require_integer(j, "players");
        if (err) return err;
    }
    // state must be "lobby" or "playing"
    auto state = j["state"].get<std::string>();
    if (state != "lobby" && state != "playing") {
        return ErrorParams{INVALID_FIELD};
    }
    return std::nullopt;
}

auto validate_join(const nlohmann::json& j) -> std::optional<ErrorParams> {
    {
        auto err = validate_v(j);
        if (err) return err;
    }
    {
        auto err = require_string(j, "name");
        if (err) return err;
    }
    {
        auto err = validate_name_string(j["name"].get_ref<const std::string&>());
        if (err) return err;
    }
    return std::nullopt;
}

auto validate_input(const nlohmann::json& j) -> std::optional<ErrorParams> {
    return validate_dir(j);
}

auto validate_interact(const nlohmann::json& /*j*/) -> std::optional<ErrorParams> {
    // interact has no mandatory fields; any JSON object with type "interact" is valid.
    return std::nullopt;
}

// ── from_json: dispatch ─────────────────────────────────────────────────

auto from_json(const nlohmann::json& j) -> Message {
    // Check that "type" is present and is a string.
    auto type_err = require_string(j, "type");
    if (type_err) {
        // Could be MISSING_FIELD (no type) or INVALID_FIELD (type not a string)
        return ctf::Error{type_err->reason};
    }

    auto type = j["type"].get<std::string>();

    if (type == "discover") {
        auto err = validate_discover(j);
        if (err) return ctf::Error{err->reason};
        return Discover{j["v"].get<int>()};
    }

    if (type == "server_info") {
        auto err = validate_server_info(j);
        if (err) return ctf::Error{err->reason};
        return ServerInfo{
            j["v"].get<int>(),
            j["name"].get<std::string>(),
            j["tcp_port"].get<int>(),
            j["state"].get<std::string>(),
            j["players"].get<int>()
        };
    }

    if (type == "join") {
        auto err = validate_join(j);
        if (err) return ctf::Error{err->reason};
        return Join{j["v"].get<int>(), trimmed(j["name"].get<std::string>())};
    }

    if (type == "input") {
        auto err = validate_input(j);
        if (err) return ctf::Error{err->reason};
        return Input{j["dir"]["x"].get<int>(), j["dir"]["y"].get<int>()};
    }

    if (type == "interact") {
        auto err = validate_interact(j);
        if (err) return ctf::Error{err->reason};
        return Interact{};
    }

    if (type == "welcome") {
        // Server-sent message; light validation.
        if (!j.contains("player_id") || !j["player_id"].is_string()) {
            return ctf::Error{MISSING_FIELD};
        }
        if (!j.contains("config") || !j["config"].is_object()) {
            return ctf::Error{MISSING_FIELD};
        }
        const auto& cfg = j["config"];
        auto ci = require_integer(cfg, "map_size");
        if (ci) return ctf::Error{ci->reason};
        ci = require_integer(cfg, "circle_radius");
        if (ci) return ctf::Error{ci->reason};
        ci = require_integer(cfg, "player_radius");
        if (ci) return ctf::Error{ci->reason};
        ci = require_integer(cfg, "interact_radius");
        if (ci) return ctf::Error{ci->reason};
        ci = require_integer(cfg, "speed");
        if (ci) return ctf::Error{ci->reason};
        ci = require_integer(cfg, "tick_rate");
        if (ci) return ctf::Error{ci->reason};

        return Welcome{
            j["player_id"].get<std::string>(),
            Config{
                cfg["map_size"].get<int>(),
                cfg["circle_radius"].get<int>(),
                cfg["player_radius"].get<int>(),
                cfg["interact_radius"].get<int>(),
                cfg["speed"].get<int>(),
                cfg["tick_rate"].get<int>()
            }
        };
    }

    if (type == "lobby") {
        if (!j.contains("players") || !j["players"].is_array()) {
            return ctf::Error{MISSING_FIELD};
        }
        Lobby lobby;
        for (const auto& p : j["players"]) {
            if (!p.contains("id") || !p["id"].is_string() ||
                !p.contains("name") || !p["name"].is_string()) {
                return ctf::Error{INVALID_FIELD};
            }
            lobby.players.push_back(PlayerInfo{
                p["id"].get<std::string>(),
                p["name"].get<std::string>()
            });
        }
        return lobby;
    }

    if (type == "countdown") {
        auto err = require_integer(j, "seconds");
        if (err) return ctf::Error{err->reason};
        return Countdown{j["seconds"].get<int>()};
    }

    if (type == "start") {
        return Start{};
    }

    if (type == "state") {
        if (!j.contains("flag") || !j["flag"].is_object()) {
            return ctf::Error{MISSING_FIELD};
        }
        const auto& fj = j["flag"];

        // owner: null (free) or string
        std::optional<std::string> owner;
        if (fj.contains("owner")) {
            if (fj["owner"].is_string()) {
                owner = fj["owner"].get<std::string>();
            } else if (!fj["owner"].is_null()) {
                return ctf::Error{INVALID_FIELD};
            }
        }
        auto ne = require_number(fj, "x");
        if (ne) return ctf::Error{ne->reason};
        ne = require_number(fj, "y");
        if (ne) return ctf::Error{ne->reason};

        State state;
        state.flag = Flag{owner, fj["x"].get<double>(), fj["y"].get<double>()};

        if (!j.contains("players") || !j["players"].is_array()) {
            return ctf::Error{MISSING_FIELD};
        }
        for (const auto& p : j["players"]) {
            if (!p.contains("id") || !p["id"].is_string()) {
                return ctf::Error{INVALID_FIELD};
            }
            auto ne2 = require_number(p, "x");
            if (ne2) return ctf::Error{ne2->reason};
            ne2 = require_number(p, "y");
            if (ne2) return ctf::Error{ne2->reason};
            state.players.push_back(Player{
                p["id"].get<std::string>(),
                p["x"].get<double>(),
                p["y"].get<double>()
            });
        }
        return state;
    }

    if (type == "game_over") {
        auto err = require_string(j, "winner");
        if (err) return ctf::Error{err->reason};
        return GameOver{j["winner"].get<std::string>()};
    }

    if (type == "error") {
        auto err = require_string(j, "reason");
        if (err) return ctf::Error{err->reason};
        return ctf::Error{j["reason"].get<std::string>()};
    }

    // Unknown type
    return ctf::Error{UNKNOWN_TYPE};
}

// ── to_json overloads ────────────────────────────────────────────────────

auto to_json(const ServerInfo& msg) -> nlohmann::json {
    return {
        {"type", "server_info"},
        {"v", msg.v},
        {"name", msg.name},
        {"tcp_port", msg.tcp_port},
        {"state", msg.state},
        {"players", msg.players}
    };
}

auto to_json(const Welcome& msg) -> nlohmann::json {
    return {
        {"type", "welcome"},
        {"player_id", msg.player_id},
        {"config", {
            {"map_size", msg.config.map_size},
            {"circle_radius", msg.config.circle_radius},
            {"player_radius", msg.config.player_radius},
            {"interact_radius", msg.config.interact_radius},
            {"speed", msg.config.speed},
            {"tick_rate", msg.config.tick_rate}
        }}
    };
}

auto to_json(const Lobby& msg) -> nlohmann::json {
    nlohmann::json j;
    j["type"] = "lobby";
    auto& arr = j["players"] = nlohmann::json::array();
    for (const auto& p : msg.players) {
        arr.push_back({{"id", p.id}, {"name", p.name}});
    }
    return j;
}

auto to_json(const Countdown& msg) -> nlohmann::json {
    return {
        {"type", "countdown"},
        {"seconds", msg.seconds}
    };
}

auto to_json(const Start& /*msg*/) -> nlohmann::json {
    return {{"type", "start"}};
}

auto to_json(const State& msg) -> nlohmann::json {
    nlohmann::json j;
    j["type"] = "state";

    if (msg.flag.owner.has_value()) {
        j["flag"]["owner"] = msg.flag.owner.value();
    } else {
        j["flag"]["owner"] = nullptr;
    }
    j["flag"]["x"] = msg.flag.x;
    j["flag"]["y"] = msg.flag.y;

    auto& arr = j["players"] = nlohmann::json::array();
    for (const auto& p : msg.players) {
        arr.push_back({{"id", p.id}, {"x", p.x}, {"y", p.y}});
    }
    return j;
}

auto to_json(const GameOver& msg) -> nlohmann::json {
    return {
        {"type", "game_over"},
        {"winner", msg.winner}
    };
}

auto to_json(const ctf::Error& msg) -> nlohmann::json {
    return {
        {"type", "error"},
        {"reason", msg.reason}
    };
}

} // namespace ctf::msg
