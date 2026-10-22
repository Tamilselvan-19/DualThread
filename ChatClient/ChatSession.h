#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

#include "ChatClient.h"

// Owns the TCP socket and the UDP heartbeat socket. All socket work happens on
// worker threads; UI code only receives posted messages from these threads.
class ChatSession {
public:
    ChatSession();
    ~ChatSession();

    bool ConnectBlocking(const std::string& host, int port, int udpPort,
                         const std::string& userId, const std::string& password,
                         int heartbeatMs, std::string& error);
    void Start(HWND notifyWindow);
    void Stop();

    bool SendFields(const std::vector<std::pair<std::string, std::string>>& fields);
    bool SendChat(const std::string& to, const std::string& text,
                  const std::string& clientMsgId);
    bool SendGroupChat(const std::string& groupId, const std::string& text,
                       const std::string& clientMsgId);
    bool SendTyping(const std::string& to, const std::string& groupId, bool typing);
    bool RequestHistory(const std::string& peer, const std::string& groupId, int limit);
    bool RequestGroups();
    bool RequestPresence();

    bool IsConnected() const { return m_connected; }
    std::string UserId() const;
    std::string ServerId() const;
    int RttMs() const { return m_rtt.load(); }
    std::string LastError() const;

private:
    bool ConnectInternal(std::string& error);
    void NetworkLoop();
    void HeartbeatLoop();
    void DropSocket();
    SOCKET CurrentSocket();
    void PostFrame(const std::string& payload);
    void PostStatus(int status, const std::string& text);
    void PostHeartbeat(const std::string& text);

    std::string m_host;
    int m_port = 5000;
    int m_udpPort = 5001;
    int m_heartbeatMs = 5000;
    std::string m_userId;
    std::string m_password;

    mutable std::mutex m_socketMutex;
    std::mutex m_sendMutex;
    mutable std::mutex m_metaMutex;
    SOCKET m_socket = INVALID_SOCKET;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_fatalAuth{false};
    std::atomic<int> m_rtt{-1};

    std::string m_serverId;
    std::string m_lastError;

    HWND m_notify = nullptr;
    std::thread m_networkThread;
    std::thread m_heartbeatThread;
};
