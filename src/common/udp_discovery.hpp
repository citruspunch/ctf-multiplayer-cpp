#pragma once

#include "net/udp_socket.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ctf::discovery {

// ── ServerStateProvider interface ────────────────────────────────────────
// Implemented by the server application to provide state for server_info
// responses.

class ServerStateProvider {
public:
    virtual ~ServerStateProvider() = default;

    virtual auto get_server_name() const -> std::string = 0;
    virtual auto get_tcp_port() const -> int = 0;
    virtual auto get_state() const -> std::string = 0;  // "lobby" | "playing"
    virtual auto get_player_count() const -> int = 0;
};

// ── DiscoveryServer ──────────────────────────────────────────────────────
// Listens on a UDP socket for "discover" messages and responds with
// "server_info" unicast to the requester.

class DiscoveryServer {
public:
    DiscoveryServer(net::UdpSocket socket, const ServerStateProvider& provider);

    // Non-copyable, movable (move constructor only — reference member prevents
    // defaulted move assignment).
    DiscoveryServer(const DiscoveryServer&) = delete;
    auto operator=(const DiscoveryServer&) -> DiscoveryServer& = delete;
    DiscoveryServer(DiscoveryServer&&) = default;
    auto operator=(DiscoveryServer&&) -> DiscoveryServer& = delete;

    // Process incoming discovery requests (non-blocking).
    // Call this periodically from the server event loop.
    void tick();

private:
    net::UdpSocket socket_;
    const ServerStateProvider& provider_;
};

// ── ServerEntry ──────────────────────────────────────────────────────────
// Represents a discovered server.

struct ServerEntry {
    std::string ip;
    std::string name;
    int tcp_port;
    std::string state;
    int players;
};

// ── DiscoveryClient ──────────────────────────────────────────────────────
// Sends broadcast "discover" messages and collects responses.

class DiscoveryClient {
public:
    DiscoveryClient() = default;

    // Non-copyable, movable.
    DiscoveryClient(const DiscoveryClient&) = delete;
    auto operator=(const DiscoveryClient&) -> DiscoveryClient& = delete;
    DiscoveryClient(DiscoveryClient&&) = default;
    auto operator=(DiscoveryClient&&) -> DiscoveryClient& = default;

    // Send "discover" to broadcast addresses and collect responses.
    // Sends to both 255.255.255.255:port and the subnet broadcast address.
    // Blocks for up to timeout_ms milliseconds waiting for responses.
    void discover(int port, int timeout_ms = 2000);

    // Send "discover" to a specific IP:port (unicast).
    // Blocks for up to timeout_ms milliseconds for a single response.
    auto discover_unicast(const std::string& ip, int port,
                          int timeout_ms = 1000) -> std::optional<ServerEntry>;

    // Access collected servers.
    auto servers() const -> const std::vector<ServerEntry>&;

    // Clear the list of discovered servers.
    void clear();

private:
    std::vector<ServerEntry> servers_;
};

// ── Utility ──────────────────────────────────────────────────────────────

// Parse "IP:port" format and return the IP and port.
// Returns nullopt if the format is invalid.
auto parse_ip_port(const std::string& s) -> std::optional<std::pair<std::string, int>>;

// Get the subnet broadcast address as a string.
// Returns "255.255.255.255" as fallback if detection fails.
auto subnet_broadcast() -> std::string;

}  // namespace ctf::discovery
