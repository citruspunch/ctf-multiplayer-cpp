#include <catch2/catch_test_macros.hpp>

#include "json.hpp"
#include "messages.hpp"

#include <string>

using namespace ctf;

// Helper: parse a JSON string into a Message variant.
static auto parse(const std::string& s) -> Message {
    auto j = json::parse_line(s);
    REQUIRE(j.has_value());
    return msg::from_json(*j);
}

// Helper: check that a parsed message is an Error with the given reason.
static auto is_error(const Message& m, const char* reason) -> bool {
    if (auto* e = std::get_if<Error>(&m)) {
        return e->reason == reason;
    }
    return false;
}

// ── discover ──────────────────────────────────────────────────────────────

TEST_CASE("discover with v:1 parses ok", "[messages]") {
    auto m = parse(R"({"type":"discover","v":1})");
    auto* d = std::get_if<Discover>(&m);
    REQUIRE(d != nullptr);
    CHECK(d->v == 1);
}

TEST_CASE("discover with v:2 returns VERSION_MISMATCH", "[messages]") {
    auto m = parse(R"({"type":"discover","v":2})");
    CHECK(is_error(m, VERSION_MISMATCH));
}

// ── input ──────────────────────────────────────────────────────────────────

TEST_CASE("input with dir.x:2 returns INVALID_FIELD", "[messages]") {
    auto m = parse(R"({"type":"input","dir":{"x":2,"y":0}})");
    CHECK(is_error(m, INVALID_FIELD));
}

TEST_CASE("input with valid dir parses ok", "[messages]") {
    auto m = parse(R"({"type":"input","dir":{"x":-1,"y":1}})");
    auto* i = std::get_if<Input>(&m);
    REQUIRE(i != nullptr);
    CHECK(i->dir_x == -1);
    CHECK(i->dir_y == 1);
}

// ── join ──────────────────────────────────────────────────────────────────

TEST_CASE("join with empty name returns NAME_INVALID", "[messages]") {
    auto m = parse(R"({"type":"join","v":1,"name":""})");
    CHECK(is_error(m, NAME_INVALID));
}

TEST_CASE("join with 21-char name returns NAME_INVALID", "[messages]") {
    auto m = parse(R"({"type":"join","v":1,"name":"abcdefghijklmnopqrstu"})");
    CHECK(is_error(m, NAME_INVALID));
}

TEST_CASE("join with valid name parses ok", "[messages]") {
    auto m = parse(R"({"type":"join","v":1,"name":"Alice"})");
    auto* j = std::get_if<Join>(&m);
    REQUIRE(j != nullptr);
    CHECK(j->v == 1);
    CHECK(j->name == "Alice");
}

TEST_CASE("join trims surrounding whitespace from name", "[messages]") {
    auto m = parse(R"({"type":"join","v":1,"name":"  Bob  "})");
    auto* j = std::get_if<Join>(&m);
    REQUIRE(j != nullptr);
    CHECK(j->name == "Bob");
}

// ── unknown type ──────────────────────────────────────────────────────────

TEST_CASE("unknown type returns UNKNOWN_TYPE", "[messages]") {
    auto m = parse(R"({"type":"frobnicate","v":1})");
    CHECK(is_error(m, UNKNOWN_TYPE));
}

// ── invalid JSON ──────────────────────────────────────────────────────────

TEST_CASE("invalid JSON returns nullopt from parse_line", "[messages]") {
    auto j = json::parse_line("not json at all");
    CHECK_FALSE(j.has_value());
}

// ── missing field ─────────────────────────────────────────────────────────

TEST_CASE("missing type field returns MISSING_FIELD", "[messages]") {
    auto m = parse(R"({"v":1})");
    CHECK(is_error(m, MISSING_FIELD));
}

TEST_CASE("input missing dir returns MISSING_FIELD", "[messages]") {
    auto m = parse(R"({"type":"input"})");
    CHECK(is_error(m, MISSING_FIELD));
}

TEST_CASE("join missing name returns MISSING_FIELD", "[messages]") {
    auto m = parse(R"({"type":"join","v":1})");
    CHECK(is_error(m, MISSING_FIELD));
}