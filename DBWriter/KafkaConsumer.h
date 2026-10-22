#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <librdkafka/rdkafka.h>
#include <vector>

struct DbWriterConfig {
    std::string kafkaBrokers = "127.0.0.1:9092";
    std::string kafkaTopic = "chat-messages";
    std::string kafkaGroupId = "dbwriter-group";
    std::string mysqlHost = "127.0.0.1";
    int mysqlPort = 3306;
    std::string mysqlDb = "ChatDatabase";
    std::string mysqlUser = "root";
    std::string mysqlPassword = "";
    std::string mysqlDriver = "MySQL ODBC 26.7 ANSI Driver";
    std::string logFile = "logs/dbwriter.log";
};

class DatabaseWriter;

class KafkaConsumer {
public:
    bool Start(const DbWriterConfig& cfg, DatabaseWriter* writer);
    void Stop();

private:
    void PollLoop();
    DbWriterConfig m_cfg;
    DatabaseWriter* m_writer = nullptr;
    rd_kafka_t* m_consumer = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

