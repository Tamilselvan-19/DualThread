#include "KafkaProducer.h"

#include <librdkafka/rdkafka.h>
#include <chrono>
#include <thread>

#include "../Common/Logger.h"

namespace {
    void DeliveryReportCallback(rd_kafka_t*, const rd_kafka_message_t* message, void*) {
        if (message->err) {
            Logger::Error("Kafka    : Delivery failed: " +
                          std::string(rd_kafka_message_errstr(message)));
        }
    }

    void SetConf(rd_kafka_conf_t* conf, const char* key, const char* value) {
        char errbuf[512] = {};
        if (rd_kafka_conf_set(conf, key, value, errbuf, sizeof(errbuf)) !=
            RD_KAFKA_CONF_OK) {
            Logger::Warn(std::string("Kafka    : config ") + key + " rejected: " + errbuf);
        }
    }
}

KafkaProducer::~KafkaProducer() {
    Stop();
}

bool KafkaProducer::Start(const ServerConfig& c) {
    if (m_running) return true;
    m_cfg = c;
    char errbuf[512] = {0};

    rd_kafka_conf_t* conf = rd_kafka_conf_new();
    SetConf(conf, "bootstrap.servers", c.kafkaBrokers.c_str());
    SetConf(conf, "message.timeout.ms", "5000");
    SetConf(conf, "linger.ms", "5");
    rd_kafka_conf_set_dr_msg_cb(conf, DeliveryReportCallback);

    m_producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf, sizeof(errbuf));
    if (!m_producer) {
        Logger::Error("Kafka    : Producer creation failed: " + std::string(errbuf));
        rd_kafka_conf_destroy(conf);
        return false;
    }
    m_topic = rd_kafka_topic_new(m_producer, c.kafkaTopic.c_str(), nullptr);
    if (!m_topic) {
        Logger::Error("Kafka    : Topic handle creation failed");
        rd_kafka_destroy(m_producer);
        m_producer = nullptr;
        return false;
    }
    m_running = true;
    m_worker = std::thread([this] { WorkerLoop(); });
    Logger::Info("Kafka    : Producer started (broker: " + c.kafkaBrokers +
                 ", topic: " + c.kafkaTopic + ")");
    return true;
}

void KafkaProducer::Enqueue(const std::string& jsonPayload) {
    if (!m_running) {
        Logger::Warn("Kafka    : Producer not running - event dropped");
        return;
    }
    m_queue.Push(jsonPayload);
}

void KafkaProducer::WorkerLoop() {
    char errbuf[512] = {};
    while (m_running) {
        auto payload = m_queue.WaitPop(200);
        if (!payload.has_value()) {
            if (m_producer) rd_kafka_poll(m_producer, 0);
            continue;
        }

        bool queued = false;
        for (int attempt = 0; attempt < 5 && m_running; ++attempt) {
            int rc = rd_kafka_produce(
                m_topic, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
                const_cast<char*>(payload->data()),
                static_cast<size_t>(payload->size()),
                nullptr, 0, nullptr);
            if (rc == 0) {
                queued = true;
                break;
            }
            int err = rd_kafka_last_error();
            if (err == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
                rd_kafka_poll(m_producer, 100);
            } else {
                Logger::Error("Kafka    : produce failed: " +
                              std::string(rd_kafka_err2str(
                                  static_cast<rd_kafka_resp_err_t>(err))));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            (void)errbuf;
        }
        if (!queued) {
            Logger::Error("Kafka    : Dropping event after retries: " + *payload);
        }
        if (m_producer) rd_kafka_poll(m_producer, 0);
    }
}

void KafkaProducer::Stop() {
    if (!m_running && !m_producer) return;
    m_running = false;
    m_queue.Stop();
    if (m_worker.joinable()) m_worker.join();
    if (m_producer) {
        Logger::Info("Kafka    : Flushing outstanding messages...");
        rd_kafka_flush(m_producer, 5000);
        if (m_topic) rd_kafka_topic_destroy(m_topic);
        rd_kafka_destroy(m_producer);
        m_topic = nullptr;
        m_producer = nullptr;
    }
}
