#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " --server | --client\n";
        return 1;
    }

    std::string mode{argv[1]};

    if (mode == "--server") {
        std::cout << "server mode\n";
    } else if (mode == "--client") {
        std::cout << "client mode\n";
    } else {
        std::cerr << "Error: expected --server or --client, got: " << mode << "\n";
        return 1;
    }

    return 0;
}
