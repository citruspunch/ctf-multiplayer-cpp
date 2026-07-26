#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "constants.hpp"

namespace ctf {
inline namespace framing {

// Exception thrown when a message exceeds the maximum allowed size.
class message_too_large_error : public std::runtime_error {
public:
    message_too_large_error()
        : std::runtime_error("message exceeds maximum size") {}

    auto error_code() const -> std::string {
        return "MESSAGE_TOO_LARGE";
    }
};

// Accumulates raw bytes and extracts complete JSON lines delimited by '\n'.
// Tolerates "\r\n" line endings (the '\r' is stripped).
class LineBuffer {
public:
    explicit LineBuffer(
        std::size_t max_size = ctf::constants::message_max_size);

    // Append raw bytes to the buffer. Throws message_too_large_error if the
    // total buffer exceeds max_size after appending.
    void append(const char* data, std::size_t len);

    // If a '\n' is present, extract the message before it, strip trailing
    // '\r', remove everything up to and including '\n' from the buffer, and
    // return the message. Otherwise returns std::nullopt.
    auto extract() -> std::optional<std::string>;

    // Clear the buffer.
    void reset();

    // Whether the buffer is empty.
    bool empty() const;

private:
    std::string buffer_;
    std::size_t max_size_;
};

// Serialize a JSON message to a compact string and append '\n'.
// Throws message_too_large_error if the encoded message exceeds
// ctf::constants::message_max_size.
auto encode(const nlohmann::json& msg) -> std::string;

}  // namespace framing
}  // namespace ctf
