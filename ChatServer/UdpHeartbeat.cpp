#include "UdpHeartbeat.h"

#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <ws2tcpip.h>

#include "../Common/JsonUtil.h"
#include "../Common/Logger.h"
#include "../Common/Protocol.h"

bool UdpHeartbeat::Start(const ServerConfig& config) {
    m_cfg = config;
    m_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_udpSocket == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(m_cfg.udpPort));

    if (bind(m_udpSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Logger::Error("UDP      : bind() failed on port " +
                      std::to_string(m_cfg.udpPort) + ": " +
                      std::to_string(WSAGetLastError()));
        closesocket(m_udpSocket);
        m_udpSocket = INVALID_SOCKET;
        return false;
    }

    m_running = true;
    m_thread = std::thread([this] { UdpLoop(); });
    Logger::Info("UDP      : Heartbeat listener active on port " +
                 std::to_string(m_cfg.udpPort));
    return true;
}

void UdpHeartbeat::Stop() {
    m_running = false;
    if (m_udpSocket != INVALID_SOCKET) {
        closesocket(m_udpSocket);
        m_udpSocket = INVALID_SOCKET;
    }
    if (m_thread.joinable()) m_thread.join();
}

void UdpHeartbeat::SetPulseCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pulseCallback = std::move(callback);
}

void UdpHeartbeat::NoteUserConnected(const std::string& userId, const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_users[userId] = { ip, now };
}

void UdpHeartbeat::CleanupExpired() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_users.begin(); it != m_users.end();) {
        if ((now - it->second.second) > m_cfg.inactivityTimeoutMs) {
            Logger::Warn("UDP      : Heartbeat timeout for " + it->first +
                         " (last seen " +
                         std::to_string((now - it->second.second) / 1000) + "s ago)");
            it = m_users.erase(it);
        } else {
            ++it;
        }
    }
}

void UdpHeartbeat::UdpLoop() {
    char buffer[2048] = {};
    auto lastCleanup = std::chrono::steady_clock::now();

    while (m_running) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(m_udpSocket, buffer, sizeof(buffer) - 1, 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) {
            if (!m_running) break;
            continue;
        }
        buffer[n] = '\0';

        std::map<std::string, std::string> hb;
        if (!JsonParse(buffer, hb)) continue;
        if (JsonGet(hb, "type") == "hb_ack") continue; // never loop replies

        std::string userId = JsonGet(hb, "userId");
        if (userId.empty()) continue;

        char ip[64] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));

        std::function<void(const std::string&, const std::string&)> callback;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            bool isNew = m_users.find(userId) == m_users.end();
            m_users[userId] = { ip, now };
            callback = m_pulseCallback;
            if (isNew) {
                Logger::Info("UDP      : Heartbeat received from " + userId + " (" + ip + ")");
            }
        }
        if (callback) callback(userId, ip);

        const std::string ack = JsonBuild({
            {"type", "hb_ack"},
            {"serverId", m_cfg.serverId},
            {"userId", userId},
            {"seq", JsonGet(hb, "seq", "0")},
            {"status", "online"},
            {"timestamp", protocol::IsoTimestampUtc()}
        });
        sendto(m_udpSocket, ack.data(), static_cast<int>(ack.size()), 0,
               reinterpret_cast<sockaddr*>(&from), fromLen);

        auto now = std::chrono::steady_clock::now();
        if (now - lastCleanup > std::chrono::seconds(1)) {
            CleanupExpired();
            lastCleanup = now;
        }
    }
}
