#include "RedisCache.h"

#include <hiredis/hiredis.h>
#include <map>
#include <ws2tcpip.h>

#include "../Common/JsonUtil.h"
#include "../Common/Logger.h"
#include "../Common/Protocol.h"
#include "../Common/Sha256.h"
#include "MySQLDatabase.h"

namespace {
    constexpr int kPresenceTtlSeconds = 30;
}

RedisCache::RedisCache(const ServerConfig& config, MySQLDatabase* db)
    : m_cfg(config), m_db(db) {}

RedisCache::~RedisCache() { Stop(); }

void RedisCache::FreeReply(redisReply* reply) {
    if (reply) freeReplyObject(reply);
}

bool RedisCache::Connect() {
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (m_ctx) {
        redisFree(m_ctx);
        m_ctx = nullptr;
    }
    m_ctx = redisConnect(m_cfg.redisHost.c_str(), m_cfg.redisPort);
    if (!m_ctx || m_ctx->err) {
        Logger::Warn("Redis    : Connection lost (" +
            std::string(m_ctx ? m_ctx->errstr : "null context") +
            "). Falling back to SQL...");
        m_connected = false;
        if (m_ctx) { redisFree(m_ctx); m_ctx = nullptr; }
        return false;
    }
    m_connected = true;
    Logger::Info("Redis    : Connected (Cache TTL: " +
                 std::to_string(m_cfg.cacheTTLSeconds) + "s)");
    return true;
}

bool RedisCache::ReadCachedHash(const std::string& userId, std::string& hashOut) {
    if (!m_connected) return false;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return false;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(m_ctx, "GET chat:user:%s", userId.c_str()));
    if (!reply) { m_connected = false; return false; }
    bool ok = false;
    if (reply->type == REDIS_REPLY_STRING) {
        std::map<std::string, std::string> obj;
        if (JsonParse(std::string(reply->str, reply->len), obj)) {
            hashOut = JsonGet(obj, "hash");
            ok = !hashOut.empty();
        }
    }
    FreeReply(reply);
    return ok;
}

void RedisCache::CacheUser(const std::string& userId, const std::string& passwordHash,
                           const std::string& ip) {
    if (!m_connected) return;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return;
    std::string value = JsonBuild({{"hash", passwordHash}, {"ip", ip}});
    redisReply* reply = static_cast<redisReply*>(redisCommand(m_ctx,
        "SET chat:user:%s %s EX %d", userId.c_str(), value.c_str(), m_cfg.cacheTTLSeconds));
    if (reply) FreeReply(reply);
    Logger::Info("Redis    : Cached auth data for " + userId +
                 " (TTL " + std::to_string(m_cfg.cacheTTLSeconds) + "s)");
}

bool RedisCache::ValidateViaSql(const std::string& userId, const std::string& password,
                                const std::string& ip, bool& cacheHit) {
    cacheHit = false;
    UserRecord rec;
    if (!m_db || !m_db->GetUser(userId, rec)) {
        // SQL fetch failed or user unknown: last chance is cached auth data.
        std::string cachedHash;
        if (ReadCachedHash(userId, cachedHash) &&
            cachedHash == Sha256Hex(password)) {
            Logger::Warn("Redis    : SQL unavailable - user " + userId +
                         " authenticated from Redis cache.");
            return true;
        }
        Logger::Info("SQL      : User validation failed for " + userId);
        return false;
    }
    if (rec.passwordHash != Sha256Hex(password)) {
        Logger::Info("SQL      : Invalid credentials for " + userId);
        return false;
    }
    Logger::Info("SQL      : Validation successful for " + userId);
    CacheUser(userId, rec.passwordHash, ip);
    return true;
}

bool RedisCache::ValidateUser(const std::string& userId, const std::string& password,
                              const std::string& ip, bool& cacheHit) {
    cacheHit = false;

    std::string cachedHash;
    if (ReadCachedHash(userId, cachedHash)) {
        cacheHit = true;
        bool ok = (cachedHash == Sha256Hex(password));
        Logger::Info(std::string("Redis    : ") +
                     (ok ? "User " : "Invalid credentials for ") +
                     userId + (ok ? " validated (Redis cache hit)" : " (Redis cache hit)"));
        if (ok) return true;
    }

    if (m_db && !m_db->IsConnected()) {
        Logger::Warn("SQL      : reconnecting to MySQL...");
    }
    return ValidateViaSql(userId, password, ip, cacheHit);
}

bool RedisCache::Publish(const std::string& channel, const std::string& message) {
    if (!m_connected) return false;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return false;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(m_ctx, "PUBLISH %s %s", channel.c_str(), message.c_str()));
    if (!reply) { m_connected = false; return false; }
    FreeReply(reply);
    return true;
}

bool RedisCache::SetOnline(const std::string& userId, const std::string& serverId) {
    if (userId.empty()) return false;
    if (!m_connected) Connect();
    if (!m_connected) return false;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(m_ctx,
        "SET chat:online:%s %s EX %d", userId.c_str(), serverId.c_str(),
        kPresenceTtlSeconds));
    if (!reply) { m_connected = false; return false; }
    FreeReply(reply);
    return true;
}

