#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include "../Common/Logger.h"
#include "ServerConfig.h"
#include "TcpServer.h"
#include "UdpHeartbeat.h"
#include "RedisCache.h"
#include "KafkaProducer.h"
#include "MySQLDatabase.h"

int main(int argc, char* argv[]) {
    std::string configPath = (argc > 1) ? argv[1] : "config/server1.ini";

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    ServerConfig cfg = ServerConfig::Load(configPath);
    Logger::Init(cfg.logFile);
    Logger::Info("Loading configuration from " + configPath);
    Logger::Info("ServerId : " + cfg.serverId);
    Logger::Info("TCP Port : " + std::to_string(cfg.tcpPort));
    Logger::Info("UDP Port : " + std::to_string(cfg.udpPort));

    MySQLDatabase db;
    if (!db.Connect(cfg)) {
        Logger::Warn("SQL      : Will retry in background on first validation.");
        for (int i = 0; i < 5 && !db.IsConnected(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            db.Connect(cfg);
        }
    }

    RedisCache redis(cfg, &db);
    if (!redis.Connect()) {
        Logger::Warn("Redis    : Starting without cache (SQL fallback active).");
    }

    KafkaProducer kafka;
    if (!kafka.Start(cfg)) {
        Logger::Error("Kafka    : Producer unavailable - messages will not be persisted.");
    }

    UdpHeartbeat heartbeat;
    if (!heartbeat.Start(cfg)) {
        Logger::Error("UDP      : Heartbeat listener failed to start.");
    }
    heartbeat.SetPulseCallback([&redis](const std::string& userId, const std::string&) {
        redis.RefreshOnline(userId);
    });

    TcpServer server(cfg, &redis, &kafka, &heartbeat, &db);
    if (!server.Start()) {
        Logger::Error("Fatal    : TCP server failed to start.");
        heartbeat.Stop();
        kafka.Stop();
        redis.Stop();
        db.Close();
        Logger::Close();
        WSACleanup();
        return 1;
    }

    redis.StartSubscriber(
    std::vector<std::string>{"chat.broadcast", "chat.presence"},
    [&server](const std::string& channel, const std::string& payload) {
        Logger::Info(
            "REDIS DEBUG: Received message on channel: " + channel +
            ", payload bytes: " + std::to_string(payload.size())
        );

        server.OnCrossServerMessage(payload);
    });

    Logger::Info("Type 'q' + Enter for graceful shutdown.");
    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            // Running as a background service often means stdin is closed.
            // Keep serving traffic instead of treating EOF as a shutdown request.
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cin.clear();
            continue;
        }
        if (line == "q" || line == "quit") break;
    }

    Logger::Info("Shutting down gracefully...");
    server.Stop();
    redis.Stop();
    heartbeat.Stop();
    kafka.Stop();
    db.Close();
    Logger::Info("Shutdown complete.");
    Logger::Close();
    WSACleanup();
    return 0;
}
