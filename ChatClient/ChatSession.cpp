#include "ChatSession.h"

#include <algorithm>
#include <chrono>
#include <map>

#include <ws2tcpip.h>

#include "../Common/JsonUtil.h"
#include "../Common/Protocol.h"

namespace {
    bool ConnectWithTimeout(SOCKET s, const sockaddr_in& addr, int timeoutMs) {
        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);

        int rc = connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        bool connected = false;
        if (rc == 0) {
            connected = true;
        } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(s, &writeSet);
            timeval timeout{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
            int ready = select(0, nullptr, &writeSet, nullptr, &timeout);
            if (ready > 0) {
                int error = 0;
                int length = sizeof(error);
                if (getsockopt(s, SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char*>(&error), &length) == 0) {
                    connected = (error == 0);
                }
            }
        }

        u_long blocking = 0;
        ioctlsocket(s, FIONBIO, &blocking);
        if (connected) {
            int yes = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&yes), sizeof(yes));
        }
        return connected;
    }

    std::string FriendlySocketError() {
        int code = WSAGetLastError();
        switch (code) {
        case WSAECONNREFUSED: return "The server refused the connection.";
        case WSAETIMEDOUT: return "The connection timed out.";
        case WSAENETUNREACH: return "The network is unreachable.";
        case WSAEHOSTUNREACH: return "The server host is unreachable.";
        default: return "Socket error " + std::to_string(code) + ".";
        }
    }
}

ChatSession::ChatSession() {}

ChatSession::~ChatSession() {
    Stop();
}

bool ChatSession::ConnectBlocking(const std::string& host, int port, int udpPort,
                                  const std::string& userId, const std::string& password,
                                  int heartbeatMs, std::string& error) {
    m_host = host;
    m_port = port;
    m_udpPort = udpPort;
    m_userId = userId;
    m_password = password;
    m_heartbeatMs = heartbeatMs > 1000 ? heartbeatMs : 5000;
    m_fatalAuth = false;
    error.clear();
    return ConnectInternal(error);
}

bool ChatSession::ConnectInternal(std::string& error) {
    error.clear();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(m_port));
    if (inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) != 1) {
        error = "Enter a valid IPv4 server address.";
        m_lastError = error;
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = "Could not create a TCP socket.";
        m_lastError = error;
        return false;
    }

    if (!ConnectWithTimeout(s, addr, 5000)) {
        error = FriendlySocketError();
        m_lastError = error;
        closesocket(s);
        return false;
    }

    DWORD sendTimeout = 5000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));

    const std::string auth = JsonBuild({
        {"type", "auth"},
        {"userId", m_userId},
        {"password", m_password}
    });
    if (!protocol::SendFrame(s, auth)) {
        error = "Could not send the authentication request.";
        m_lastError = error;
        closesocket(s);
        return false;
    }

    std::string frame;
    if (!protocol::RecvFrame(s, frame, 10000)) {
        error = "No authentication response from the server.";
        m_lastError = error;
        closesocket(s);
        return false;
    }

    std::map<std::string, std::string> response;
    if (!JsonParse(frame, response)) {
        error = "The server returned an invalid authentication response.";
        m_lastError = error;
        closesocket(s);
        return false;
    }

    if (JsonGet(response, "type") != "auth_ok") {
        std::string message = JsonGet(response, "message", "Authentication failed.");
        if (message.find("Invalid credentials") != std::string::npos) {
            m_fatalAuth = true;
        }
        error = message;
        m_lastError = error;
        closesocket(s);
        return false;
    }

    DWORD noTimeout = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&noTimeout), sizeof(noTimeout));

    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        if (m_socket != INVALID_SOCKET) {
            shutdown(m_socket, SD_BOTH);
            closesocket(m_socket);
        }
        m_socket = s;
    }
    {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_serverId = JsonGet(response, "serverId", "server");
    }
    m_connected = true;
    return true;
}

