#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "framing.hpp"
#include "json.hpp"
#include "messages.hpp"
#include "net/platform.hpp"
#include "net/tcp_socket.hpp"
#include "server.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using Catch::Approx;
using namespace std::chrono_literals;

// ── Bot client ────────────────────────────────────────────────────────────

namespace {

struct BotPlayer {
    std::string id;
    double x{0.0};
    double y{0.0};
};

class BotClient {
public:
    BotClient(const std::string& name) : name_(name) {}
    ~BotClient() { disconnect(); }

    BotClient(const BotClient&) = delete;
    auto operator=(const BotClient&) -> BotClient& = delete;
    BotClient(BotClient&&) noexcept = default;
    auto operator=(BotClient&&) noexcept -> BotClient& = default;

    auto connect(const std::string& ip, int port) -> bool {
        socket_ = ctf::net::TcpSocket::connect(ip, port, 3000);
        return static_cast<bool>(socket_);
    }

    void send_join() {
        nlohmann::json j;
        j["type"] = "join";
        j["v"] = 1;
        j["name"] = name_;
        send_raw(ctf::framing::encode(j));
    }

    void poll() {
        if (!socket_) return;
        char buf[4096];
        for (;;) {
            auto n = socket_.recv(buf, sizeof(buf));
            if (n > 0) {
                try {
                    recv_buf_.append(buf, static_cast<std::size_t>(n));
                } catch (const ctf::framing::message_too_large_error&) {
                    disconnect();
                    return;
                }
            } else if (n == 0) {
                disconnected_ = true;
                disconnect();
                return;
            } else {
                break;
            }
        }
        while (auto line = recv_buf_.extract()) {
            handle_message(*line);
        }
    }

    void think() {
        if (!socket_ || state_ != BotState::Playing) return;
        auto it = players_.find(player_id_);
        if (it == players_.end()) return;

        const auto& me = it->second;
        double cx = 500.0, cy = 500.0;
        double dist = std::sqrt((me.x - cx) * (me.x - cx) +
                                (me.y - cy) * (me.y - cy));

        if (!flag_captured_) {
            if (dist <= 35.0) {
                send_interact();
            } else {
                send_direction_toward(cx, cy);
            }
        } else {
            if (dist > 320.0) {
                send_direction(0, 0);
            } else {
                send_direction_away(cx, cy);
            }
        }
    }

    auto got_welcome() const -> bool { return got_welcome_; }
    auto got_start() const -> bool { return got_start_; }
    auto got_game_over() const -> bool { return got_game_over_; }
    auto got_lobby_after_game() const -> bool { return got_lobby_after_game_; }
    auto winner() const -> std::string { return winner_; }
    auto player_id() const -> std::string { return player_id_; }
    auto last_flag_owner() const -> std::optional<std::string> { return last_flag_owner_; }
    auto last_flag_x() const -> double { return last_flag_x_; }
    auto last_flag_y() const -> double { return last_flag_y_; }

    void disconnect() { socket_.close(); recv_buf_.reset(); }

private:
    enum class BotState { Joining, Lobby, Countdown, Playing, GameOver };

    void send_raw(const std::string& msg) {
        if (!socket_) return;
        std::string buf = msg;
        while (!buf.empty() && socket_) {
            auto n = socket_.send(buf.data(), buf.size());
            if (n > 0) { buf.erase(0, static_cast<std::size_t>(n)); }
            else if (n == -1) { std::this_thread::sleep_for(1ms); }
            else { disconnect(); return; }
        }
    }

    void send_direction(int dx, int dy) {
        if (dx == last_dir_x_ && dy == last_dir_y_) return;
        last_dir_x_ = dx; last_dir_y_ = dy;
        nlohmann::json j;
        j["type"] = "input";
        j["dir"] = {{"x", dx}, {"y", dy}};
        send_raw(ctf::framing::encode(j));
    }

    void send_direction_toward(double tx, double ty) {
        auto it = players_.find(player_id_);
        if (it == players_.end()) return;
        int dx = 0, dy = 0;
        if (tx > it->second.x + 1.0) dx = 1;
        else if (tx < it->second.x - 1.0) dx = -1;
        if (ty > it->second.y + 1.0) dy = 1;
        else if (ty < it->second.y - 1.0) dy = -1;
        send_direction(dx, dy);
    }

