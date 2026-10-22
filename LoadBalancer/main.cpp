#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "../Common/Logger.h"
#include "LoadBalancer.h"

int main(int argc, char* argv[]) {
    std::string configPath = (argc > 1) ? argv[1] : "config/loadbalancer.ini";

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    LoadBalancer lb;
    if (!lb.Start(configPath)) {
        std::cerr << "Load balancer failed to start" << std::endl;
        WSACleanup();
        return 1;
    }

    std::cout << "Type 'q' + Enter for graceful shutdown." << std::endl;
    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cin.clear();
            continue;
        }
        if (line == "q" || line == "quit") break;
    }

    lb.Stop();
    WSACleanup();
    return 0;
}
