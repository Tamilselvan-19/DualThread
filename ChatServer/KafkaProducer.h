#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "../Common/ThreadSafeQueue.h"
#include "ServerConfig.h"

struct rd_kafka_s;
struct rd_kafka_topic_s;

// Kafka producer used by the chat servers to publish chat events to the
// "chat-messages" topic. Messages are queued by client handler threads and
// delivered asynchronously by a dedicated worker thread.
class KafkaProducer {
public:
    ~KafkaProducer();
    bool Start(const ServerConfig& config);
    void Enqueue(const std::string& jsonPayload);
    bool IsRunning() const { return m_running; }
    void Stop();

private:
    void WorkerLoop();
    ServerConfig m_cfg;
    ThreadSafeQueue<std::string> m_queue;
    std::thread m_worker;
    std::atomic<bool> m_running{false};
    rd_kafka_s* m_producer = nullptr;
    rd_kafka_topic_s* m_topic = nullptr;
};
