#include <catch2/catch_test_macros.hpp>

#include "framing.hpp"
#include "json.hpp"

#include <string>

using ctf::framing::LineBuffer;
using ctf::framing::message_too_large_error;

// ── Two messages in one read ──────────────────────────────────────────────

TEST_CASE("LineBuffer extracts two messages from one read", "[framing]") {
    LineBuffer buf;
    std::string data = "{\"a\":1}\n{\"b\":2}\n";
    buf.append(data.data(), data.size());

    auto first = buf.extract();
    REQUIRE(first.has_value());
    CHECK(*first == "{\"a\":1}");

    auto second = buf.extract();
    REQUIRE(second.has_value());
    CHECK(*second == "{\"b\":2}");

    CHECK_FALSE(buf.extract().has_value());
    CHECK(buf.empty());
}

// ── Split message across reads ────────────────────────────────────────────

TEST_CASE("LineBuffer handles split messages", "[framing]") {
    LineBuffer buf;

    // First chunk — no newline yet.
    std::string chunk1 = "{\"a\":";
    buf.append(chunk1.data(), chunk1.size());
    CHECK_FALSE(buf.extract().has_value());

    // Second chunk completes the message.
    std::string chunk2 = "1}\n";
    buf.append(chunk2.data(), chunk2.size());

    auto msg = buf.extract();
    REQUIRE(msg.has_value());
    CHECK(*msg == "{\"a\":1}");
    CHECK(buf.empty());
}

// ── \r\n tolerance ────────────────────────────────────────────────────────

TEST_CASE("LineBuffer strips trailing \\r from \\r\\n endings", "[framing]") {
    LineBuffer buf;
    std::string data = "{\"a\":1}\r\n";
    buf.append(data.data(), data.size());

    auto msg = buf.extract();
    REQUIRE(msg.has_value());
    CHECK(*msg == "{\"a\":1}");
    CHECK_FALSE(msg->back() == '\r');
}

// ── > 64 KB throws MESSAGE_TOO_LARGE ──────────────────────────────────────

TEST_CASE("LineBuffer throws MESSAGE_TOO_LARGE beyond max_size", "[framing]") {
    LineBuffer buf(100);  // small max for easy testing
    std::string big(101, 'x');
    CHECK_THROWS_AS(buf.append(big.data(), big.size()),
                     message_too_large_error);

    // Also test with the default max (64 KB).
    LineBuffer buf64;
    std::string huge(ctf::constants::message_max_size + 1, 'x');
    CHECK_THROWS_AS(buf64.append(huge.data(), huge.size()),
                     message_too_large_error);
}

// ── Empty buffer (no \n) → nullopt ────────────────────────────────────────

TEST_CASE("LineBuffer returns nullopt when no newline", "[framing]") {
    LineBuffer buf;
    std::string data = "no newline here";
    buf.append(data.data(), data.size());
    CHECK_FALSE(buf.extract().has_value());
    CHECK_FALSE(buf.empty());
}

// ── encode produces compact JSON + newline ────────────────────────────────

TEST_CASE("encode appends newline and validates size", "[framing]") {
    nlohmann::json j;
    j["type"] = "discover";
    j["v"] = 1;

    auto encoded = ctf::framing::encode(j);
    CHECK(encoded.back() == '\n');
    CHECK(encoded.find("\"type\":\"discover\"") != std::string::npos);
    CHECK(encoded.find(' ') == std::string::npos);  // compact, no spaces
}