bool RedisCache::RefreshOnline(const std::string& userId) {
    if (userId.empty() || !m_connected) return false;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(m_ctx,
        "EXPIRE chat:online:%s %d", userId.c_str(), kPresenceTtlSeconds));
    if (!reply) { m_connected = false; return false; }
    bool ok = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    FreeReply(reply);
    return ok;
}

bool RedisCache::ClearOnline(const std::string& userId) {
    if (userId.empty() || !m_connected) return false;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(m_ctx,
        "DEL chat:online:%s", userId.c_str()));
    if (!reply) { m_connected = false; return false; }
    FreeReply(reply);
    return true;
}

bool RedisCache::IsOnline(const std::string& userId) {
    if (userId.empty() || !m_connected) return false;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(m_ctx,
        "EXISTS chat:online:%s", userId.c_str()));
    if (!reply) { m_connected = false; return false; }
    bool ok = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    FreeReply(reply);
    return ok;
}

std::vector<std::string> RedisCache::GetOnlineUsers() {
    std::vector<std::string> users;
    if (!m_connected) return users;
    std::lock_guard<std::mutex> lock(m_ctxMutex);
    if (!m_ctx) return users;

    redisReply* reply = static_cast<redisReply*>(redisCommand(m_ctx,
        "KEYS chat:online:*"));
    if (!reply) { m_connected = false; return users; }
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i] && reply->element[i]->type == REDIS_REPLY_STRING) {
                std::string key(reply->element[i]->str, reply->element[i]->len);
                const std::string prefix = "chat:online:";
                if (key.rfind(prefix, 0) == 0) users.push_back(key.substr(prefix.size()));
            }
        }
    }
    FreeReply(reply);
    return users;
}

void RedisCache::StartSubscriber(const std::string& channel,
                                 std::function<void(const std::string&)> onMessage) {
    StartSubscriber(std::vector<std::string>{channel},
        [onMessage](const std::string&, const std::string& payload) {
            if (onMessage) onMessage(payload);
        });
}

void RedisCache::StartSubscriber(
    const std::vector<std::string>& channels,
    std::function<void(const std::string&, const std::string&)> onMessage) {
    if (m_running) return;
    m_running = true;
    m_subThread = std::thread([this, channels, onMessage]() {
        SubscriberLoop(channels, onMessage);
    });
}

void RedisCache::SubscriberLoop(
    const std::vector<std::string>& channels,
    std::function<void(const std::string&, const std::string&)> onMessage) {
    m_subCtx = redisConnect(m_cfg.redisHost.c_str(), m_cfg.redisPort);
    if (!m_subCtx || m_subCtx->err) {
        Logger::Error("Redis    : Subscriber connection failed");
        if (m_subCtx) { redisFree(m_subCtx); m_subCtx = nullptr; }
        m_running = false;
        return;
    }
    m_subFd = m_subCtx->fd;

    for (const auto& channel : channels) {
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_subCtx, "SUBSCRIBE %s", channel.c_str()));
        if (reply) FreeReply(reply);
    }
    Logger::Info("Redis    : Subscriber active on " + std::to_string(channels.size()) +
                 " channel(s)");

    while (m_running) {
        void* raw = nullptr;
        if (redisGetReply(m_subCtx, &raw) != REDIS_OK) {
            if (m_running) {
                Logger::Warn("Redis    : Subscriber read failed; reconnecting in 2s");
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (!m_running) break;
                redisFree(m_subCtx);
                m_subFd = -1;
                m_subCtx = redisConnect(m_cfg.redisHost.c_str(), m_cfg.redisPort);
                if (!m_subCtx || m_subCtx->err) {
                    if (m_subCtx) { redisFree(m_subCtx); m_subCtx = nullptr; }
                    break;
                }
                m_subFd = m_subCtx->fd;
                for (const auto& channel : channels) {
                    redisReply* reply = static_cast<redisReply*>(
                        redisCommand(m_subCtx, "SUBSCRIBE %s", channel.c_str()));
                    if (reply) FreeReply(reply);
                }
                continue;
            }
            break;
        }
        redisReply* r = static_cast<redisReply*>(raw);
        if (r && r->type == REDIS_REPLY_ARRAY && r->elements >= 3 &&
            r->element[2] && r->element[2]->type == REDIS_REPLY_STRING) {
            std::string channel;
            if (r->element[1] && r->element[1]->type == REDIS_REPLY_STRING) {
                channel.assign(r->element[1]->str, r->element[1]->len);
            }
            std::string payload(r->element[2]->str, r->element[2]->len);
            if (onMessage) onMessage(channel, payload);
        }
        if (r) FreeReply(r);
    }

    if (m_subCtx) {
        redisFree(m_subCtx);
        m_subCtx = nullptr;
    }
    m_subFd = -1;
}

void RedisCache::Stop() {
    m_running = false;
    int fd = m_subFd.exchange(-1);
    if (fd >= 0) {
        shutdown(fd, SD_BOTH);
    }
    if (m_subThread.joinable()) m_subThread.join();
    {
        std::lock_guard<std::mutex> lock(m_ctxMutex);
        if (m_ctx) { redisFree(m_ctx); m_ctx = nullptr; }
    }
    m_connected = false;
}
