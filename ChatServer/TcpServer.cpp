#include "TcpServer.h"

#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <vector>

#include "../Common/JsonUtil.h"
#include "../Common/Logger.h"
#include "../Common/Protocol.h"
#include "KafkaProducer.h"
#include "MySQLDatabase.h"
#include "RedisCache.h"
#include "UdpHeartbeat.h"

namespace {
    const char* kBroadcastChannel = "chat.broadcast";
    const char* kPresenceChannel = "chat.presence";
    constexpr size_t kMaxMessageLength = 8000;

    std::string Trim(const std::string& value) {
        size_t start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, end - start + 1);
    }

    std::vector<std::string> SplitCsv(const std::string& value) {
        std::vector<std::string> out;
        std::stringstream stream(value);
        std::string item;
        while (std::getline(stream, item, ',')) {
            item = Trim(item);
            if (!item.empty()) out.push_back(item);
        }
        return out;
    }

    std::string JoinCsv(const std::set<std::string>& values) {
        std::string out;
        for (const auto& value : values) {
            if (!out.empty()) out += ",";
            out += value;
        }
        return out;
    }

    std::string JoinCsv(const std::vector<std::string>& values) {
        std::string out;
        for (const auto& value : values) {
            if (!out.empty()) out += ",";
            out += value;
        }
        return out;
    }

    bool IsValidUserId(const std::string& userId) {
        if (userId.empty() || userId.size() > 64) return false;
        if (userId.find_first_of(",\r\n\t") != std::string::npos) return false;
        return true;
    }

    std::string MakeMessageId(const std::string& serverId, uint64_t seq) {
        return "msg-" + serverId + "-" + std::to_string(seq) + "-" +
               std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }

    std::string MakeGroupId() {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        std::mt19937_64 rng(static_cast<uint64_t>(now));
        std::ostringstream out;
        out << "grp-" << std::hex << now << "-" << (rng() & 0xFFFF);
        return out.str();
    }
}

bool ClientInfo::Send(const std::string& payload) {
    if (!alive) return false;
    std::lock_guard<std::mutex> lock(sendMutex);
    if (!alive || socket == INVALID_SOCKET) return false;
    if (!protocol::SendFrame(socket, payload)) {
        alive = false;
        return false;
    }
    return true;
}

void ClientInfo::CloseSocket() {
    if (closed.exchange(true)) return;
    if (socket != INVALID_SOCKET) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

TcpServer::TcpServer(const ServerConfig& config, RedisCache* redis,
                     KafkaProducer* kafka, UdpHeartbeat* heartbeat,
                     MySQLDatabase* database)
    : m_cfg(config), m_redis(redis), m_kafka(kafka),
      m_heartbeat(heartbeat), m_db(database) {}

TcpServer::~TcpServer() { Stop(); }

bool TcpServer::Start() {
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        Logger::Error("TCP      : socket() failed: " + std::to_string(WSAGetLastError()));
        return false;
    }
    int yes = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(m_cfg.tcpPort));
    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Logger::Error("TCP      : bind() failed: " + std::to_string(WSAGetLastError()));
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Logger::Error("TCP      : listen() failed: " + std::to_string(WSAGetLastError()));
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }
    m_running = true;
    m_acceptThread = std::thread([this] { AcceptLoop(); });
    Logger::Info("TCP      : Server " + m_cfg.serverId + " listening on port " +
                 std::to_string(m_cfg.tcpPort));
    return true;
}

void TcpServer::Stop() {
    if (!m_running.exchange(false)) return;

    if (m_listenSocket != INVALID_SOCKET) {
        shutdown(m_listenSocket, SD_BOTH);
        closesocket(m_listenSocket);
    }
    if (m_acceptThread.joinable()) m_acceptThread.join();
    m_listenSocket = INVALID_SOCKET;

    std::vector<std::shared_ptr<ClientInfo>> clients;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& kv : m_clients) {
            kv.second->alive = false;
            clients.push_back(kv.second);
        }
    }
    for (auto& client : clients) client->CloseSocket();

    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(m_workersMutex);
        for (auto& entry : m_workers) {
            if (entry.second.joinable()) workers.push_back(std::move(entry.second));
        }
        m_workers.clear();
    }
    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clients.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_groupsMutex);
        m_groupMembers.clear();
        m_userGroups.clear();
    }
    Logger::Info("TCP      : Server stopped.");
}

