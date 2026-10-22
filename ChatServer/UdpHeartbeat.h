#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

#include "ServerConfig.h"

// Receives UDP heartbeat packets from clients (through the load balancer),
// tracks liveness and replies with a heartbeat acknowledgement. The reply is
// used by the client to report real round-trip health instead of guessing.
class UdpHeartbeat {
public:
    bool Start(const ServerConfig& config);
    void Stop();
    void NoteUserConnected(const std::string& userId, const std::string& ip);
    void SetPulseCallback(std::function<void(const std::string&, const std::string&)> callback);

private:
    void UdpLoop();
    void CleanupExpired();

    ServerConfig m_cfg;
    SOCKET m_udpSocket = INVALID_SOCKET;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::map<std::string, std::pair<std::string, long long>> m_users;
    std::mutex m_mutex;
    std::function<void(const std::string&, const std::string&)> m_pulseCallback;
};
