#include "TcpProxy.h"

#include <thread>
#include <vector>
#include <algorithm>

#include "../Common/Logger.h"
#include "../Common/Protocol.h"

namespace {
    std::mutex g_sessionsMutex;
    std::vector<std::weak_ptr<ProxySession>> g_sessions;

    void ForwardLoop(const std::shared_ptr<ProxySession>& session, bool clientToBackend) {
        SOCKET from = clientToBackend ? session->clientSocket : session->backendSocket;
        SOCKET to = clientToBackend ? session->backendSocket : session->clientSocket;
        char buffer[8192];
        while (!session->closed) {
            int n = recv(from, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            if (!protocol::SendAll(to, buffer, n)) break;
        }
        session->Close();
    }
}

void ProxySession::Close() {
    std::lock_guard<std::mutex> lock(closeMutex);
    if (closed.exchange(true)) return;
    if (clientSocket != INVALID_SOCKET) {
        shutdown(clientSocket, SD_BOTH);
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
    if (backendSocket != INVALID_SOCKET) {
        shutdown(backendSocket, SD_BOTH);
        closesocket(backendSocket);
        backendSocket = INVALID_SOCKET;
    }
    Logger::Info("Proxy    : closed session for " + clientIp +
                 (backendName.empty() ? "" : " -> " + backendName));
}

void TcpProxy::StartProxyPair(std::shared_ptr<ProxySession> session) {
    if (!session || session->clientSocket == INVALID_SOCKET ||
        session->backendSocket == INVALID_SOCKET) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);
        g_sessions.erase(
            std::remove_if(g_sessions.begin(), g_sessions.end(),
                           [](const std::weak_ptr<ProxySession>& item) {
                               return item.expired();
                           }),
            g_sessions.end());
        g_sessions.push_back(session);
    }

    std::thread([session]() { ForwardLoop(session, true); }).detach();
    std::thread([session]() { ForwardLoop(session, false); }).detach();
}

void TcpProxy::CloseAll() {
    std::vector<std::shared_ptr<ProxySession>> sessions;
    {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);
        for (auto& weak : g_sessions) {
            if (auto session = weak.lock()) sessions.push_back(session);
        }
        g_sessions.clear();
    }
    for (auto& session : sessions) session->Close();
}