void TcpServer::JoinFinishedWorkers() {
    std::vector<std::thread> finished;
    {
        std::lock_guard<std::mutex> lock(m_workersMutex);
        for (auto it = m_workers.begin(); it != m_workers.end();) {
            if (it->first->finished) {
                if (it->second.joinable()) finished.push_back(std::move(it->second));
                it = m_workers.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& worker : finished) {
        if (worker.joinable()) worker.join();
    }
}

void TcpServer::AcceptLoop() {
    while (m_running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_listenSocket, &readSet);
        timeval timeout{1, 0};
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (!m_running) break;
        if (ready == 0) {
            JoinFinishedWorkers();
            continue;
        }
        if (ready == SOCKET_ERROR) {
            if (m_running) Logger::Error("TCP      : select() failed");
            break;
        }

        sockaddr_in from{};
        int fromLen = sizeof(from);
        SOCKET clientSocket = accept(m_listenSocket,
                                     reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (clientSocket == INVALID_SOCKET) {
            if (m_running) Logger::Error("TCP      : accept() failed");
            break;
        }
        char ip[64] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));

        auto info = std::make_shared<ClientInfo>();
        info->socket = clientSocket;
        info->ip = ip;
        {
            std::lock_guard<std::mutex> lock(m_workersMutex);
            m_workers.emplace_back(info, std::thread([this, info] { ClientLoop(info); }));
        }
    }
}

void TcpServer::BroadcastLocal(const std::string& payload,
                               const std::string& excludeUserId) {
    std::vector<std::shared_ptr<ClientInfo>> targets;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& kv : m_clients) {
            if (kv.first == excludeUserId) continue;
            targets.push_back(kv.second);
        }
    }
    for (auto& target : targets) target->Send(payload);
}

void TcpServer::BroadcastGroupLocal(const std::string& groupId,
                                    const std::string& payload,
                                    const std::string& excludeUserId) {
    std::vector<std::string> members;
    {
        std::lock_guard<std::mutex> lock(m_groupsMutex);
        auto it = m_groupMembers.find(groupId);
        if (it != m_groupMembers.end()) {
            members.assign(it->second.begin(), it->second.end());
        }
    }
    std::vector<std::shared_ptr<ClientInfo>> targets;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (const auto& member : members) {
            if (member == excludeUserId) continue;
            auto it = m_clients.find(member);
            if (it != m_clients.end()) targets.push_back(it->second);
        }
    }
    for (auto& target : targets) target->Send(payload);
}

bool TcpServer::SendToUser(const std::string& userId, const std::string& payload) {
    std::shared_ptr<ClientInfo> client;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clients.find(userId);
        if (it == m_clients.end()) return false;
        client = it->second;
    }
    return client->Send(payload);
}

std::string TcpServer::BuildOnlineList() {
    std::set<std::string> users;
    if (m_redis) {
        for (const auto& user : m_redis->GetOnlineUsers()) users.insert(user);
    }
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (const auto& kv : m_clients) users.insert(kv.first);
    }
    return JoinCsv(users);
}

void TcpServer::SendPresenceList(const std::shared_ptr<ClientInfo>& client) {
    client->Send(JsonBuild({
        {"type", "presence_list"},
        {"users", BuildOnlineList()}
    }));
}

void TcpServer::SendGroupList(const std::shared_ptr<ClientInfo>& client) {
    std::vector<GroupRecord> groups;
    if (!m_db || !m_db->ListGroupsForUser(client->userId, groups)) {
        groups.clear();
    }
    if (groups.empty()) {
        // Fallback to the in-memory cache if SQL is unavailable.
        std::set<std::string> ids;
        {
            std::lock_guard<std::mutex> lock(m_groupsMutex);
            auto it = m_userGroups.find(client->userId);
            if (it != m_userGroups.end()) ids = it->second;
        }
        for (const auto& id : ids) {
            GroupRecord record;
            if (m_db && m_db->GetGroupInfo(id, record)) {
                groups.push_back(record);
            } else {
                record.groupId = id;
                record.groupName = id;
                groups.push_back(record);
            }
        }
    }

    for (const auto& group : groups) {
        client->Send(JsonBuild({
            {"type", "group"},
            {"groupId", group.groupId},
            {"groupName", group.groupName},
            {"ownerId", group.ownerId},
            {"memberCount", std::to_string(group.memberCount)}
        }));
    }
    client->Send(JsonBuild({{"type", "group_list_end"}}));
}

