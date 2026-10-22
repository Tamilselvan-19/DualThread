
#include "LoadBalancer.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "../Common/Ini.h"
#include "../Common/JsonUtil.h"
#include "../Common/Logger.h"
#include "../Common/Protocol.h"
#include "TcpProxy.h"

namespace {

    std::string SocketAddressToString(const sockaddr_in& addr) {
        char ip[INET_ADDRSTRLEN] = {};

        if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == nullptr) {
            return "unknown";
        }

        return std::string(ip) + ":" +
               std::to_string(ntohs(addr.sin_port));
    }

    bool ConnectWithTimeout(
        SOCKET s,
        const sockaddr_in& addr,
        int timeoutMs
    ) {
        u_long nonBlocking = 1;

        if (ioctlsocket(s, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
            return false;
        }

        int rc = connect(
            s,
            reinterpret_cast<const sockaddr*>(&addr),
            sizeof(addr)
        );

        bool connected = false;

        if (rc == 0) {
            connected = true;
        } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(s, &writeSet);

            timeval timeout{
                timeoutMs / 1000,
                (timeoutMs % 1000) * 1000
            };

            int ready = select(
                0,
                nullptr,
                &writeSet,
                nullptr,
                &timeout
            );

            if (ready > 0) {
                int error = 0;
                int length = sizeof(error);

                if (getsockopt(
                        s,
                        SOL_SOCKET,
                        SO_ERROR,
                        reinterpret_cast<char*>(&error),
                        &length
                    ) == 0) {
                    connected = (error == 0);
                }
            }
        }

        u_long blocking = 0;
        ioctlsocket(s, FIONBIO, &blocking);

        if (connected) {
            int yes = 1;
            setsockopt(
                s,
                IPPROTO_TCP,
                TCP_NODELAY,
                reinterpret_cast<const char*>(&yes),
                sizeof(yes)
            );
        }

        return connected;
    }

    bool EndpointMatches(
        const sockaddr_in& from,
        const Backend& backend
    ) {
        if (backend.udpPort <= 0) {
            return false;
        }

        if (from.sin_port !=
            htons(static_cast<u_short>(backend.udpPort))) {
            return false;
        }

        sockaddr_in expected{};
        expected.sin_family = AF_INET;

        if (inet_pton(
                AF_INET,
                backend.host.c_str(),
                &expected.sin_addr
            ) != 1) {
            return false;
        }

        return from.sin_addr.s_addr == expected.sin_addr.s_addr;
    }
}

bool LoadBalancer::LoadConfig(const std::string& path) {
    m_listenPort = IniFile::ReadInt(
        path, "balancer", "ListenPort", 5000
    );

    m_udpPort = IniFile::ReadInt(
        path, "balancer", "UdpPort", 5001
    );

    m_healthIntervalMs = IniFile::ReadInt(
        path, "balancer", "HealthCheckIntervalMs", 2000
    );

    m_healthTimeoutMs = IniFile::ReadInt(
        path, "balancer", "HealthCheckTimeoutMs", 1000
    );

    m_logFile = IniFile::ReadValue(
        path, "balancer", "LogFile", "logs/loadbalancer.log"
    );

    for (const char* section :
         { "backend1", "backend2", "backend3", "backend4" }) {

        std::string name = IniFile::ReadValue(
            path, section, "Name", section
        );

        std::string host = IniFile::ReadValue(
            path, section, "Host", "127.0.0.1"
        );

        int tcp = IniFile::ReadInt(
            path, section, "TcpPort", 0
        );

        int udp = IniFile::ReadInt(
            path, section, "UdpPort", 0
        );

        if (tcp == 0) {
            continue;
        }

        auto backend = std::make_unique<Backend>();

        backend->name = name;
        backend->host = host;
        backend->tcpPort = tcp;
        backend->udpPort = udp;
        backend->healthy = true;

        m_manager.AddBackend(std::move(backend));
    }

    return !m_manager.Backends().empty();
}

