#pragma once

#include <string>

// Single shared server configuration. The previous revision declared two
// different ServerConfig structures (here and in TcpServer.h), which made the
// ChatServer project impossible to compile.
struct ServerConfig {
    std::string serverId = "server1";
    int tcpPort = 6001;
    int udpPort = 7001;
    std::string logFile = "logs/server.log";

    int heartbeatIntervalMs = 5000;
    int inactivityTimeoutMs = 15000;
    int hbMonitorIntervalMs = 1000;
    int hbTimeoutSec = 15;

    std::string redisHost = "127.0.0.1";
    int redisPort = 6379;
    int cacheTTLSeconds = 300;

    std::string kafkaBrokers = "127.0.0.1:9092";
    std::string kafkaTopic = "chat-messages";

    std::string mysqlHost = "127.0.0.1";
    int mysqlPort = 3306;
    std::string mysqlUser = "root";
    std::string mysqlPassword;
    std::string mysqlDatabase = "ChatDatabase";
    std::string mysqlDriver = "MySQL ODBC 26.7 ANSI Driver";

    static ServerConfig Load(const std::string& path);
};
