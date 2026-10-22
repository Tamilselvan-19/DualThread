#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

struct Backend {
    std::string name;
    std::string host;
    int tcpPort = 0;
    int udpPort = 0;
    std::atomic<bool> healthy{ true };
};

class BackendManager {
public:
    void AddBackend(std::unique_ptr<Backend> backend);
    Backend* NextHealthy();
    void StartHealthChecks(int intervalMs, int probeTimeoutMs);
    void Stop();
    const std::vector<std::unique_ptr<Backend>>& Backends() const { return m_backends; }

private:
    void HealthCheckLoop(int intervalMs, int probeTimeoutMs);
    static bool Probe(const Backend& backend, int timeoutMs);

    std::vector<std::unique_ptr<Backend>> m_backends;
    std::atomic<uint64_t> m_rr{ 0 };
    std::atomic<bool> m_running{ false };
    std::thread m_healthThread;
};