    void send_direction_away(double cx, double cy) {
        auto it = players_.find(player_id_);
        if (it == players_.end()) return;
        int dx = 0, dy = 0;
        if (it->second.x > cx) dx = 1; else if (it->second.x < cx) dx = -1;
        if (it->second.y > cy) dy = 1; else if (it->second.y < cy) dy = -1;
        send_direction(dx, dy);
    }

    void send_interact() {
        nlohmann::json j;
        j["type"] = "interact";
        send_raw(ctf::framing::encode(j));
    }

    void handle_message(const std::string& line) {
        auto j = ctf::json::parse_line(line);
        if (!j) return;
        auto msg = ctf::msg::from_json(*j);

        if (auto* w = std::get_if<ctf::Welcome>(&msg)) {
            player_id_ = w->player_id; got_welcome_ = true;
            state_ = BotState::Lobby; return;
        }
        if (auto* l = std::get_if<ctf::Lobby>(&msg)) {
            if (state_ == BotState::Playing || state_ == BotState::GameOver)
                got_lobby_after_game_ = true;
            state_ = BotState::Lobby; return;
        }
        if (std::get_if<ctf::Countdown>(&msg)) {
            state_ = BotState::Countdown; return;
        }
        if (std::get_if<ctf::Start>(&msg)) {
            got_start_ = true; state_ = BotState::Playing;
            flag_captured_ = false; return;
        }
        if (auto* s = std::get_if<ctf::State>(&msg)) {
            if (state_ == BotState::Playing) {
                players_.clear();
                for (const auto& p : s->players)
                    players_[p.id] = {p.id, p.x, p.y};
                last_flag_owner_ = s->flag.owner;
                last_flag_x_ = s->flag.x; last_flag_y_ = s->flag.y;
                if (s->flag.owner.has_value() && s->flag.owner.value() == player_id_)
                    flag_captured_ = true;
            }
            return;
        }
        if (auto* g = std::get_if<ctf::GameOver>(&msg)) {
            got_game_over_ = true; winner_ = g->winner;
            state_ = BotState::GameOver; return;
        }
    }

    std::string name_;
    ctf::net::TcpSocket socket_;
    ctf::framing::LineBuffer recv_buf_;
    BotState state_{BotState::Joining};
    std::string player_id_;
    std::map<std::string, BotPlayer> players_;
    bool got_welcome_{false}, got_start_{false}, got_game_over_{false};
    bool got_lobby_after_game_{false}, flag_captured_{false}, disconnected_{false};
    int last_dir_x_{999}, last_dir_y_{999};
    std::string winner_;
    std::optional<std::string> last_flag_owner_;
    double last_flag_x_{500.0}, last_flag_y_{500.0};
};

}  // anonymous namespace

// ── Test: Full game lifecycle with 2 bots ─────────────────────────────────

TEST_CASE("Full game lifecycle with 2 bots", "[integration][server]") {
    REQUIRE(ctf::net::init_net());

    ctf::server::Server server(0, true);
    REQUIRE(server.port() > 0);

    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(100ms);

    BotClient bot1("Alice");
    BotClient bot2("Bob");

    REQUIRE(bot1.connect("127.0.0.1", server.port()));
    REQUIRE(bot2.connect("127.0.0.1", server.port()));

    std::this_thread::sleep_for(200ms);
    bot1.send_join();
    bot2.send_join();

    // Wait for start.
    {
        auto deadline = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < deadline) {
            bot1.poll(); bot2.poll();
            if (bot1.got_start() && bot2.got_start()) break;
            std::this_thread::sleep_for(10ms);
        }
    }

    CHECK(bot1.got_welcome());
    CHECK(bot2.got_welcome());
    CHECK(bot1.got_start());
    CHECK(bot2.got_start());

    // Play the game.
    {
        auto deadline = std::chrono::steady_clock::now() + 15s;
        while (std::chrono::steady_clock::now() < deadline) {
            bot1.poll(); bot1.think();
            bot2.poll(); bot2.think();
            if (bot1.got_game_over() || bot2.got_game_over()) break;
            std::this_thread::sleep_for(10ms);
        }
    }

    CHECK((bot1.got_game_over() || bot2.got_game_over()));

    std::string winner;
    if (bot1.got_game_over()) winner = bot1.winner();
    else if (bot2.got_game_over()) winner = bot2.winner();
    CHECK_FALSE(winner.empty());

    if (bot1.got_game_over() && bot2.got_game_over())
        CHECK(bot1.winner() == bot2.winner());

    // Wait for lobby after game_over (post-game pause is 5 s).
    {
        auto deadline = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < deadline) {
            bot1.poll(); bot2.poll();
            if (bot1.got_lobby_after_game() || bot2.got_lobby_after_game()) break;
            std::this_thread::sleep_for(10ms);
        }
    }

    CHECK((bot1.got_lobby_after_game() || bot2.got_lobby_after_game()));

    bot1.disconnect(); bot2.disconnect();
    server.stop(); server_thread.join();
    ctf::net::cleanup_net();
}

