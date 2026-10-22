#pragma once

#include <mutex>
#include <string>

#include "KafkaConsumer.h"

class DatabaseWriter {
public:
    bool Connect(const DbWriterConfig& config);
    bool EnsureConnected(const DbWriterConfig& config);
    bool InsertMessage(const std::string& messageId,
                       const std::string& userId,
                       const std::string& timestamp,
                       const std::string& message);
    bool InsertMessage(const std::string& messageId,
                       const std::string& userId,
                       const std::string& timestamp,
                       const std::string& message,
                       const std::string& conversationType,
                       const std::string& targetId);
    bool IsConnected() const { return m_connected; }
    void Close();

private:
    bool InsertPrepared(const std::string& messageId,
                        const std::string& userId,
                        const std::string& timestamp,
                        const std::string& message,
                        const std::string& conversationType,
                        const std::string& targetId,
                        bool extended);
    void ReleaseAll();

    std::mutex m_mutex;
    void* m_connRaw = nullptr; // SQLHDBC
    void* m_envRaw = nullptr;  // SQLHENV
    bool m_connected = false;
    bool m_schemaExtended = true;
    DbWriterConfig m_cfg;
};
