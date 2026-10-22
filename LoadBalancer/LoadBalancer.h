#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

#include "BackendManager.h"

class LoadBalancer {
public:
    bool Start(const std::string& configPath);
    void Stop();

private:
    bool LoadConfig(const std::string& configPath);
    void AcceptLoop();
    void UdpRelayLoop();

    int m_listenPort = 5000;
    int m_udpPort = 5001;
    int m_healthIntervalMs = 2000;
    int m_healthTimeoutMs = 1000;
    std::string m_logFile;

    SOCKET m_listenSocket = INVALID_SOCKET;
    SOCKET m_udpSocket = INVALID_SOCKET;
    std::atomic<bool> m_running{ false };
    std::thread m_acceptThread;
    std::thread m_udpThread;

    BackendManager m_manager;
    std::mutex m_udpRouteMutex;
    std::map<std::string, int> m_udpRouteByUser;
    std::map<std::string, std::string> m_udpBackendHost;
    std::map<std::string, sockaddr_in> m_udpClientAddr;
};
