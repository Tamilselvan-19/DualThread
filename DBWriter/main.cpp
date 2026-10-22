#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "../Common/Ini.h"
#include "../Common/Logger.h"
#include "DatabaseWriter.h"
#include "KafkaConsumer.h"

namespace {
    std::string Read(const std::string& path, const std::string& key,
                     const std::string& legacyKey, const std::string& fallback) {
        std::string value = IniFile::ReadValue(path, "dbwriter", key, "");
        if (value.empty() && !legacyKey.empty()) {
            value = IniFile::ReadValue(path, "dbwriter", legacyKey, "");
        }
        return value.empty() ? fallback : value;
    }

    int ReadInt(const std::string& path, const std::string& key,
                const std::string& legacyKey, int fallback) {
        std::string value = IniFile::ReadValue(path, "dbwriter", key, "");
        if (value.empty() && !legacyKey.empty()) {
            value = IniFile::ReadValue(path, "dbwriter", legacyKey, "");
        }
        try {
            return value.empty() ? fallback : std::stoi(value);
        } catch (...) {
            return fallback;
        }
    }

    DbWriterConfig LoadConfig(const std::string& path) {
        DbWriterConfig cfg;
        cfg.kafkaBrokers = Read(path, "kafka_brokers", "Brokers", cfg.kafkaBrokers);
        cfg.kafkaTopic = Read(path, "kafka_topic", "Topic", cfg.kafkaTopic);
        cfg.kafkaGroupId = Read(path, "kafka_group_id", "GroupId", cfg.kafkaGroupId);
        cfg.mysqlHost = Read(path, "mysql_host", "Host", cfg.mysqlHost);
        cfg.mysqlPort = ReadInt(path, "mysql_port", "Port", cfg.mysqlPort);
        cfg.mysqlDb = Read(path, "mysql_db", "Database", cfg.mysqlDb);
        cfg.mysqlUser = Read(path, "mysql_user", "User", cfg.mysqlUser);
        cfg.mysqlPassword = Read(path, "mysql_password", "Password", cfg.mysqlPassword);
        cfg.mysqlDriver = Read(path, "odbc_driver", "OdbcDriver", cfg.mysqlDriver);
        cfg.logFile = Read(path, "log_file", "LogFile", cfg.logFile);

        const char* envPassword = std::getenv("CPPCHAT_MYSQL_PASSWORD");
        if (envPassword && *envPassword) cfg.mysqlPassword = envPassword;
        return cfg;
    }
}

int main(int argc, char* argv[]) {
    std::string configPath = (argc > 1) ? argv[1] : "config/dbwriter.ini";

    DbWriterConfig cfg = LoadConfig(configPath);
    Logger::Init(cfg.logFile);
    Logger::Info("DBWriter  : loading configuration from " + configPath);
    Logger::Info("DBWriter  : Kafka " + cfg.kafkaBrokers + " / topic " + cfg.kafkaTopic);
    Logger::Info("DBWriter  : MySQL " + cfg.mysqlHost + ":" +
                 std::to_string(cfg.mysqlPort) + "/" + cfg.mysqlDb);

    DatabaseWriter writer;
    if (!writer.Connect(cfg)) {
        Logger::Warn("DBWriter  : MySQL not ready; continuing and retrying on message consumption");
    }

    KafkaConsumer consumer;
    if (!consumer.Start(cfg, &writer)) {
        Logger::Error("DBWriter  : Kafka consumer failed to start");
        Logger::Close();
        return 1;
    }

    Logger::Info("DBWriter  : service started. Type 'q' + Enter for graceful shutdown.");
    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cin.clear();
            continue;
        }
        if (line == "q" || line == "quit") break;
    }

    consumer.Stop();
    writer.Close();
    Logger::Info("DBWriter  : shutdown complete");
    Logger::Close();
    return 0;
}