void TcpServer::SendError(const std::shared_ptr<ClientInfo>& client,
                          const std::string& message,
                          const std::string& clientMsgId) {
    client->Send(JsonBuild({
        {"type", "error"},
        {"message", message},
        {"clientMsgId", clientMsgId}
    }));
}

void TcpServer::SendHistory(const std::shared_ptr<ClientInfo>& client,
                            const std::string& targetId, bool isGroup, int limit) {
    if (targetId.empty()) {
        SendError(client, "History target is missing");
        return;
    }
    if (isGroup) {
        bool member = m_db && m_db->IsGroupMember(targetId, client->userId);
        if (!member) {
            std::lock_guard<std::mutex> lock(m_groupsMutex);
            auto it = m_userGroups.find(client->userId);
            member = it != m_userGroups.end() && it->second.count(targetId) > 0;
        }
        if (!member) {
            SendError(client, "You are not a member of this group");
            return;
        }
    }

    std::vector<HistoryRecord> records;
    if (!m_db || !m_db->GetHistory(client->userId, targetId, isGroup, limit, records)) {
        SendError(client, "Message history is temporarily unavailable");
        return;
    }

    for (const auto& record : records) {
        client->Send(JsonBuild({
            {"type", "history_item"},
            {"messageId", record.messageId},
            {"userId", record.userId},
            {"targetId", record.targetId},
            {"conversationType", record.conversationType},
            {"message", record.message},
            {"timestamp", record.timestamp},
            {"with", isGroup ? "" : targetId},
            {"groupId", isGroup ? targetId : ""}
        }));
    }
    client->Send(JsonBuild({
        {"type", "history_end"},
        {"with", isGroup ? "" : targetId},
        {"groupId", isGroup ? targetId : ""},
        {"count", std::to_string(records.size())}
    }));
}

void TcpServer::UpdateGroupCache(const std::string& groupId,
                                 const std::string& groupName,
                                 const std::vector<std::string>& members,
                                 std::vector<std::string>* added,
                                 std::vector<std::string>* removed) {
    (void)groupName;
    std::lock_guard<std::mutex> lock(m_groupsMutex);
    std::set<std::string> next(members.begin(), members.end());
    std::set<std::string> previous;
    auto existing = m_groupMembers.find(groupId);
    if (existing != m_groupMembers.end()) previous = existing->second;
    m_groupMembers[groupId] = next;

    if (added) added->clear();
    if (removed) removed->clear();

    for (const auto& user : next) {
        if (!previous.count(user)) {
            m_userGroups[user].insert(groupId);
            if (added) added->push_back(user);
        }
    }
    for (const auto& user : previous) {
        if (!next.count(user)) {
            auto it = m_userGroups.find(user);
            if (it != m_userGroups.end()) {
                it->second.erase(groupId);
                if (it->second.empty()) m_userGroups.erase(it);
            }
            if (removed) removed->push_back(user);
        }
    }
}

void TcpServer::NotifyLocalGroupChanges(const std::string& groupId,
                                        const std::string& groupName,
                                        const std::vector<std::string>& added,
                                        const std::vector<std::string>& removed) {
    for (const auto& user : added) {
        if (!SendToUser(user, JsonBuild({
                {"type", "group_added"},
                {"groupId", groupId},
                {"groupName", groupName}
            }))) {
            // Offline users simply receive the group in their next group list.
        }
    }
    for (const auto& user : removed) {
        SendToUser(user, JsonBuild({
            {"type", "group_removed"},
            {"groupId", groupId},
            {"groupName", groupName}
        }));
    }
}

void TcpServer::PublishGroupSync(const std::string& groupId,
                                 const std::string& groupName,
                                 const std::vector<std::string>& members) {
    if (!m_redis) return;
    m_redis->Publish(kBroadcastChannel, JsonBuild({
        {"type", "group_sync"},
        {"serverId", m_cfg.serverId},
        {"groupId", groupId},
        {"groupName", groupName},
        {"members", JoinCsv(members)}
    }));
}