void ChatSession::Start(HWND notifyWindow) {
    m_notify = notifyWindow;
    if (m_running.exchange(true)) return;
    m_networkThread = std::thread([this] { NetworkLoop(); });
    m_heartbeatThread = std::thread([this] { HeartbeatLoop(); });
}

SOCKET ChatSession::CurrentSocket() {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    return m_socket;
}

void ChatSession::DropSocket() {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (m_socket != INVALID_SOCKET) {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_connected = false;
}

void ChatSession::NetworkLoop() {
    bool firstAttempt = true;
    int backoffSeconds = 1;

    while (m_running) {
        if (CurrentSocket() == INVALID_SOCKET) {
            PostStatus(firstAttempt ? SESSION_CONNECTING : SESSION_RECONNECTING,
                       firstAttempt ? "Connecting to DualThread..." : "Reconnecting...");
            std::string error;
            if (!ConnectInternal(error)) {
                if (!m_running) break;
                if (m_fatalAuth) {
                    PostStatus(SESSION_AUTH_FAILED, error);
                    break;
                }
                PostStatus(SESSION_DISCONNECTED, error + " Reconnecting...");
                for (int i = 0; i < backoffSeconds * 10 && m_running; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                backoffSeconds = (std::min)(backoffSeconds * 2, 10);
                firstAttempt = false;
                continue;
            }
            backoffSeconds = 1;
            firstAttempt = false;
            std::string server;
            {
                std::lock_guard<std::mutex> lock(m_metaMutex);
                server = m_serverId;
            }
            PostStatus(SESSION_CONNECTED, server);
        }

        SOCKET s = CurrentSocket();
        if (s == INVALID_SOCKET) continue;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s, &readSet);
        timeval timeout{0, 500000};
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (!m_running) break;
        if (ready == SOCKET_ERROR) {
            DropSocket();
            if (m_running) PostStatus(SESSION_DISCONNECTED, "Connection lost. Reconnecting...");
            continue;
        }
        if (ready == 0) continue;

        std::string frame;
        if (!protocol::RecvFrame(s, frame, 0)) {
            DropSocket();
            if (m_running) {
                PostStatus(SESSION_DISCONNECTED, "Connection lost. Reconnecting...");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }
        PostFrame(frame);
    }
}

void ChatSession::HeartbeatLoop() {
    SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCKET) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(m_udpPort));
    if (inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) != 1) {
        closesocket(udp);
        return;
    }

    uint64_t seq = 0;
    int consecutiveMisses = 0;

    while (m_running) {
        auto cycleStart = std::chrono::steady_clock::now();
        if (m_connected && CurrentSocket() != INVALID_SOCKET) {
            ++seq;
            const std::string heartbeat = JsonBuild({
                {"type", "heartbeat"},
                {"userId", m_userId},
                {"seq", std::to_string(seq)},
                {"timestamp", protocol::IsoTimestampUtc()}
            });
            sendto(udp, heartbeat.data(), static_cast<int>(heartbeat.size()), 0,
                   reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));

            bool acknowledged = false;
            auto sentAt = std::chrono::steady_clock::now();
            while (m_running) {
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(udp, &readSet);
                timeval wait{0, 200000};
                int ready = select(0, &readSet, nullptr, nullptr, &wait);
                if (ready <= 0) {
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - sentAt).count() > 1500) {
                        break;
                    }
                    continue;
                }
                char buffer[2048] = {};
                int n = recvfrom(udp, buffer, sizeof(buffer) - 1, 0, nullptr, nullptr);
                if (n <= 0) break;
                buffer[n] = '\0';
                std::map<std::string, std::string> ack;
                if (JsonParse(buffer, ack) && JsonGet(ack, "type") == "hb_ack") {
                    int rtt = static_cast<int>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - sentAt).count());
                    m_rtt = rtt;
                    consecutiveMisses = 0;
                    PostHeartbeat(JsonGet(ack, "serverId", "server") + "|" +
                                  std::to_string(rtt) + "|ok");
                    acknowledged = true;
                    break;
                }
            }
            if (!acknowledged && m_running) {
                ++consecutiveMisses;
                if (consecutiveMisses >= 2) {
                    PostHeartbeat("||timeout");
                }
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cycleStart).count();
        int remaining = m_heartbeatMs - static_cast<int>(elapsed);
        while (remaining > 0 && m_running) {
            int slice = remaining > 100 ? 100 : remaining;
            std::this_thread::sleep_for(std::chrono::milliseconds(slice));
            remaining -= slice;
        }
    }

    closesocket(udp);
}

