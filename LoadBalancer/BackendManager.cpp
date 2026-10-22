
#include "BackendManager.h"

#include <chrono>
#include <string>
#include <ws2tcpip.h>

#include "../Common/Logger.h"

void BackendManager::AddBackend(std::unique_ptr<Backend> backend) {
    if (backend) {
        m_backends.push_back(std::move(backend));
    }
}

Backend* BackendManager::NextHealthy() {
    const size_t count = m_backends.size();

    if (count == 0) {
        return nullptr;
    }

    const size_t start =
        static_cast<size_t>(m_rr.fetch_add(1)) % count;

    for (size_t i = 0; i < count; ++i) {
        const size_t idx = (start + i) % count;
        Backend* backend = m_backends[idx].get();

        if (backend && backend->healthy.load()) {
            return backend;
        }
    }

    return nullptr;
}

bool BackendManager::Probe(const Backend& backend, int timeoutMs) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (s == INVALID_SOCKET) {
        const int error = WSAGetLastError();

        Logger::Error(
            "Probe failed: backend=" + backend.name +
            ", stage=socket, WSA error=" + std::to_string(error)
        );

        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(backend.tcpPort));

    if (inet_pton(AF_INET, backend.host.c_str(), &addr.sin_addr) != 1) {
        Logger::Error(
            "Probe failed: backend=" + backend.name +
            ", stage=inet_pton, invalid IPv4 address=" + backend.host
        );

        closesocket(s);
        return false;
    }

    u_long nonBlocking = 1;

    if (ioctlsocket(s, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        const int error = WSAGetLastError();

        Logger::Error(
            "Probe failed: backend=" + backend.name +
            ", stage=ioctlsocket, WSA error=" + std::to_string(error)
        );

        closesocket(s);
        return false;
    }

    bool ok = false;

    const int rc = connect(
        s,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );

    const int connectError =
        (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;

    if (rc == 0) {
        ok = true;
    } else if (
        connectError == WSAEWOULDBLOCK ||
        connectError == WSAEINPROGRESS ||
        connectError == WSAEINVAL
    ) {
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(s, &writeSet);

        timeval timeout{};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;

        const int ready = select(
            0,
            nullptr,
            &writeSet,
            nullptr,
            &timeout
        );

        if (ready > 0 && FD_ISSET(s, &writeSet)) {
            int socketError = 0;
            int length = sizeof(socketError);

            if (getsockopt(
                    s,
                    SOL_SOCKET,
                    SO_ERROR,
                    reinterpret_cast<char*>(&socketError),
                    &length
                ) == 0) {
                ok = (socketError == 0);

                if (!ok) {
                    Logger::Error(
                        "Probe failed: backend=" + backend.name +
                        ", stage=connect completion, socket error=" +
                        std::to_string(socketError)
                    );
                }
            } else {
                const int error = WSAGetLastError();

                Logger::Error(
                    "Probe failed: backend=" + backend.name +
                    ", stage=getsockopt, WSA error=" +
                    std::to_string(error)
                );
            }
        } else {
            const int error =
                (ready == SOCKET_ERROR) ? WSAGetLastError() : 0;

            Logger::Error(
                "Probe failed: backend=" + backend.name +
                ", stage=select, result=" + std::to_string(ready) +
                ", WSA error=" + std::to_string(error) +
                (ready == 0 ? ", reason=timeout" : "")
            );
        }
    } else {
        Logger::Error(
            "Probe failed: backend=" + backend.name +
            ", stage=connect, WSA error=" +
            std::to_string(connectError)
        );
    }

    closesocket(s);
    return ok;
}

void BackendManager::StartHealthChecks(
    int intervalMs,
    int probeTimeoutMs
) {
    if (m_running.exchange(true)) {
        return;
    }

    m_healthThread = std::thread(
        [this, intervalMs, probeTimeoutMs]() {
            HealthCheckLoop(intervalMs, probeTimeoutMs);
        }
    );
}

void BackendManager::Stop() {
    m_running = false;

    if (m_healthThread.joinable()) {
        m_healthThread.join();
    }
}

void BackendManager::HealthCheckLoop(
    int intervalMs,
    int probeTimeoutMs
) {
    while (m_running) {
        for (auto& item : m_backends) {
            if (!m_running) {
                break;
            }

            if (!item) {
                continue;
            }

            Backend* backend = item.get();

            const bool ok = Probe(*backend, probeTimeoutMs);

            // Temporary diagnostics: log every probe result.
            Logger::Info(
                "Health diagnostic: backend=" + backend->name +
                ", host=" + backend->host +
                ", tcpPort=" + std::to_string(backend->tcpPort) +
                ", probeResult=" + (ok ? "SUCCESS" : "FAILURE")
            );

            const bool previous = backend->healthy.exchange(ok);

            if (previous != ok) {
                if (ok) {
                    Logger::Info(
                        "Health: " + backend->name + " is healthy"
                    );
                } else {
                    Logger::Error(
                        "Health: " + backend->name +
                        " is unavailable; new clients will use a healthy backend"
                    );
                }
            }
        }

        // Sleep in short intervals so shutdown can be noticed.
        const int sleepMs = (intervalMs > 0) ? intervalMs : 200;

        for (int elapsed = 0;
             elapsed < sleepMs && m_running;
             elapsed += 200) {
            const int remaining = sleepMs - elapsed;
            const int waitMs = (remaining < 200) ? remaining : 200;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(waitMs)
            );
        }
    }
}
