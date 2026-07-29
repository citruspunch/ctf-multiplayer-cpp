#pragma once

#include "framing.hpp"
#include "gui_helpers.hpp"
#include "input.hpp"
#include "messages.hpp"
#include "net/tcp_socket.hpp"
#include "udp_discovery.hpp"

#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ctf::client {

// Client screens / state machine.
enum class ClientState {
    Discovery,
    JoinName,
    Lobby,
    Countdown,
    Playing,
    GameOver,
    Disconnected,
};

// Playable CTF client with a Raylib UI.
//
// Screen flow:
//   Discovery  → find servers (broadcast / manual unicast / direct IP:port)
//   JoinName   → TCP connect + send join, retry on NAME_INVALID
//   Lobby      → player table until countdown starts
//   Countdown  → big number until 'start'
//   Playing    → game view (full rendering in a later task)
//   GameOver   → winner announcement, back to lobby on server 'lobby'
//   Disconnected → TCP close detected, back to discovery
class Client {
public:
    Client();
    ~Client();

    Client(const Client&) = delete;
    auto operator=(const Client&) -> Client& = delete;

    // Open the window and run the main loop until it closes.
    void run();

private:
    // ── Networking ──────────────────────────────────────────────────
    void connect_to(const std::string& ip, int port);
    void poll_network();
    void flush_send();
    void send_message(const nlohmann::json& j);
    void handle_message(const std::string& line);
    void on_server_disconnected(const std::string& reason);
    void return_to_discovery(const std::string& status);

    // ── Discovery actions ───────────────────────────────────────────
    void start_broadcast_discovery();
    void start_manual_discovery(const std::string& ip);
    void check_discovery_futures();

    // ── Per-state update / draw ─────────────────────────────────────
    void update();
    void draw();

    void update_discovery();
    void draw_discovery();

    void update_join_name();
    void draw_join_name();

    void update_lobby();
    void draw_lobby();

    void update_countdown();
    void draw_countdown();

    void update_playing();
    void draw_playing();

    void update_game_over();
    void draw_game_over();

    void update_disconnected();
    void draw_disconnected();

    // ── UI helpers ──────────────────────────────────────────────────
    // Text input field; captures printable chars while focused.
    struct TextField {
        std::string text;
        bool        focused{false};
        int         max_len{64};
    };

    static auto button(const char* label, float x, float y, float w,
                       float h) -> bool;
    static void text_field(TextField& field, float x, float y, float w,
                           float h);
    static void draw_spinner(float cx, float cy, const char* label);

    // Look up a player's display name from the lobby roster, falling
    // back to the id when the name is unknown.
    auto name_of(const std::string& id) const -> std::string;

    // ── State ───────────────────────────────────────────────────────
    ClientState state_{ClientState::Discovery};

    // Networking
    net::TcpSocket socket_;
    LineBuffer     recv_buf_;
    std::string    send_buf_;
    bool           window_open_{false};

    // Discovery
    discovery::DiscoveryClient                        discovery_;
    std::future<void>                                 broadcast_future_;
    std::future<std::optional<discovery::ServerEntry>> unicast_future_;
    bool                                              discovering_{false};
    int                                               selected_server_{-1};
    TextField                                         manual_ip_{};
    TextField                                         direct_addr_{};
    std::string                                       discovery_status_;

    // Join
    TextField   name_field_{};
    std::string join_error_;

    // Session data (filled from server messages)
    std::string              player_id_;
    std::optional<Config>    config_;
    std::vector<PlayerInfo>  lobby_players_;
    int                      countdown_seconds_{0};
    std::string              winner_id_;
    std::optional<State>     latest_state_;
    std::string              disconnect_reason_;
    std::string              status_line_;

    // Game (Playing)
    InputSampler                          input_;
    double                                match_start_time_{0.0};
    double                                match_end_time_{0.0};
    std::map<std::string, std::string>    player_names_;  // id → name
    std::map<std::string, ctf::Player>    known_players_;  // last state
    std::string                           departure_notice_;
    double                                departure_notice_time_{0.0};

    // Countdown screen particles
    bool                                  countdown_particles_init_{false};
    std::vector<gui::Particle>            countdown_particles_;
};

}  // namespace ctf::client
