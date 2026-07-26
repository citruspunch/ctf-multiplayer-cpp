#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace ctf {
inline namespace json {

// Parse a JSON line from a string view. Returns nullopt on invalid JSON.
// Accepts surrounding whitespace.
auto parse_line(std::string_view line) -> std::optional<nlohmann::json>;

// Serialize JSON to a compact string (no spaces, no newlines).
// Invalid UTF-8 sequences are silently ignored.
auto dump_compact(const nlohmann::json& j) -> std::string;

}  // namespace json
}  // namespace ctf
