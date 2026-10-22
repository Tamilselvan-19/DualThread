#include "ServerConfig.h"
#include "../Common/Ini.h"

namespace {
    std::string ReadFirst(const std::string& path,
                          const std::string& section,
                          const std::string& key,
                          const std::string& legacyKey,
                          const std::string& defaultValue) {
        std::string value = IniFile::ReadValue(path, section, key, "");
        if (value.empty() && !legacyKey.empty()) {
            value = IniFile::ReadValue(path, section, legacyKey, "");
        }
        return value.empty() ? defaultValue : value;
    }

    int ReadFirstInt(const std::string& path,
                     const std::string& section,
                     const std::string& key,
                     const std::string& legacyKey,
                     int defaultValue) {
        std::string value = IniFile::ReadValue(path, section, key, "");
        if (value.empty() && !legacyKey.empty()) {
            value = IniFile::ReadValue(path, section, legacyKey, "");
        }
        if (value.empty()) return defaultValue;
        try {
            return std::stoi(value);
        } catch (...) {
            return defaultValue;
        }
    }
}

ServerConfig ServerConfig::Load(const std::string& path) {
    ServerConfig c;

    c.serverId = ReadFirst(path, "server", "server_id", "ServerId", c.serverId);
    c.tcpPort = ReadFirstInt(path, "server", "tcp_port", "TcpPort", c.tcpPort);
    c.udpPort = ReadFirstInt(path, "server", "udp_port", "UdpPort", c.udpPort);
    c.logFile = ReadFirst(path, "server", "log_file", "LogFile", c.logFile);

    c.heartbeatIntervalMs = ReadFirstInt(
        path, "server", "heartbeat_interval_ms", "HeartbeatIntervalMs", c.heartbeatIntervalMs);
    c.inactivityTimeoutMs = ReadFirstInt(
        path, "server", "inactivity_timeout_ms", "InactivityTimeoutMs", c.inactivityTimeoutMs);
    c.hbMonitorIntervalMs = ReadFirstInt(
        path, "heartbeat", "monitor_interval_ms", "MonitorIntervalMs", c.hbMonitorIntervalMs);
    c.hbTimeoutSec = ReadFirstInt(
        path, "heartbeat", "timeout_sec", "TimeoutSec", c.hbTimeoutSec);
    if (c.inactivityTimeoutMs <= 0) c.inactivityTimeoutMs = c.hbTimeoutSec * 1000;
    if (c.hbTimeoutSec <= 0) c.hbTimeoutSec = c.inactivityTimeoutMs / 1000;
    // Keep the two timeout representations consistent.
    if (c.inactivityTimeoutMs < c.hbTimeoutSec * 1000) {
        c.inactivityTimeoutMs = c.hbTimeoutSec * 1000;
    }

    c.redisHost = ReadFirst(path, "redis", "host", "Host", c.redisHost);
    c.redisPort = ReadFirstInt(path, "redis", "port", "Port", c.redisPort);
    c.cacheTTLSeconds = ReadFirstInt(
        path, "redis", "cache_ttl_seconds", "CacheTTLSeconds", c.cacheTTLSeconds);

    c.kafkaBrokers = ReadFirst(path, "kafka", "brokers", "Brokers", c.kafkaBrokers);
    c.kafkaTopic = ReadFirst(path, "kafka", "topic", "Topic", c.kafkaTopic);

    c.mysqlHost = ReadFirst(path, "mysql", "host", "Host", c.mysqlHost);
    c.mysqlPort = ReadFirstInt(path, "mysql", "port", "Port", c.mysqlPort);
    c.mysqlUser = ReadFirst(path, "mysql", "user", "User", c.mysqlUser);
    c.mysqlPassword = ReadFirst(path, "mysql", "password", "Password", c.mysqlPassword);
    c.mysqlDatabase = ReadFirst(path, "mysql", "db", "Database", c.mysqlDatabase);
    c.mysqlDriver = ReadFirst(path, "mysql", "odbc_driver", "OdbcDriver", c.mysqlDriver);

    return c;
}
