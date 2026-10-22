#include "Protocol.h"
#include "Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace protocol {
    constexpr uint32_t MAX_FRAME_SIZE = 16 * 1024 * 1024;

    bool SendAll(SOCKET s, const char* data, int length) {
        int sent = 0;
        while (sent < length) {
            int n = send(s, data + sent, length - sent, 0);
            if (n == SOCKET_ERROR || n == 0) return false;
            sent += n;
        }
        return true;
    }

    bool RecvAll(SOCKET s, char* data, int length) {
        int received = 0;
        while (received < length) {
            int n = recv(s, data + received, length - received, 0);
            if (n == 0 || n == SOCKET_ERROR) return false;
            received += n;
        }
        return true;
    }

    bool SendFrame(SOCKET s, const std::string& payload) {
        if (payload.empty() || payload.size() > MAX_FRAME_SIZE) return false;
        uint32_t length = static_cast<uint32_t>(payload.size());
        uint32_t netLength = htonl(length);
        if (!SendAll(s, reinterpret_cast<const char*>(&netLength), sizeof(netLength))) return false;
        return SendAll(s, payload.data(), static_cast<int>(payload.size()));
    }

    bool RecvFrame(SOCKET s, std::string& payload, int timeoutMs) {
        payload.clear();
        if (timeoutMs > 0) {
            DWORD timeout = static_cast<DWORD>(timeoutMs);
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        }

        uint32_t netLength = 0;
        if (!RecvAll(s, reinterpret_cast<char*>(&netLength), sizeof(netLength))) return false;
        uint32_t length = ntohl(netLength);
        if (length == 0 || length > MAX_FRAME_SIZE) return false;

        payload.resize(length);
        if (!RecvAll(s, &payload[0], static_cast<int>(length))) {
            payload.clear();
            return false;
        }
        return true;
    }

    std::string CurrentTimeMs() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    std::string IsoTimestampUtc() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_s(&tm, &t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }
}