bool LoadBalancer::Start(const std::string& configPath) {
    if (!LoadConfig(configPath)) {
        std::cerr << "No backends configured in "
                  << configPath << std::endl;
        return false;
    }

    Logger::Init(m_logFile);

    m_listenSocket = socket(
        AF_INET, SOCK_STREAM, IPPROTO_TCP
    );

    if (m_listenSocket == INVALID_SOCKET) {
        Logger::Error(
            "TCP: socket creation failed, error=" +
            std::to_string(WSAGetLastError())
        );
        return false;
    }

    int yes = 1;

    setsockopt(
        m_listenSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&yes),
        sizeof(yes)
    );

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(
        static_cast<u_short>(m_listenPort)
    );

    if (bind(
            m_listenSocket,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)
        ) == SOCKET_ERROR ||
        listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {

        Logger::Error(
            "TCP: bind/listen failed on port " +
            std::to_string(m_listenPort) +
            ", error=" + std::to_string(WSAGetLastError())
        );

        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    m_udpSocket = socket(
        AF_INET, SOCK_DGRAM, IPPROTO_UDP
    );

    if (m_udpSocket != INVALID_SOCKET) {
        sockaddr_in uaddr{};
        uaddr.sin_family = AF_INET;
        uaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        uaddr.sin_port = htons(
            static_cast<u_short>(m_udpPort)
        );

        if (bind(
                m_udpSocket,
                reinterpret_cast<sockaddr*>(&uaddr),
                sizeof(uaddr)
            ) == SOCKET_ERROR) {

            Logger::Error(
                "UDP: bind failed on port " +
                std::to_string(m_udpPort) +
                ", error=" + std::to_string(WSAGetLastError())
            );

            closesocket(m_udpSocket);
            m_udpSocket = INVALID_SOCKET;
        }
    } else {
        Logger::Error(
            "UDP: socket creation failed, error=" +
            std::to_string(WSAGetLastError())
        );
    }

    m_manager.StartHealthChecks(
        m_healthIntervalMs,
        m_healthTimeoutMs
    );

    m_running = true;

    m_acceptThread = std::thread(
        [this] { AcceptLoop(); }
    );

    if (m_udpSocket != INVALID_SOCKET) {
        m_udpThread = std::thread(
            [this] { UdpRelayLoop(); }
        );
    }

    Logger::Info(
        "Load balancer started on TCP " +
        std::to_string(m_listenPort) +
        " / UDP " + std::to_string(m_udpPort)
    );

    return true;
}

void LoadBalancer::Stop() {
    if (!m_running.exchange(false)) {
        return;
    }

    m_manager.Stop();

    if (m_listenSocket != INVALID_SOCKET) {
        shutdown(m_listenSocket, SD_BOTH);
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_udpSocket != INVALID_SOCKET) {
        closesocket(m_udpSocket);
        m_udpSocket = INVALID_SOCKET;
    }

    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }

    if (m_udpThread.joinable()) {
        m_udpThread.join();
    }

    TcpProxy::CloseAll();

    Logger::Info("Load balancer stopped.");
    Logger::Close();
}

void LoadBalancer::AcceptLoop() {
    while (m_running) {
        sockaddr_in from{};
        int fromLen = sizeof(from);

        SOCKET clientSocket = accept(
            m_listenSocket,
            reinterpret_cast<sockaddr*>(&from),
            &fromLen
        );

        if (clientSocket == INVALID_SOCKET) {
            if (m_running) {
                Logger::Error(
                    "TCP: accept() failed, error=" +
                    std::to_string(WSAGetLastError())
                );
            }
            break;
        }

        std::string clientIp = SocketAddressToString(from);

        Backend* backend = m_manager.NextHealthy();

        if (!backend) {
            Logger::Error(
                "Routing: no healthy backend available"
            );

            protocol::SendFrame(
                clientSocket,
                JsonBuild({
                    {"type", "error"},
                    {"message",
                     "No healthy chat server is available"}
                })
            );

            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            continue;
        }

        SOCKET backendSocket = socket(
            AF_INET, SOCK_STREAM, IPPROTO_TCP
        );

        if (backendSocket == INVALID_SOCKET) {
            Logger::Error(
                "Routing: backend socket creation failed"
            );
            closesocket(clientSocket);
            continue;
        }

        sockaddr_in backendAddr{};
        backendAddr.sin_family = AF_INET;
        backendAddr.sin_port = htons(
            static_cast<u_short>(backend->tcpPort)
        );

        if (inet_pton(
                AF_INET,
                backend->host.c_str(),
                &backendAddr.sin_addr
            ) != 1 ||
            !ConnectWithTimeout(
                backendSocket, backendAddr, 3000
            )) {

            Logger::Error(
                "Routing: could not connect to backend " +
                backend->name
            );

            backend->healthy = false;
            closesocket(backendSocket);

            protocol::SendFrame(
                clientSocket,
                JsonBuild({
                    {"type", "error"},
                    {"message", "Chat server is unavailable"}
                })
            );

            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            continue;
        }

        std::string firstFrame;
        bool authFrame = false;

        if (protocol::RecvFrame(
                clientSocket, firstFrame, 10000
            )) {

            std::map<std::string, std::string> auth;

            if (JsonParse(firstFrame, auth) &&
                JsonGet(auth, "type") == "auth") {

                authFrame = true;

                std::string uid = JsonGet(auth, "userId");

                if (!uid.empty()) {
                    std::lock_guard<std::mutex> lock(
                        m_udpRouteMutex
                    );

                    m_udpRouteByUser[uid] = backend->udpPort;
                    m_udpBackendHost[uid] = backend->host;

                    Logger::Info(
                        "UDP route registered: user=" + uid +
                        " backend=" + backend->name +
                        " endpoint=" + backend->host + ":" +
                        std::to_string(backend->udpPort)
                    );
                }
            }
        }

        if (!authFrame ||
            !protocol::SendFrame(backendSocket, firstFrame)) {

            Logger::Warn(
                "Routing: client did not send a valid auth frame"
            );

            shutdown(backendSocket, SD_BOTH);
            closesocket(backendSocket);

            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            continue;
        }

        DWORD noTimeout = 0;

        setsockopt(
            clientSocket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&noTimeout),
            sizeof(noTimeout)
        );

        setsockopt(
            backendSocket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&noTimeout),
            sizeof(noTimeout)
        );

        auto session = std::make_shared<ProxySession>();

        session->clientSocket = clientSocket;
        session->backendSocket = backendSocket;
        session->clientIp = clientIp;
        session->backendName = backend->name;

        Logger::Info(
            "Routing: " + clientIp +
            " -> " + backend->name
        );

        TcpProxy::StartProxyPair(session);
    }
}

