#include "platform.hpp"

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

}  // namespace ctf::net
