#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ServerConfig.h"

struct UserRecord {
    std::string userId;
    std::string passwordHash;
    std::string ipAddress;
};

struct GroupRecord {
    std::string groupId;
    std::string groupName;
    std::string ownerId;
    int memberCount = 0;
};

struct HistoryRecord {
    std::string messageId;
    std::string userId;
    std::string targetId;
    std::string conversationType;
    std::string message;
    std::string timestamp;
};

// Native Windows ODBC access to MySQL. No JDBC / Connector-C++ classes are
// used anywhere in this class.
class MySQLDatabase {
public:
    ~MySQLDatabase();

    bool Connect(const ServerConfig& config);
    bool IsConnected() const;
    bool GetUser(const std::string& userId, UserRecord& out);
    bool ListUsers(std::vector<std::string>& userIds);

    bool GetUserGroups(const std::string& userId, std::vector<std::string>& groupIds);
    bool CreateGroup(const std::string& groupId, const std::string& groupName,
                     const std::string& ownerId);
    bool AddGroupMember(const std::string& groupId, const std::string& userId);
    bool RemoveGroupMember(const std::string& groupId, const std::string& userId);
    bool IsGroupMember(const std::string& groupId, const std::string& userId);
    bool GetGroupMembers(const std::string& groupId, std::vector<std::string>& userIds);
    bool GetGroupInfo(const std::string& groupId, GroupRecord& out);
    bool ListGroupsForUser(const std::string& userId, std::vector<GroupRecord>& groups);
    bool GetHistory(const std::string& userId, const std::string& targetId,
                    bool isGroup, int limit, std::vector<HistoryRecord>& messages);

    void Close();

private:
    bool ConnectLocked();
    bool EnsureConnectedLocked();
    void CloseLocked();

    mutable std::mutex m_mutex;
    ServerConfig m_cfg;
    void* m_env = nullptr; // SQLHENV
    void* m_dbc = nullptr; // SQLHDBC
    bool m_connected = false;
};