void LoadBalancer::UdpRelayLoop() {
    char buffer[2048];

    while (m_running) {
        sockaddr_in from{};
        int fromLen = sizeof(from);

        int n = recvfrom(
            m_udpSocket,
            buffer,
            sizeof(buffer) - 1,
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLen
        );

        if (n <= 0) {
            if (!m_running) {
                break;
            }
            continue;
        }

        buffer[n] = '\0';

        const std::string source =
            SocketAddressToString(from);

        std::map<std::string, std::string> hb;

        if (!JsonParse(buffer, hb)) {
            Logger::Warn(
                "UDP: invalid JSON from " + source
            );
            continue;
        }

        const std::string userId = JsonGet(hb, "userId");
        const std::string type = JsonGet(hb, "type");
        const std::string seq = JsonGet(hb, "seq");
        const std::string serverId = JsonGet(hb, "serverId");

        if (userId.empty()) {
            Logger::Warn(
                "UDP: packet has no userId from " + source
            );
            continue;
        }

        bool fromBackend = false;
        std::string backendName;

        for (const auto& backend : m_manager.Backends()) {
            if (backend && EndpointMatches(from, *backend)) {
                fromBackend = true;
                backendName = backend->name;
                break;
            }
        }

        if (fromBackend) {
            sockaddr_in clientAddr{};
            bool found = false;

            {
                std::lock_guard<std::mutex> lock(
                    m_udpRouteMutex
                );

                auto it = m_udpClientAddr.find(userId);

                if (it != m_udpClientAddr.end()) {
                    clientAddr = it->second;
                    found = true;
                }
            }

            if (!found) {
                Logger::Warn(
                    "UDP ACK: no client address for user=" +
                    userId + " server=" + backendName +
                    " seq=" + seq
                );
                continue;
            }

            const int sent = sendto(
                m_udpSocket,
                buffer,
                n,
                0,
                reinterpret_cast<sockaddr*>(&clientAddr),
                sizeof(clientAddr)
            );

            if (sent == SOCKET_ERROR) {
                Logger::Warn(
                    "UDP ACK: send failed user=" + userId +
                    " server=" + backendName +
                    " seq=" + seq +
                    " error=" +
                    std::to_string(WSAGetLastError())
                );
            } else {
                Logger::Info(
                    "UDP ACK: server=" + backendName +
                    " user=" + userId +
                    " seq=" + seq +
                    " client=" +
                    SocketAddressToString(clientAddr) +
                    " sent=" + std::to_string(sent)
                );
            }

            continue;
        }

        int port = 0;
        std::string host;

        {
            std::lock_guard<std::mutex> lock(
                m_udpRouteMutex
            );

            auto it = m_udpRouteByUser.find(userId);

            if (it == m_udpRouteByUser.end()) {
                Logger::Warn(
                    "UDP HB: no backend route for user=" +
                    userId + " client=" + source
                );
                continue;
            }

            port = it->second;

            auto hostIt = m_udpBackendHost.find(userId);

            if (hostIt == m_udpBackendHost.end()) {
                Logger::Warn(
                    "UDP HB: no backend host for user=" +
                    userId
                );
                continue;
            }

            host = hostIt->second;
            m_udpClientAddr[userId] = from;
        }

        if (port <= 0) {
            Logger::Warn(
                "UDP HB: invalid backend port for user=" +
                userId
            );
            continue;
        }

        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(
            static_cast<u_short>(port)
        );

        if (inet_pton(
                AF_INET,
                host.c_str(),
                &dest.sin_addr
            ) != 1) {

            Logger::Warn(
                "UDP HB: invalid backend host=" + host +
                " user=" + userId
            );
            continue;
        }

        const int sent = sendto(
            m_udpSocket,
            buffer,
            n,
            0,
            reinterpret_cast<sockaddr*>(&dest),
            sizeof(dest)
        );

        if (sent == SOCKET_ERROR) {
            Logger::Warn(
                "UDP HB: forward failed user=" + userId +
                " backend=" + host + ":" +
                std::to_string(port) +
                " error=" +
                std::to_string(WSAGetLastError())
            );
        } else {
            Logger::Info(
                "UDP HB: user=" + userId +
                " type=" + type +
                " seq=" + seq +
                " client=" + source +
                " backend=" + host + ":" +
                std::to_string(port) +
                " sent=" + std::to_string(sent)
            );
        }
    }
}