// ── Test: Countdown abort on player drop ──────────────────────────────────

TEST_CASE("Countdown aborts when a player drops", "[integration][server]") {
    REQUIRE(ctf::net::init_net());

    ctf::server::Server server(0, true);
    REQUIRE(server.port() > 0);

    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(100ms);

    BotClient bot1("Alice");
    BotClient bot2("Bob");

    REQUIRE(bot1.connect("127.0.0.1", server.port()));
    REQUIRE(bot2.connect("127.0.0.1", server.port()));

    std::this_thread::sleep_for(200ms);
    bot1.send_join();
    bot2.send_join();

    {
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            bot1.poll(); bot2.poll();
            if (bot1.got_welcome() && bot2.got_welcome()) break;
            std::this_thread::sleep_for(10ms);
        }
    }
    CHECK(bot1.got_welcome());
    CHECK(bot2.got_welcome());

    // Disconnect bot2 — countdown should abort, no start.
    bot2.disconnect();
    std::this_thread::sleep_for(500ms);

    {
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            bot1.poll();
            std::this_thread::sleep_for(10ms);
        }
    }

    CHECK_FALSE(bot1.got_start());

    bot1.disconnect();
    server.stop(); server_thread.join();
    ctf::net::cleanup_net();
}

// ── Test: Disconnect carrier resets flag ──────────────────────────────────

TEST_CASE("Carrier disconnect resets flag to centre", "[integration][server]") {
    REQUIRE(ctf::net::init_net());

    ctf::server::Server server(0, true);
    REQUIRE(server.port() > 0);

    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(100ms);

    BotClient botA("Carrier");
    BotClient botB("Watcher");

    REQUIRE(botA.connect("127.0.0.1", server.port()));
    REQUIRE(botB.connect("127.0.0.1", server.port()));

    std::this_thread::sleep_for(200ms);
    botA.send_join();
    botB.send_join();

    {
        auto deadline = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < deadline) {
            botA.poll(); botB.poll();
            if (botA.got_start() && botB.got_start()) break;
            std::this_thread::sleep_for(10ms);
        }
    }
    REQUIRE(botA.got_start());

    // Move botA toward center and capture the flag.
    {
        auto deadline = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < deadline) {
            botA.poll(); botA.think();
            botB.poll();
            if (botA.last_flag_owner().has_value() &&
                botA.last_flag_owner().value() == botA.player_id()) break;
            std::this_thread::sleep_for(10ms);
        }
    }

    CHECK(botA.last_flag_owner().has_value());
    CHECK(botA.last_flag_owner().value() == botA.player_id());

    // Disconnect botA (the carrier).
    botA.disconnect();
    std::this_thread::sleep_for(300ms);

    // Poll botB — the next state should show flag free at centre.
    bool flag_reset = false;
    {
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            botB.poll();
            if (!botB.last_flag_owner().has_value() &&
                std::abs(botB.last_flag_x() - 500.0) < 1.0 &&
                std::abs(botB.last_flag_y() - 500.0) < 1.0) {
                flag_reset = true; break;
            }
            std::this_thread::sleep_for(10ms);
        }
    }
    CHECK(flag_reset);

    botB.disconnect();
    server.stop(); server_thread.join();
    ctf::net::cleanup_net();
}
