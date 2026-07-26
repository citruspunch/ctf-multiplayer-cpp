#include <catch2/catch_test_macros.hpp>

#include "json.hpp"
#include "messages.hpp"
#include "net/platform.hpp"
#include "net/udp_socket.hpp"
#include "udp_discovery.hpp"

#include <atomic>
#include <cstring>
#include <thread>

// Dummy provider for testing.
struct TestProvider : public ctf::discovery::ServerStateProvider {
    auto get_server_name() const -> std::string override { return "TestServer"; }
    auto get_tcp_port() const -> int override { return 8889; }
    auto get_state() const -> std::string override { return "lobby"; }
    auto get_player_count() const -> int override { return 0; }
};

// Helper: send a raw UDP datagram to 127.0.0.1:port.
static void send_udp(ctf::net::UdpSocket& sock, int port,
                     const std::string& data) {
    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    sock.send_to(data, addr);
}

TEST_CASE("UDP discovery server responds to valid discover", "[net][discovery]") {
    REQUIRE(ctf::net::init_net());

    // Bind server socket to a random port (0 = OS assignment).
    auto server_sock = ctf::net::UdpSocket::bind(0);
    REQUIRE(server_sock);

    int port = server_sock.local_port();
    REQUIRE(port > 0);

    TestProvider provider;
    ctf::discovery::DiscoveryServer server(std::move(server_sock), provider);

    std::atomic<bool> keep_running{true};
    std::thread server_thread([&]() {
        while (keep_running.load()) {
            server.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Give server a moment to start listening.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ctf::discovery::DiscoveryClient client;
    auto result = client.discover_unicast("127.0.0.1", port, 2000);

    REQUIRE(result.has_value());
    CHECK(result->name == "TestServer");
    CHECK(result->tcp_port == 8889);
    CHECK(result->state == "lobby");
    CHECK(result->players == 0);

    keep_running.store(false);
    server_thread.join();
    ctf::net::cleanup_net();
}

TEST_CASE("UDP discovery silently discards invalid messages", "[net][discovery]") {
    REQUIRE(ctf::net::init_net());

    auto server_sock = ctf::net::UdpSocket::bind(0);
    REQUIRE(server_sock);

    int port = server_sock.local_port();
    REQUIRE(port > 0);

    TestProvider provider;
    ctf::discovery::DiscoveryServer server(std::move(server_sock), provider);

    std::atomic<bool> keep_running{true};
    std::thread server_thread([&]() {
        while (keep_running.load()) {
            server.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Create a separate socket to send test messages.
    auto test_sock = ctf::net::UdpSocket::make_broadcast();
    REQUIRE(test_sock);

    // 1. Send discover with v=2 — silently discarded, no response.
    {
        nlohmann::json j;
        j["type"] = "discover";
        j["v"] = 2;
        auto msg = ctf::json::dump_compact(j) + "\n";
        send_udp(test_sock, port, msg);
    }

    // 2. Send garbage — silently discarded, no response.
    {
        send_udp(test_sock, port, "this is not valid json\n");
    }

    // Wait for server to process (and potentially respond).
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Try to receive — should be nothing (EAGAIN / EWOULDBLOCK → -1).
    sockaddr_in sender{};
    char buf[1024];
    auto n = test_sock.recv_from(buf, sizeof(buf), sender);
    CHECK(n < 0);

    // 3. Now send a valid discover v=1 — should get a response.
    {
        nlohmann::json j;
        j["type"] = "discover";
        j["v"] = 1;
        auto msg = ctf::json::dump_compact(j) + "\n";
        send_udp(test_sock, port, msg);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    n = test_sock.recv_from(buf, sizeof(buf), sender);
    CHECK(n > 0);

    // Verify the response is a valid server_info.
    if (n > 0) {
        buf[n < static_cast<ssize_t>(sizeof(buf) - 1) ? n : sizeof(buf) - 1] = '\0';
        auto json = ctf::json::parse_line({buf, static_cast<std::size_t>(n)});
        REQUIRE(json.has_value());
        auto msg = ctf::msg::from_json(*json);
        auto* info = std::get_if<ctf::ServerInfo>(&msg);
        REQUIRE(info != nullptr);
        CHECK(info->name == "TestServer");
        CHECK(info->tcp_port == 8889);
    }

    keep_running.store(false);
    server_thread.join();
    ctf::net::cleanup_net();
}