bool ChatSession::SendFields(const std::vector<std::pair<std::string, std::string>>& fields) {
    if (!m_connected) return false;
    const std::string payload = JsonBuild(fields);
    std::lock_guard<std::mutex> sendLock(m_sendMutex);
    std::lock_guard<std::mutex> socketLock(m_socketMutex);
    if (m_socket == INVALID_SOCKET) return false;
    return protocol::SendFrame(m_socket, payload);
}

bool ChatSession::SendChat(const std::string& to, const std::string& text,
                           const std::string& clientMsgId) {
    return SendFields({
        {"type", "chat"},
        {"to", to},
        {"message", text},
        {"clientMsgId", clientMsgId}
    });
}

bool ChatSession::SendGroupChat(const std::string& groupId, const std::string& text,
                                const std::string& clientMsgId) {
    return SendFields({
        {"type", "group_chat"},
        {"groupId", groupId},
        {"message", text},
        {"clientMsgId", clientMsgId}
    });
}

bool ChatSession::SendTyping(const std::string& to, const std::string& groupId, bool typing) {
    return SendFields({
        {"type", "typing"},
        {"to", to},
        {"groupId", groupId},
        {"isTyping", typing ? "true" : "false"}
    });
}

bool ChatSession::RequestHistory(const std::string& peer, const std::string& groupId,
                                 int limit) {
    return SendFields({
        {"type", "history"},
        {"with", peer},
        {"groupId", groupId},
        {"limit", std::to_string(limit)}
    });
}

bool ChatSession::RequestGroups() {
    return SendFields({{"type", "group_list"}});
}

bool ChatSession::RequestPresence() {
    return SendFields({{"type", "presence_list"}});
}

void ChatSession::Stop() {
    m_running = false;
    DropSocket();
    if (m_networkThread.joinable()) m_networkThread.join();
    if (m_heartbeatThread.joinable()) m_heartbeatThread.join();
}

std::string ChatSession::UserId() const {
    return m_userId;
}

std::string ChatSession::ServerId() const {
    std::lock_guard<std::mutex> lock(m_metaMutex);
    return m_serverId;
}

std::string ChatSession::LastError() const {
    std::lock_guard<std::mutex> lock(m_metaMutex);
    return m_lastError;
}

void ChatSession::PostFrame(const std::string& payload) {
    if (!m_notify) return;
    auto* copy = new std::string(payload);
    if (!PostMessageA(m_notify, WM_APP_SESSION_FRAME, 0,
                      reinterpret_cast<LPARAM>(copy))) {
        delete copy;
    }
}

void ChatSession::PostStatus(int status, const std::string& text) {
    if (!m_notify) return;
    auto* copy = new std::string(text);
    if (!PostMessageA(m_notify, WM_APP_SESSION_STATUS,
                      static_cast<WPARAM>(status), reinterpret_cast<LPARAM>(copy))) {
        delete copy;
    }
}

void ChatSession::PostHeartbeat(const std::string& text) {
    if (!m_notify) return;
    auto* copy = new std::string(text);
    if (!PostMessageA(m_notify, WM_APP_SESSION_HEARTBEAT, 0,
                      reinterpret_cast<LPARAM>(copy))) {
        delete copy;
    }
}
