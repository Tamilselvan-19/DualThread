#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

struct ProxySession {
    SOCKET clientSocket = INVALID_SOCKET;
    SOCKET backendSocket = INVALID_SOCKET;
    std::string clientIp;
    std::string backendName;
    std::atomic<bool> closed{false};
    std::mutex closeMutex;

    void Close();
};

class TcpProxy {
public:
    static void StartProxyPair(std::shared_ptr<ProxySession> session);
    static void CloseAll();
};