void TcpServer::OnCrossServerMessage(const std::string& payload) {
    Logger::Info(
        "REDIS DEBUG: OnCrossServerMessage called; payload bytes: " +
        std::to_string(payload.size())
    );

    std::map<std::string, std::string> msg;
    if (!JsonParse(payload, msg)) {
        Logger::Warn("REDIS DEBUG: JSON parsing failed");
        return;
    }

    Logger::Info(
        "REDIS DEBUG: Parsed message type=" + JsonGet(msg, "type") +
        ", origin=" + JsonGet(msg, "serverId") +
        ", sender=" + JsonGet(msg, "userId") +
        ", target=" + JsonGet(msg, "targetId")
    );

    const std::string originServer = JsonGet(msg, "serverId");
    if (originServer == m_cfg.serverId) return;

    const std::string type = JsonGet(msg, "type");
    if (type == "chat" || type == "group_chat") {
        const std::string conversation = JsonGet(msg, "conversationType", "personal");
        const std::string targetId = JsonGet(msg, "targetId");
        const std::string sender = JsonGet(msg, "userId");
        if (type == "group_chat" || conversation == "group") {
            if (!targetId.empty()) BroadcastGroupLocal(targetId, payload, sender);
        } else if (!targetId.empty()) {
    const bool delivered = SendToUser(targetId, payload);

    Logger::Info(
        "Redis    : Cross-server chat from " + sender +
        " to " + targetId +
        " on " + m_cfg.serverId +
        (delivered ? " - delivered locally" : " - recipient not connected locally")
    );
} else {
            // Backwards-compatible global broadcast frame.
            BroadcastLocal(payload, sender);
        }
        return;
    }

    if (type == "typing") {
        const std::string groupId = JsonGet(msg, "groupId");
        const std::string to = JsonGet(msg, "to");
        const std::string sender = JsonGet(msg, "userId");
        if (!groupId.empty()) {
            BroadcastGroupLocal(groupId, payload, sender);
        } else if (!to.empty()) {
            SendToUser(to, payload);
        }
        return;
    }

    if (type == "group_sync") {
        const std::string groupId = JsonGet(msg, "groupId");
        if (groupId.empty()) return;
        std::vector<std::string> members = SplitCsv(JsonGet(msg, "members"));
        std::vector<std::string> added;
        std::vector<std::string> removed;
        UpdateGroupCache(groupId, JsonGet(msg, "groupName"), members, &added, &removed);
        NotifyLocalGroupChanges(groupId, JsonGet(msg, "groupName"), added, removed);
        return;
    }

    if (type == "presence") {
        BroadcastLocal(payload);
    }
}

