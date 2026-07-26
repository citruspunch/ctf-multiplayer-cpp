#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "constants.hpp"
#include "game.hpp"

#include <cmath>
#include <random>
#include <string>

using Catch::Approx;
using namespace ctf;
using namespace ctf::game;

// ── Spawn ─────────────────────────────────────────────────────────────────

TEST_CASE("spawn_player places all players in ring and outside circle",
          "[domain]") {
    std::mt19937 rng(42);  // fixed seed for determinism

    for (int i = 0; i < 100; ++i) {
        auto id = "p" + std::to_string(i);
        auto p = spawn_player(id, id, rng);

        double dist = distance_to_centre(p);
        CHECK(dist >= static_cast<double>(spawn_radius_min));
        CHECK(dist <= static_cast<double>(spawn_radius_max));
        // All spawns must be outside the victory circle.
        CHECK(dist > static_cast<double>(victory_distance));
        CHECK(p.inside_on_pickup == false);
    }
}

// ── Movement: cardinal ────────────────────────────────────────────────────

TEST_CASE("move_player (1,0) at 200 u/s for 1 s moves x+200", "[domain]") {
    PlayerState p;
    p.x = 500.0;
    p.y = 500.0;
    p.dir_x = 1;
    p.dir_y = 0;

    move_player(p, 1.0);

    CHECK(p.x == Approx(700.0).epsilon(0.001));
    CHECK(p.y == Approx(500.0).epsilon(0.001));
}

// ── Movement: diagonal normalisation ─────────────────────────────────────

TEST_CASE("move_player diagonal (1,1) normalises by sqrt(2)", "[domain]") {
    PlayerState p;
    p.x = 500.0;
    p.y = 500.0;
    p.dir_x = 1;
    p.dir_y = 1;

    move_player(p, 1.0);

    double dx = p.x - 500.0;
    double dy = p.y - 500.0;
    double total = std::sqrt(dx * dx + dy * dy);
    CHECK(total == Approx(static_cast<double>(speed)).epsilon(0.01));
}

// ── Clamp ─────────────────────────────────────────────────────────────────

TEST_CASE("move_player clamps to [15, 985]", "[domain]") {
    PlayerState p;
    p.x = 10.0;
    p.y = 1000.0;
    p.dir_x = -1;
    p.dir_y = 1;

    move_player(p, 1.0);

    CHECK(p.x == Approx(15.0).epsilon(0.001));
    CHECK(p.y == Approx(985.0).epsilon(0.001));
}

// ── Victory ───────────────────────────────────────────────────────────────

TEST_CASE("check_victory: distance 316 wins", "[domain]") {
    GameState gs;
    PlayerState p;
    p.id = "p1";
    p.x = 500.0 + 316.0;
    p.y = 500.0;
    p.inside_on_pickup = true;
    gs.players.push_back(p);

    CHECK(check_victory(gs, "p1") == true);
}

TEST_CASE("check_victory: distance 315 does not win", "[domain]") {
    GameState gs;
    PlayerState p;
    p.id = "p1";
    p.x = 500.0 + 315.0;
    p.y = 500.0;
    p.inside_on_pickup = true;
    gs.players.push_back(p);

    CHECK(check_victory(gs, "p1") == false);
}

TEST_CASE("check_victory: outside but inside_on_pickup false does not win",
          "[domain]") {
    GameState gs;
    PlayerState p;
    p.id = "p1";
    p.x = 500.0 + 400.0;
    p.y = 500.0;
    p.inside_on_pickup = false;
    gs.players.push_back(p);

    CHECK(check_victory(gs, "p1") == false);
}

// ── Capture ────────────────────────────────────────────────────────────────

TEST_CASE("process_interact captures flag at dist 40", "[domain]") {
    GameState gs;
    gs.flag = {std::nullopt, 500.0, 500.0};

    PlayerState p;
    p.id = "p1";
    p.x = 500.0 + 40.0;
    p.y = 500.0;
    gs.players.push_back(p);

    process_interact(gs, "p1");

    REQUIRE(gs.flag.owner.has_value());
    CHECK(gs.flag.owner.value() == "p1");
}

TEST_CASE("process_interact fails capture at dist 41", "[domain]") {
    GameState gs;
    gs.flag = {std::nullopt, 500.0, 500.0};

    PlayerState p;
    p.id = "p1";
    p.x = 500.0 + 41.0;
    p.y = 500.0;
    gs.players.push_back(p);

    process_interact(gs, "p1");

    CHECK_FALSE(gs.flag.owner.has_value());
}

// ── Steal ─────────────────────────────────────────────────────────────────

TEST_CASE("process_interact steals flag at dist 40", "[domain]") {
    GameState gs;
    gs.flag = {std::nullopt, 500.0, 500.0};

    PlayerState carrier;
    carrier.id = "p1";
    carrier.x = 500.0;
    carrier.y = 500.0;
    gs.players.push_back(carrier);
    gs.flag.owner = "p1";

    PlayerState thief;
    thief.id = "p2";
    thief.x = 500.0 + 40.0;
    thief.y = 500.0;
    gs.players.push_back(thief);

    process_interact(gs, "p2");

    REQUIRE(gs.flag.owner.has_value());
    CHECK(gs.flag.owner.value() == "p2");
}

TEST_CASE("process_interact fails steal at dist 41", "[domain]") {
    GameState gs;
    gs.flag = {std::nullopt, 500.0, 500.0};

    PlayerState carrier;
    carrier.id = "p1";
    carrier.x = 500.0;
    carrier.y = 500.0;
    gs.players.push_back(carrier);
    gs.flag.owner = "p1";

    PlayerState thief;
    thief.id = "p2";
    thief.x = 500.0 + 41.0;
    thief.y = 500.0;
    gs.players.push_back(thief);

    process_interact(gs, "p2");

    REQUIRE(gs.flag.owner.has_value());
    CHECK(gs.flag.owner.value() == "p1");  // unchanged
}

// ── reset_flag ────────────────────────────────────────────────────────────

TEST_CASE("reset_flag clears owner and centres flag", "[domain]") {
    GameState gs;
    gs.flag = {"p1", 300.0, 300.0};

    reset_flag(gs);

    CHECK_FALSE(gs.flag.owner.has_value());
    CHECK(gs.flag.x == Approx(500.0).epsilon(0.001));
    CHECK(gs.flag.y == Approx(500.0).epsilon(0.001));
}