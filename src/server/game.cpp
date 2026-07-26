#include "game.hpp"

#include "constants.hpp"

#include <cmath>

namespace ctf::game {

auto spawn_player(const std::string& id, const std::string& name,
                  std::mt19937& rng) -> PlayerState {
    static constexpr double pi = 3.14159265358979323846;
    std::uniform_real_distribution<double> angle_dist(0.0, 2.0 * pi);
    std::uniform_real_distribution<double> radius_dist(
        static_cast<double>(constants::spawn_radius_min),
        static_cast<double>(constants::spawn_radius_max));

    double theta = angle_dist(rng);
    double r = radius_dist(rng);

    PlayerState p;
    p.id   = id;
    p.name = name;
    p.x = 500.0 + r * std::cos(theta);
    p.y = 500.0 + r * std::sin(theta);
    p.dir_x = 0;
    p.dir_y = 0;
    p.inside_on_pickup = false;  // Outside by construction (R >= 350 > 315).
    return p;
}

void move_player(PlayerState& p, double dt) {
    // Normalise diagonal movement (both dir_x and dir_y non-zero).
    double dx = static_cast<double>(p.dir_x);
    double dy = static_cast<double>(p.dir_y);
    if (dx != 0.0 && dy != 0.0) {
        double inv_sqrt2 = 1.0 / std::sqrt(2.0);
        dx *= inv_sqrt2;
        dy *= inv_sqrt2;
    }

    p.x += dx * static_cast<double>(constants::speed) * dt;
    p.y += dy * static_cast<double>(constants::speed) * dt;

    // Clamp to [player_radius, map_size - player_radius].
    if (p.x < constants::player_radius) p.x = constants::player_radius;
    if (p.x > constants::map_size - constants::player_radius) p.x = constants::map_size - constants::player_radius;
    if (p.y < constants::player_radius) p.y = constants::player_radius;
    if (p.y > constants::map_size - constants::player_radius) p.y = constants::map_size - constants::player_radius;
}

auto distance_to_centre(const PlayerState& p) -> double {
    double dx = p.x - static_cast<double>(constants::circle_center_x);
    double dy = p.y - static_cast<double>(constants::circle_center_y);
    return std::sqrt(dx * dx + dy * dy);
}

auto is_inside_circle(const PlayerState& p) -> bool {
    return distance_to_centre(p) <= static_cast<double>(constants::victory_distance);
}

void process_interact(GameState& gs, const std::string& actor_id) {
    auto* actor = find_player(gs, actor_id);
    if (!actor) return;

    // If flag is free.
    if (!gs.flag.owner.has_value()) {
        double dist_to_flag = std::sqrt(
            (actor->x - gs.flag.x) * (actor->x - gs.flag.x) +
            (actor->y - gs.flag.y) * (actor->y - gs.flag.y));
        if (dist_to_flag <= static_cast<double>(constants::interact_radius)) {
            gs.flag.owner = actor_id;
            actor->inside_on_pickup = is_inside_circle(*actor);
        }
        return;
    }

    // If flag is owned by someone else.
    if (gs.flag.owner.value() != actor_id) {
        auto* carrier = find_player(gs, gs.flag.owner.value());
        if (!carrier) return;
        double dist_to_carrier = std::sqrt(
            (actor->x - carrier->x) * (actor->x - carrier->x) +
            (actor->y - carrier->y) * (actor->y - carrier->y));
        if (dist_to_carrier <= static_cast<double>(constants::interact_radius)) {
            gs.flag.owner = actor_id;
            actor->inside_on_pickup = is_inside_circle(*actor);
        }
        return;
    }
    // Actor already owns the flag — interact is a no-op.
}

auto check_victory(const GameState& gs, const std::string& carrier_id) -> bool {
    auto* carrier = find_player(gs, carrier_id);
    if (!carrier) return false;
    return carrier->inside_on_pickup && distance_to_centre(*carrier) > static_cast<double>(constants::victory_distance);
}

void reset_flag(GameState& gs) {
    gs.flag.owner.reset();
    gs.flag.x = 500.0;
    gs.flag.y = 500.0;
}

auto find_player(GameState& gs, const std::string& id) -> PlayerState* {
    for (auto& p : gs.players) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

auto find_player(const GameState& gs, const std::string& id) -> const PlayerState* {
    for (const auto& p : gs.players) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

auto round_1dp(double v) -> double {
    // Half-away-from-zero rounding to 1 decimal place.
    double scaled = v * 10.0;
    if (scaled >= 0.0) {
        return std::floor(scaled + 0.5) / 10.0;
    } else {
        return std::ceil(scaled - 0.5) / 10.0;
    }
}

}  // namespace ctf::game