bool TcpServer::HandleFrame(const std::shared_ptr<ClientInfo>& client,
                            const std::map<std::string, std::string>& msg) {
    const std::string type = JsonGet(msg, "type");
    const std::string timestamp = protocol::IsoTimestampUtc();

    if (type == "chat") {
        const std::string text = Trim(JsonGet(msg, "message"));
        const std::string to = JsonGet(msg, "to");
        const std::string clientMsgId = JsonGet(msg, "clientMsgId");
        if (text.empty()) {
            SendError(client, "Message text is empty", clientMsgId);
            return true;
        }
        if (text.size() > kMaxMessageLength) {
            SendError(client, "Message is too long (maximum 8000 characters)", clientMsgId);
            return true;
        }
        if (!to.empty() && to != "*" && !IsValidUserId(to)) {
            SendError(client, "Invalid recipient", clientMsgId);
            return true;
        }
        if (!to.empty() && to != "*" && m_db) {
            UserRecord recipient;
            if (!m_db->GetUser(to, recipient)) {
                SendError(client, "Recipient not found", clientMsgId);
                return true;
            }
        }

        const std::string messageId =
            MakeMessageId(m_cfg.serverId, ++m_messageSeq);
        const std::string outgoing = JsonBuild({
            {"type", "chat"},
            {"messageId", messageId},
            {"userId", client->userId},
            {"targetId", to},
            {"to", to},
            {"conversationType", "personal"},
            {"serverId", m_cfg.serverId},
            {"timestamp", timestamp},
            {"message", text}
        });

        if (to.empty() || to == "*") {
            BroadcastLocal(outgoing, client->userId);
        } else if (to != client->userId) {
            SendToUser(to, outgoing);
        }

        if (m_redis) {
            m_redis->Publish(kBroadcastChannel, outgoing);
        }
        if (m_kafka) {
            m_kafka->Enqueue(JsonBuild({
                {"messageId", messageId},
                {"userId", client->userId},
                {"serverId", m_cfg.serverId},
                {"timestamp", timestamp},
                {"message", text},
                {"conversationType", "personal"},
                {"targetId", to}
            }));
        }
        client->Send(JsonBuild({
            {"type", "ack"},
            {"clientMsgId", clientMsgId},
            {"messageId", messageId},
            {"timestamp", timestamp},
            {"status", "sent"}
        }));
        Logger::Info("TCP      : Personal message from " + client->userId +
                     " to " + (to.empty() ? "*" : to));
        return true;
    }

    if (type == "group_chat") {
        const std::string groupId = JsonGet(msg, "groupId");
        const std::string text = Trim(JsonGet(msg, "message"));
        const std::string clientMsgId = JsonGet(msg, "clientMsgId");
        if (groupId.empty()) {
            SendError(client, "Group id is missing", clientMsgId);
            return true;
        }
        if (text.empty()) {
            SendError(client, "Message text is empty", clientMsgId);
            return true;
        }
        if (text.size() > kMaxMessageLength) {
            SendError(client, "Message is too long (maximum 8000 characters)", clientMsgId);
            return true;
        }

        bool member = m_db && m_db->IsGroupMember(groupId, client->userId);
        if (!member) {
            std::lock_guard<std::mutex> lock(m_groupsMutex);
            auto it = m_userGroups.find(client->userId);
            member = it != m_userGroups.end() && it->second.count(groupId) > 0;
        }
        if (!member) {
            SendError(client, "You are not a member of this group", clientMsgId);
            return true;
        }

        GroupRecord group;
        if (m_db) m_db->GetGroupInfo(groupId, group);
        const std::string groupName = group.groupName.empty() ? groupId : group.groupName;
        const std::string messageId =
            MakeMessageId(m_cfg.serverId, ++m_messageSeq);
        const std::string outgoing = JsonBuild({
            {"type", "group_chat"},
            {"messageId", messageId},
            {"userId", client->userId},
            {"groupId", groupId},
            {"groupName", groupName},
            {"targetId", groupId},
            {"conversationType", "group"},
            {"serverId", m_cfg.serverId},
            {"timestamp", timestamp},
            {"message", text}
        });

        BroadcastGroupLocal(groupId, outgoing, client->userId);
        if (m_redis) m_redis->Publish(kBroadcastChannel, outgoing);
        if (m_kafka) {
            m_kafka->Enqueue(JsonBuild({
                {"messageId", messageId},
                {"userId", client->userId},
                {"serverId", m_cfg.serverId},
                {"timestamp", timestamp},
                {"message", text},
                {"conversationType", "group"},
                {"targetId", groupId},
                {"targetName", groupName}
            }));
        }
        client->Send(JsonBuild({
            {"type", "ack"},
            {"clientMsgId", clientMsgId},
            {"messageId", messageId},
            {"timestamp", timestamp},
            {"status", "sent"}
        }));
        Logger::Info("TCP      : Group message from " + client->userId +
                     " to " + groupId);
        return true;
    }

    if (type == "group_create") {
        const std::string name = Trim(JsonGet(msg, "name"));
        if (name.empty() || name.size() > 128) {
            SendError(client, "Group name must be between 1 and 128 characters");
            return true;
        }
        const std::string groupId = MakeGroupId();
        if (!m_db || !m_db->CreateGroup(groupId, name, client->userId)) {
            SendError(client, "Could not create the group (database unavailable)");
            return true;
        }
        if (!m_db->AddGroupMember(groupId, client->userId)) {
            SendError(client, "Could not add the owner to the group");
            return true;
        }

        std::vector<std::string> members{client->userId};
        for (const auto& member : SplitCsv(JsonGet(msg, "members"))) {
            if (member == client->userId || !IsValidUserId(member)) continue;
            UserRecord user;
            if (!m_db->GetUser(member, user)) continue;
            if (m_db->AddGroupMember(groupId, member)) members.push_back(member);
        }

        std::vector<std::string> added;
        std::vector<std::string> removed;
        UpdateGroupCache(groupId, name, members, &added, &removed);
        added.erase(std::remove(added.begin(), added.end(), client->userId), added.end());
        NotifyLocalGroupChanges(groupId, name, added, removed);
        PublishGroupSync(groupId, name, members);

        client->Send(JsonBuild({
            {"type", "group_created"},
            {"groupId", groupId},
            {"groupName", name},
            {"ownerId", client->userId},
            {"memberCount", std::to_string(members.size())},
            {"members", JoinCsv(members)}
        }));
        Logger::Info("TCP      : Group created: " + name + " (" + groupId + ")");
        return true;
    }

    if (type == "group_join") {
        const std::string groupId = JsonGet(msg, "groupId");
        GroupRecord group;
        if (!m_db || !m_db->GetGroupInfo(groupId, group)) {
            SendError(client, "Group not found");
            return true;
        }
        if (!m_db->AddGroupMember(groupId, client->userId)) {
            SendError(client, "Could not join the group");
            return true;
        }

        std::vector<std::string> members;
        if (!m_db->GetGroupMembers(groupId, members)) {
            SendError(client, "Could not read group membership");
            return true;
        }
        if (std::find(members.begin(), members.end(), client->userId) == members.end()) {
            members.push_back(client->userId);
        }
        std::vector<std::string> added;
        std::vector<std::string> removed;
        UpdateGroupCache(groupId, group.groupName, members, &added, &removed);
        added.erase(std::remove(added.begin(), added.end(), client->userId), added.end());
        NotifyLocalGroupChanges(groupId, group.groupName, added, removed);
        PublishGroupSync(groupId, group.groupName, members);

        GroupRecord updated;
        m_db->GetGroupInfo(groupId, updated);
        client->Send(JsonBuild({
            {"type", "group_joined"},
            {"groupId", groupId},
            {"groupName", group.groupName},
            {"ownerId", group.ownerId},
            {"memberCount", std::to_string(updated.memberCount)},
            {"members", JoinCsv(members)}
        }));
        return true;
    }

    if (type == "group_leave") {
        const std::string groupId = JsonGet(msg, "groupId");
        GroupRecord group;
        if (m_db) m_db->GetGroupInfo(groupId, group);
        if (!m_db || !m_db->RemoveGroupMember(groupId, client->userId)) {
            SendError(client, "Could not leave the group");
            return true;
        }
        std::vector<std::string> members;
        if (m_db) m_db->GetGroupMembers(groupId, members);
        members.erase(std::remove(members.begin(), members.end(), client->userId), members.end());
        std::vector<std::string> added;
        std::vector<std::string> removed;
        UpdateGroupCache(groupId, group.groupName, members, &added, &removed);
        PublishGroupSync(groupId, group.groupName, members);
        client->Send(JsonBuild({
            {"type", "group_left"},
            {"groupId", groupId},
            {"groupName", group.groupName}
        }));
        return true;
    }

    if (type == "group_list") {
        SendGroupList(client);
        return true;
    }

    if (type == "history") {
        const std::string groupId = JsonGet(msg, "groupId");
        const std::string peer = JsonGet(msg, "with");
        const int limit = JsonGetInt(msg, "limit", 100);
        if (!groupId.empty()) {
            SendHistory(client, groupId, true, limit);
        } else {
            SendHistory(client, peer, false, limit);
        }
        return true;
    }

    if (type == "typing") {
        const std::string to = JsonGet(msg, "to");
        const std::string groupId = JsonGet(msg, "groupId");
        const bool isTyping = JsonGetBool(msg, "isTyping", false);
        const std::string outgoing = JsonBuild({
            {"type", "typing"},
            {"userId", client->userId},
            {"to", to},
            {"groupId", groupId},
            {"isTyping", isTyping ? "true" : "false"},
            {"timestamp", timestamp}
        });
        if (!groupId.empty()) {
            BroadcastGroupLocal(groupId, outgoing, client->userId);
        } else if (!to.empty()) {
            SendToUser(to, outgoing);
        }
        if (m_redis) m_redis->Publish(kBroadcastChannel, outgoing);
        return true;
    }

    if (type == "presence_list") {
        SendPresenceList(client);
        return true;
    }

    if (type == "ping") {
        client->Send(JsonBuild({{"type", "pong"}, {"timestamp", timestamp}}));
        return true;
    }

    if (type == "logout") {
        client->Send(JsonBuild({{"type", "bye"}, {"message", "Signed out"}}));
        return false;
    }

    SendError(client, "Unsupported message type: " + type);
    return true;
}

