#include "platform.hpp"

#include <cstring>

#ifdef __APPLE__
#include <ifaddrs.h>
#include <net/if.h>
#endif

#ifdef __linux__
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace ctf::net {

auto init_net() -> bool {
#ifdef _WIN32
    static bool called = false;
    if (!called) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        called = true;
    }
    return true;
#else
    return true;
#endif
}

void cleanup_net() {
#ifdef _WIN32
    WSACleanup();
#endif
}

auto get_local_ipv4() -> std::string {
#ifdef _WIN32
    // Windows: use gethostname + getaddrinfo as a portable fallback.
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints, *info = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hostname, nullptr, &hints, &info) == 0) {
            for (struct addrinfo* p = info; p != nullptr; p = p->ai_next) {
                if (p->ai_family == AF_INET) {
                    auto* sa = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
                    char ip[INET_ADDRSTRLEN];
                    if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) {
                        // Skip loopback
                        if (std::strcmp(ip, "127.0.0.1") != 0) {
                            std::string result(ip);
                            freeaddrinfo(info);
                            return result;
                        }
                    }
                }
            }
            freeaddrinfo(info);
        }
    }
    return "127.0.0.1";
#else
    // macOS / Linux: use getifaddrs to enumerate interfaces.
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return "127.0.0.1";
    }

    std::string result = "127.0.0.1";
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        // Skip loopback interfaces.
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        char ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) {
            result = ip;
            break;  // Take first non-loopback IPv4.
        }
    }

    freeifaddrs(ifaddr);
    return result;
#endif
}

}  // namespace ctf::net
