#include "json.hpp"

namespace ctf {
inline namespace json {

auto parse_line(std::string_view line) -> std::optional<nlohmann::json> {
    try {
        return nlohmann::json::parse(line);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

auto dump_compact(const nlohmann::json& j) -> std::string {
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::ignore);
}

}  // namespace json
}  // namespace ctf
