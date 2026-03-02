#include "Server.h"
#include <iostream>

int main() {
    Server server;
    if (!server.start()) {
        std::cerr << "Server start failed." << std::endl;
        return 1;
    }
    std::cout << "Chat server running on port 65432. Press Enter to stop." << std::endl;
    std::cin.get();
    server.stop();
    std::cout << "Server stopped." << std::endl;
    return 0;
}
