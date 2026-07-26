#include "framing.hpp"
#include "constants.hpp"
#include "json.hpp"

namespace ctf {
inline namespace framing {

LineBuffer::LineBuffer(std::size_t max_size)
    : max_size_(max_size) {}

void LineBuffer::append(const char* data, std::size_t len) {
    if (buffer_.size() + len > max_size_) {
        throw message_too_large_error();
    }
    buffer_.append(data, len);
}

auto LineBuffer::extract() -> std::optional<std::string> {
    auto pos = buffer_.find('\n');
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    std::string msg = buffer_.substr(0, pos);
    // Strip trailing \r to tolerate \r\n line endings.
    if (!msg.empty() && msg.back() == '\r') {
        msg.pop_back();
    }
    buffer_.erase(0, pos + 1);
    return msg;
}

void LineBuffer::reset() {
    buffer_.clear();
}

bool LineBuffer::empty() const {
    return buffer_.empty();
}

auto encode(const nlohmann::json& msg) -> std::string {
    auto encoded = ctf::json::dump_compact(msg);
    encoded.push_back('\n');
    if (encoded.size() > ctf::constants::message_max_size) {
        throw message_too_large_error();
    }
    return encoded;
}

}  // namespace framing
}  // namespace ctf
