#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ServerConfig.h"

struct redisContext;
struct redisReply;

class MySQLDatabase;

class RedisCache {
public:
    RedisCache(const ServerConfig& config, MySQLDatabase* db);
    ~RedisCache();

    bool Connect();
    bool IsConnected() const { return m_connected; }
    bool ValidateUser(const std::string& userId, const std::string& password,
                      const std::string& ip, bool& cacheHit);
    bool Publish(const std::string& channel, const std::string& message);

    // Online-presence helpers (keys: chat:online:<userId>).
    bool SetOnline(const std::string& userId, const std::string& serverId);
    bool RefreshOnline(const std::string& userId);
    bool ClearOnline(const std::string& userId);
    bool IsOnline(const std::string& userId);
    std::vector<std::string> GetOnlineUsers();

    void StartSubscriber(const std::string& channel,
                         std::function<void(const std::string&)> onMessage);
    void StartSubscriber(const std::vector<std::string>& channels,
                         std::function<void(const std::string&, const std::string&)> onMessage);
    void Stop();

private:
    bool ValidateViaSql(const std::string& userId, const std::string& password,
                        const std::string& ip, bool& cacheHit);
    void CacheUser(const std::string& userId, const std::string& passwordHash,
                   const std::string& ip);
    bool ReadCachedHash(const std::string& userId, std::string& hashOut);
    void SubscriberLoop(const std::vector<std::string>& channels,
                        std::function<void(const std::string&, const std::string&)> onMessage);
    static void FreeReply(redisReply* reply);

    ServerConfig m_cfg;
    MySQLDatabase* m_db = nullptr;
    redisContext* m_ctx = nullptr;
    redisContext* m_subCtx = nullptr;
    std::atomic<int> m_subFd{-1};
    std::mutex m_ctxMutex;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_running{false};
    std::thread m_subThread;
};
