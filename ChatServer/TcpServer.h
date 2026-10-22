#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

#include "ServerConfig.h"

class RedisCache;
class KafkaProducer;
class UdpHeartbeat;
class MySQLDatabase;

struct ClientInfo {
    SOCKET socket = INVALID_SOCKET;
    std::string userId;
    std::string ip;
    std::atomic<bool> alive{true};
    std::atomic<bool> finished{false};
    std::atomic<bool> closed{false};
    std::mutex sendMutex;

    bool Send(const std::string& payload);
    void CloseSocket();
};

class TcpServer {
public:
    TcpServer(const ServerConfig& config, RedisCache* redis, KafkaProducer* kafka,
              UdpHeartbeat* heartbeat, MySQLDatabase* database);
    ~TcpServer();

    bool Start();
    void Stop();

    // Called from the Redis subscriber thread. The same entry point handles
    // chat, typing, group-sync and presence events.
    void OnCrossServerMessage(const std::string& payload);

private:
    void AcceptLoop();
    void ClientLoop(std::shared_ptr<ClientInfo> client);
    bool HandleFrame(const std::shared_ptr<ClientInfo>& client,
                     const std::map<std::string, std::string>& msg);
    void SendError(const std::shared_ptr<ClientInfo>& client,
                   const std::string& message,
                   const std::string& clientMsgId = "");
    void SendPresenceList(const std::shared_ptr<ClientInfo>& client);
    void SendGroupList(const std::shared_ptr<ClientInfo>& client);
    void SendHistory(const std::shared_ptr<ClientInfo>& client,
                     const std::string& targetId, bool isGroup, int limit);
    void BroadcastLocal(const std::string& payload,
                        const std::string& excludeUserId = "");
    void BroadcastGroupLocal(const std::string& groupId,
                             const std::string& payload,
                             const std::string& excludeUserId = "");
    bool SendToUser(const std::string& userId, const std::string& payload);
    std::string BuildOnlineList();
    void UpdateGroupCache(const std::string& groupId,
                          const std::string& groupName,
                          const std::vector<std::string>& members,
                          std::vector<std::string>* added,
                          std::vector<std::string>* removed);
    void NotifyLocalGroupChanges(const std::string& groupId,
                                 const std::string& groupName,
                                 const std::vector<std::string>& added,
                                 const std::vector<std::string>& removed);
    void JoinFinishedWorkers();
    void CleanupClient(const std::shared_ptr<ClientInfo>& client, bool wasCurrent);
    void PublishGroupSync(const std::string& groupId,
                          const std::string& groupName,
                          const std::vector<std::string>& members);

    ServerConfig m_cfg;
    RedisCache* m_redis = nullptr;
    KafkaProducer* m_kafka = nullptr;
    UdpHeartbeat* m_heartbeat = nullptr;
    MySQLDatabase* m_db = nullptr;

    SOCKET m_listenSocket = INVALID_SOCKET;
    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;

    std::mutex m_clientsMutex;
    std::map<std::string, std::shared_ptr<ClientInfo>> m_clients;

    std::mutex m_groupsMutex;
    std::map<std::string, std::set<std::string>> m_groupMembers;
    std::map<std::string, std::set<std::string>> m_userGroups;

    std::mutex m_workersMutex;
    std::vector<std::pair<std::shared_ptr<ClientInfo>, std::thread>> m_workers;
    std::atomic<uint64_t> m_messageSeq{0};
};
