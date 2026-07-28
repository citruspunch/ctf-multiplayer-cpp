#include "udp_discovery.hpp"
#include "constants.hpp"
#include "json.hpp"
#include "messages.hpp"

#include <cctype>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "iphlpapi")
#else
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <netinet/in.h>
#endif

namespace ctf::discovery {

// ── Internal helpers ─────────────────────────────────────────────────────

static auto make_sockaddr(const std::string& ip, int port) -> sockaddr_in {
    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    return addr;
}

static auto build_discover_msg() -> std::string {
    nlohmann::json j;
    j["type"] = "discover";
    j["v"] = 1;
    return ctf::json::dump_compact(j) + "\n";
}

// ── DiscoveryServer ──────────────────────────────────────────────────────

DiscoveryServer::DiscoveryServer(net::UdpSocket socket,
                                 const ServerStateProvider& provider)
    : socket_(std::move(socket))
    , provider_(provider)
{}

void DiscoveryServer::tick() {
    char buf[ctf::constants::message_max_size];

    for (;;) {
        sockaddr_in sender{};
        auto n = socket_.recv_from(buf, sizeof(buf) - 1, sender);
        if (n <= 0) break;  // No more data (EAGAIN) or error

        buf[n] = '\0';

        // Silently discard oversized datagrams (> 64 KB).
        if (static_cast<std::size_t>(n) >= ctf::constants::message_max_size) {
            continue;
        }

        // Parse JSON.
        auto json = ctf::json::parse_line({buf, static_cast<std::size_t>(n)});
        if (!json) continue;  // Silently discard invalid JSON.

        // Validate: must be "discover" with v == 1.
        auto msg = ctf::msg::from_json(*json);
        auto* discover = std::get_if<ctf::Discover>(&msg);
        if (!discover || discover->v != 1) continue;

        // Build server_info response.
        ctf::ServerInfo info;
        info.v = 1;
        info.name = provider_.get_server_name();
        info.tcp_port = provider_.get_tcp_port();
        info.state = provider_.get_state();
        info.players = provider_.get_player_count();

        auto payload = ctf::json::dump_compact(ctf::msg::to_json(info)) + "\n";
        socket_.send_to(payload, sender);
    }
}

// ── DiscoveryClient ──────────────────────────────────────────────────────

void DiscoveryClient::discover(int port, int timeout_ms) {
    auto sock = net::UdpSocket::make_broadcast();
    if (!sock) return;

    auto msg = build_discover_msg();

    // Send to limited broadcast.
    auto bc_addr = make_sockaddr("255.255.255.255", port);
    sock.send_to(msg, bc_addr);

    // Send to subnet broadcast.
    auto subnet = subnet_broadcast();
    if (subnet != "255.255.255.255") {
        auto subnet_addr = make_sockaddr(subnet, port);
        sock.send_to(msg, subnet_addr);
    }

    // Poll for responses.
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeout_ms);

    // Set non-blocking manually for the recv_from loop.
    char buf[ctf::constants::message_max_size];

    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) break;

        sockaddr_in sender{};
        auto n = sock.recv_from(buf, sizeof(buf) - 1, sender);
        if (n < 0) {
            // EAGAIN — small sleep to avoid busy-wait.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (n == 0) continue;

        buf[n] = '\0';
        if (static_cast<std::size_t>(n) >= ctf::constants::message_max_size) {
            continue;
        }

        auto json = ctf::json::parse_line({buf, static_cast<std::size_t>(n)});
        if (!json) continue;

        auto msg_variant = ctf::msg::from_json(*json);
        auto* info = std::get_if<ctf::ServerInfo>(&msg_variant);
        if (!info || info->v != 1) continue;

        // Get sender IP.
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, ip_str, sizeof(ip_str));

        // Deduplicate by IP:port — dual broadcast may elicit two
        // responses from the same server.
        bool duplicate = false;
        for (const auto& existing : servers_) {
            if (existing.ip == ip_str && existing.tcp_port == info->tcp_port) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        ServerEntry entry;
        entry.ip       = ip_str;
        entry.name     = info->name;
        entry.tcp_port = info->tcp_port;
        entry.state    = info->state;
        entry.players  = info->players;
        servers_.push_back(std::move(entry));
    }
}

auto DiscoveryClient::discover_unicast(const std::string& ip, int port,
                                        int timeout_ms)
    -> std::optional<ServerEntry>
{
    auto sock = net::UdpSocket::make_broadcast();
    if (!sock) return std::nullopt;

    auto msg = build_discover_msg();
    auto addr = make_sockaddr(ip, port);
    sock.send_to(msg, addr);

    // Wait for a response.
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeout_ms);

    char buf[ctf::constants::message_max_size];

    while (std::chrono::steady_clock::now() < deadline) {
        sockaddr_in sender{};
        auto n = sock.recv_from(buf, sizeof(buf) - 1, sender);
        if (n < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (n == 0) continue;

        buf[n] = '\0';
        if (static_cast<std::size_t>(n) >= ctf::constants::message_max_size) {
            continue;
        }

        auto json = ctf::json::parse_line({buf, static_cast<std::size_t>(n)});
        if (!json) continue;

        auto msg_variant = ctf::msg::from_json(*json);
        auto* info = std::get_if<ctf::ServerInfo>(&msg_variant);
        if (!info || info->v != 1) continue;

        // Verify the response came from the expected IP.
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, ip_str, sizeof(ip_str));

        ServerEntry entry;
        entry.ip       = ip_str;
        entry.name     = info->name;
        entry.tcp_port = info->tcp_port;
        entry.state    = info->state;
        entry.players  = info->players;
        return entry;
    }

    return std::nullopt;
}

auto DiscoveryClient::servers() const -> const std::vector<ServerEntry>& {
    return servers_;
}

void DiscoveryClient::clear() {
    servers_.clear();
}

// ── Utility ──────────────────────────────────────────────────────────────

auto parse_ip_port(const std::string& s)
    -> std::optional<std::pair<std::string, int>>
{
    auto colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == s.size() - 1) {
        return std::nullopt;
    }

    std::string ip = s.substr(0, colon);
    std::string port_str = s.substr(colon + 1);

    // Validate port is numeric.
    for (char c : port_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
    }

    int port = std::stoi(port_str);
    if (port < 1 || port > 65535) return std::nullopt;

    return std::make_pair(ip, port);
}

auto subnet_broadcast() -> std::string {
#ifdef _WIN32
    // Fallback: return the limited broadcast.
    // A full implementation would use GetAdaptersInfo().
    return "255.255.255.255";
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return "255.255.255.255";
    }

    std::string result = "255.255.255.255";

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        // Skip loopback.
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_BROADCAST)) continue;
        if (ifa->ifa_broadaddr == nullptr) continue;

        auto* bc = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
        char ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &bc->sin_addr, ip, sizeof(ip))) {
            result = ip;
            break;  // Take the first non-loopback broadcast.
        }
    }

    freeifaddrs(ifaddr);
    return result;
#endif
}

}  // namespace ctf::discovery