void TcpServer::CleanupClient(const std::shared_ptr<ClientInfo>& client, bool wasCurrent) {
    if (wasCurrent) {
        if (m_redis) {
            m_redis->ClearOnline(client->userId);
            m_redis->Publish(kPresenceChannel, JsonBuild({
                {"type", "presence"},
                {"userId", client->userId},
                {"online", "false"},
                {"serverId", m_cfg.serverId}
            }));
        }
        BroadcastLocal(JsonBuild({
            {"type", "presence"},
            {"userId", client->userId},
            {"online", "false"},
            {"serverId", m_cfg.serverId}
        }));
    }
    client->CloseSocket();
    client->finished = true;
    Logger::Info("TCP      : User disconnected: " + client->userId + " (" +
                 client->ip + ")");
}

void TcpServer::ClientLoop(std::shared_ptr<ClientInfo> client) {
    // ---- Authentication phase ----
    std::string frame;
    if (!protocol::RecvFrame(client->socket, frame, 10000)) {
        client->CloseSocket();
        client->finished = true;
        return;
    }

    std::map<std::string, std::string> auth;
    if (!JsonParse(frame, auth) || JsonGet(auth, "type") != "auth") {
        client->Send(JsonBuild({{"type", "error"}, {"message", "Expected auth frame"}}));
        client->alive = false;
        CleanupClient(client, false);
        return;
    }

    const std::string userId = Trim(JsonGet(auth, "userId"));
    const std::string password = JsonGet(auth, "password");
    if (!IsValidUserId(userId)) {
        client->Send(JsonBuild({{"type", "error"}, {"message", "Invalid user id"}}));
        client->alive = false;
        CleanupClient(client, false);
        return;
    }
    client->userId = userId;

    bool cacheHit = false;
    bool valid = m_redis && m_redis->ValidateUser(userId, password, client->ip, cacheHit);
    if (!valid) {
        Logger::Info("TCP      : Authentication failed for " + userId);
        client->Send(JsonBuild({{"type", "error"},
                                {"message", "Invalid credentials"}}));
        client->alive = false;
        CleanupClient(client, false);
        return;
    }

    // Messages do not carry a per-frame timeout: the TCP connection is kept
    // alive by the client and is explicitly shutdown on logout/disconnect.
    DWORD noTimeout = 0;
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&noTimeout), sizeof(noTimeout));

    std::shared_ptr<ClientInfo> previous;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto existing = m_clients.find(userId);
        if (existing != m_clients.end()) previous = existing->second;
        m_clients[userId] = client;
    }
    if (previous && previous != client) {
        previous->Send(JsonBuild({{"type", "error"},
                                  {"message", "Signed in from another location"}}));
        previous->alive = false;
        previous->CloseSocket();
    }

    // Load group membership into the local routing cache.
    if (m_db) {
        std::vector<std::string> groupIds;
        if (m_db->GetUserGroups(userId, groupIds)) {
            std::lock_guard<std::mutex> lock(m_groupsMutex);
            for (const auto& groupId : groupIds) {
                m_userGroups[userId].insert(groupId);
                m_groupMembers[groupId].insert(userId);
            }
        }
    }

    if (m_heartbeat) m_heartbeat->NoteUserConnected(userId, client->ip);
    if (m_redis) m_redis->SetOnline(userId, m_cfg.serverId);

    client->Send(JsonBuild({
        {"type", "auth_ok"},
        {"userId", userId},
        {"serverId", m_cfg.serverId},
        {"message", "Welcome " + userId}
    }));

    std::vector<std::string> allUsers;
    if (m_db) m_db->ListUsers(allUsers);
    client->Send(JsonBuild({
        {"type", "user_list"},
        {"users", JoinCsv(allUsers)}
    }));
    SendPresenceList(client);
    SendGroupList(client);

    const std::string presenceOnline = JsonBuild({
        {"type", "presence"},
        {"userId", userId},
        {"online", "true"},
        {"serverId", m_cfg.serverId}
    });
    BroadcastLocal(presenceOnline, userId);
    if (m_redis) m_redis->Publish(kPresenceChannel, presenceOnline);
    Logger::Info("TCP      : User connected: " + userId + " (" + client->ip +
                 ", " + m_cfg.serverId + ")");

    // ---- Chat phase ----
    while (m_running && client->alive) {
        frame.clear();
        if (!protocol::RecvFrame(client->socket, frame, 0)) break;
        std::map<std::string, std::string> msg;
        if (!JsonParse(frame, msg)) {
            SendError(client, "Malformed JSON frame");
            continue;
        }
        if (!HandleFrame(client, msg)) break;
    }

    bool wasCurrent = false;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clients.find(userId);
        if (it != m_clients.end() && it->second == client) {
            m_clients.erase(it);
            wasCurrent = true;
        }
    }
    client->alive = false;
    CleanupClient(client, wasCurrent);
}
