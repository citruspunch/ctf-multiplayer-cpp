#include "net/platform.hpp"
#include "server/server.hpp"
#include "client/client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " --server [port] | --client\n";
        return 1;
    }

    std::string mode{argv[1]};

    if (mode == "--server") {
        if (!ctf::net::init_net()) {
            std::cerr << "Failed to initialise networking.\n";
            return 1;
        }

        int port = ctf::constants::default_tcp_port;
        if (argc >= 3) {
            port = std::stoi(argv[2]);
        }

        std::cout << "Starting server on TCP port " << port << "...\n";
        ctf::server::Server server{port};
        server.run();

        ctf::net::cleanup_net();
    } else if (mode == "--client") {
        if (!ctf::net::init_net()) {
            std::cerr << "Failed to initialise networking.\n";
            return 1;
        }

        ctf::client::Client client;
        client.run();

        ctf::net::cleanup_net();
    } else {
        std::cerr << "Error: expected --server or --client, got: " << mode << "\n";
        return 1;
    }

    return 0;
}
