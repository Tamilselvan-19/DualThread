#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <string>

namespace protocol {
    bool SendAll(SOCKET s, const char* data, int length);
    bool RecvAll(SOCKET s, char* data, int length);
    bool SendFrame(SOCKET s, const std::string& payload);
    bool RecvFrame(SOCKET s, std::string& payload, int timeoutMs = 30000);
    std::string CurrentTimeMs();
    std::string IsoTimestampUtc();
}
