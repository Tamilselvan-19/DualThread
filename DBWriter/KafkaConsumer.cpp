#include "KafkaConsumer.h"

#include <chrono>
#include <map>
#include <librdkafka/rdkafka.h>
#include <thread>

#include "../Common/JsonUtil.h"
#include "../Common/Logger.h"
#include "DatabaseWriter.h"

bool KafkaConsumer::Start(const DbWriterConfig& cfg, DatabaseWriter* writer) {
    m_cfg = cfg;
    m_writer = writer;

    char errbuf[512] = { 0 };
    rd_kafka_conf_t* conf = rd_kafka_conf_new();

    rd_kafka_conf_set(conf, "bootstrap.servers", cfg.kafkaBrokers.c_str(), errbuf, sizeof(errbuf));
    rd_kafka_conf_set(conf, "group.id", cfg.kafkaGroupId.c_str(), errbuf, sizeof(errbuf));
    rd_kafka_conf_set(conf, "enable.auto.commit", "false", errbuf, sizeof(errbuf));
    rd_kafka_conf_set(conf, "auto.offset.reset", "earliest", errbuf, sizeof(errbuf));

    m_consumer = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errbuf, sizeof(errbuf));
    if (!m_consumer) {
        Logger::Error("Kafka    : consumer creation failed: " + std::string(errbuf));
        return false;
    }

    rd_kafka_poll_set_consumer(m_consumer);

    rd_kafka_topic_partition_list_t* topics = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(topics, cfg.kafkaTopic.c_str(), RD_KAFKA_PARTITION_UA);

    rd_kafka_resp_err_t err = rd_kafka_subscribe(m_consumer, topics);
    rd_kafka_topic_partition_list_destroy(topics);

    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        Logger::Error("Kafka    : subscribe failed: " + std::string(rd_kafka_err2str(err)));
        rd_kafka_destroy(m_consumer);
        m_consumer = nullptr;
        return false;
    }

    m_running = true;
    m_thread = std::thread([this]() { PollLoop(); });
    Logger::Info("Kafka    : consumer started");
    return true;
}

void KafkaConsumer::PollLoop() {
    while (m_running) {
        rd_kafka_message_t* msg = rd_kafka_consumer_poll(m_consumer, 500);
        if (!msg) continue;

        if (msg->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
            rd_kafka_message_destroy(msg);
            continue;
        }

        if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            Logger::Error("Kafka    : consume error: " + std::string(rd_kafka_message_errstr(msg)));
            rd_kafka_message_destroy(msg);
            continue;
        }

        std::string payload(static_cast<const char*>(msg->payload), msg->len);
        std::map<std::string, std::string> event;
        bool valid = JsonParse(payload, event) &&
            !JsonGet(event, "messageId").empty() &&
            !JsonGet(event, "userId").empty() &&
            !JsonGet(event, "timestamp").empty() &&
            !JsonGet(event, "message").empty();

        if (!valid) {
            Logger::Error("DBWriter  : invalid message skipped: " + payload);
            rd_kafka_commit_message(m_consumer, msg, 0);
            rd_kafka_message_destroy(msg);
            continue;
        }

        const std::string conversationType =
            JsonGet(event, "conversationType", "personal");
        const std::string targetId = JsonGet(event, "targetId");

        bool stored = false;
        int attempt = 0;
        while (m_running && !stored) {
            if (!m_writer->IsConnected()) {
                m_writer->Connect(m_cfg);
            }
            stored = m_writer->InsertMessage(
                JsonGet(event, "messageId"),
                JsonGet(event, "userId"),
                JsonGet(event, "timestamp"),
                JsonGet(event, "message"),
                conversationType,
                targetId);
            if (stored) break;
            if ((++attempt % 5) == 1) {
                Logger::Warn("DBWriter  : MySQL write failed; retrying without "
                             "committing the Kafka offset");
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        if (stored) {
            rd_kafka_commit_message(m_consumer, msg, 0);
        } else {
            Logger::Error("DBWriter  : shutting down with an uncommitted message");
        }

        rd_kafka_message_destroy(msg);
    }
}

void KafkaConsumer::Stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();

    if (m_consumer) {
        rd_kafka_consumer_close(m_consumer);
        rd_kafka_destroy(m_consumer);
        m_consumer = nullptr;
    }
}

