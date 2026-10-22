#include "MySQLDatabase.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../Common/Logger.h"

namespace {
    std::string OdbcError(SQLSMALLINT handleType, SQLHANDLE handle) {
        SQLCHAR state[6] = {};
        SQLCHAR message[1024] = {};
        SQLINTEGER nativeError = 0;
        SQLSMALLINT messageLength = 0;
        SQLRETURN rc = SQLGetDiagRecA(handleType, handle, 1, state, &nativeError,
                                     message, sizeof(message), &messageLength);
        if (SQL_SUCCEEDED(rc)) {
            return std::string(reinterpret_cast<char*>(state)) + ": " +
                   reinterpret_cast<char*>(message);
        }
        return "No ODBC diagnostic information available";
    }

    std::string BraceValue(const std::string& value) {
        std::string result = "{";
        for (char ch : value) {
            result += ch;
            if (ch == '}') result += '}';
        }
        result += "}";
        return result;
    }

    std::string BuildConnectionString(const ServerConfig& c) {
        return "DRIVER=" + BraceValue(c.mysqlDriver) + ";" +
               "SERVER=" + c.mysqlHost + ";" +
               "PORT=" + std::to_string(c.mysqlPort) + ";" +
               "DATABASE=" + c.mysqlDatabase + ";" +
               "UID=" + c.mysqlUser + ";" +
               "PWD=" + BraceValue(c.mysqlPassword) + ";" +
               "CHARSET=utf8mb4;";
    }

    bool BindText(SQLHSTMT stmt, SQLUSMALLINT index, const std::string& value) {
        // A null StrLen_or_IndPtr tells ODBC the input string is
        // null-terminated. The previous code passed the address of a local
        // SQL_NTS indicator, which went out of scope before SQLExecute and
        // made parameterised lookups fail unpredictably.
        return SQL_SUCCEEDED(SQLBindParameter(
            stmt, index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            static_cast<SQLULEN>(std::max<size_t>(value.size(), 1)), 0,
            const_cast<char*>(value.c_str()),
            static_cast<SQLLEN>(value.size() + 1), nullptr));
    }

    bool ReadString(SQLHSTMT stmt, SQLUSMALLINT column, std::string& out) {
        out.clear();
        char buffer[1024];
        SQLLEN indicator = 0;
        for (;;) {
            SQLRETURN rc = SQLGetData(stmt, column, SQL_C_CHAR, buffer,
                                      sizeof(buffer), &indicator);
            if (rc == SQL_NO_DATA) break;
            if (!SQL_SUCCEEDED(rc)) return false;
            if (indicator == SQL_NULL_DATA) {
                out.clear();
                break;
            }
            size_t length = 0;
            if (indicator != SQL_NTS && indicator > 0) {
                size_t remaining = static_cast<size_t>(indicator) - out.size();
                length = std::min(remaining, sizeof(buffer) - 1);
            } else {
                length = strlen(buffer);
            }
            out.append(buffer, length);
            if (rc == SQL_SUCCESS) break;
        }
        return true;
    }

    bool IsMissingSchemaError(const std::string& text) {
        return text.find("42S02") != std::string::npos ||
               text.find("42S22") != std::string::npos ||
               text.find("Unknown column") != std::string::npos ||
               text.find("doesn't exist") != std::string::npos;
    }
}

MySQLDatabase::~MySQLDatabase() {
    Close();
}

bool MySQLDatabase::Connect(const ServerConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cfg = config;
    return ConnectLocked();
}

bool MySQLDatabase::ConnectLocked() {
    CloseLocked();
    m_connected = false;

    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;

    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("SQL      : ODBC environment allocation failed");
        return false;
    }
    rc = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION,
                       reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("SQL      : " + OdbcError(SQL_HANDLE_ENV, env));
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return false;
    }
    rc = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("SQL      : ODBC connection allocation failed");
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return false;
    }

    SQLSetConnectAttr(dbc, SQL_ATTR_LOGIN_TIMEOUT,
                      reinterpret_cast<SQLPOINTER>(static_cast<SQLLEN>(5)), 0);

    std::string connectionString = BuildConnectionString(m_cfg);
    SQLCHAR output[1024] = {};
    SQLSMALLINT outputLength = 0;
    rc = SQLDriverConnectA(dbc, nullptr,
                           reinterpret_cast<SQLCHAR*>(
                               const_cast<char*>(connectionString.c_str())),
                           SQL_NTS, output, sizeof(output), &outputLength,
                           SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(rc)) {
        std::string error = OdbcError(SQL_HANDLE_DBC, dbc);
        Logger::Error("SQL      : ODBC connect failed: " + error);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        if (error.find("IM002") != std::string::npos) {
            Logger::Error("SQL      : ODBC driver '" + m_cfg.mysqlDriver +
                          "' is not installed. Set mysql odbc_driver in the server ini.");
        }
        return false;
    }

    m_env = env;
    m_dbc = dbc;
    m_connected = true;
    Logger::Info("SQL      : Connected to MySQL through ODBC (db: " +
                 m_cfg.mysqlDatabase + ")");
    return true;
}

bool MySQLDatabase::EnsureConnectedLocked() {
    if (m_connected && m_dbc != nullptr) return true;
    return ConnectLocked();
}

bool MySQLDatabase::IsConnected() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connected && m_dbc != nullptr;
}

bool MySQLDatabase::GetUser(const std::string& userId, UserRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLHDBC dbc = reinterpret_cast<SQLHDBC>(m_dbc);
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    bool ok = false;
    const char* query =
        "SELECT UserId, PasswordHash, IPAddress "
        "FROM tbl_UserDetails WHERE UserId = ?";
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) && BindText(stmt, 1, userId) && SQL_SUCCEEDED(SQLExecute(stmt))) {
        rc = SQLFetch(stmt);
        if (SQL_SUCCEEDED(rc)) {
            ReadString(stmt, 1, out.userId);
            ReadString(stmt, 2, out.passwordHash);
            ReadString(stmt, 3, out.ipAddress);
            ok = true;
        }
    } else {
        std::string error = OdbcError(SQL_HANDLE_STMT, stmt);
        Logger::Error("SQL      : GetUser failed: " + error);
        if (IsMissingSchemaError(error)) m_connected = false;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

bool MySQLDatabase::ListUsers(std::vector<std::string>& userIds) {
    userIds.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query = "SELECT UserId FROM tbl_UserDetails ORDER BY UserId";
    rc = SQLExecDirectA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc)) {
        while (SQL_SUCCEEDED(SQLFetch(stmt))) {
            std::string id;
            if (ReadString(stmt, 1, id) && !id.empty()) userIds.push_back(id);
        }
    } else {
        Logger::Warn("SQL      : ListUsers failed: " + OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return true;
}

bool MySQLDatabase::GetUserGroups(const std::string& userId,
                                  std::vector<std::string>& groupIds) {
    groupIds.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query = "SELECT GroupId FROM tbl_GroupMembers WHERE UserId = ?";
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) && BindText(stmt, 1, userId) && SQL_SUCCEEDED(SQLExecute(stmt))) {
        while (SQL_SUCCEEDED(SQLFetch(stmt))) {
            std::string id;
            if (ReadString(stmt, 1, id) && !id.empty()) groupIds.push_back(id);
        }
    } else {
        Logger::Warn("SQL      : Group membership schema not available: " +
                     OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return true;
}

bool MySQLDatabase::CreateGroup(const std::string& groupId, const std::string& groupName,
                                const std::string& ownerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query =
        "INSERT INTO tbl_Groups (GroupId, GroupName, OwnerId, CreatedAt) "
        "VALUES (?, ?, ?, NOW())";
    bool ok = false;
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) &&
        BindText(stmt, 1, groupId) &&
        BindText(stmt, 2, groupName) &&
        BindText(stmt, 3, ownerId) &&
        SQL_SUCCEEDED(SQLExecute(stmt))) {
        ok = true;
    } else {
        Logger::Error("SQL      : CreateGroup failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

bool MySQLDatabase::AddGroupMember(const std::string& groupId, const std::string& userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query =
        "INSERT INTO tbl_GroupMembers (GroupId, UserId, JoinedAt) "
        "VALUES (?, ?, NOW()) ON DUPLICATE KEY UPDATE UserId = UserId";
    bool ok = false;
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) &&
        BindText(stmt, 1, groupId) &&
        BindText(stmt, 2, userId) &&
        SQL_SUCCEEDED(SQLExecute(stmt))) {
        ok = true;
    } else {
        Logger::Error("SQL      : AddGroupMember failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

bool MySQLDatabase::RemoveGroupMember(const std::string& groupId,
                                      const std::string& userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query =
        "DELETE FROM tbl_GroupMembers WHERE GroupId = ? AND UserId = ?";
    bool ok = false;
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) &&
        BindText(stmt, 1, groupId) &&
        BindText(stmt, 2, userId) &&
        SQL_SUCCEEDED(SQLExecute(stmt))) {
        ok = true;
    } else {
        Logger::Error("SQL      : RemoveGroupMember failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

bool MySQLDatabase::IsGroupMember(const std::string& groupId, const std::string& userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query =
        "SELECT 1 FROM tbl_GroupMembers WHERE GroupId = ? AND UserId = ?";
    bool member = false;
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) &&
        BindText(stmt, 1, groupId) &&
        BindText(stmt, 2, userId) &&
        SQL_SUCCEEDED(SQLExecute(stmt))) {
        member = (SQLFetch(stmt) == SQL_SUCCESS);
    } else {
        Logger::Error("SQL      : IsGroupMember failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return member;
}

bool MySQLDatabase::GetGroupMembers(const std::string& groupId,
                                    std::vector<std::string>& userIds) {
    userIds.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query = "SELECT UserId FROM tbl_GroupMembers WHERE GroupId = ?";
    bool ok = false;
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) && BindText(stmt, 1, groupId) && SQL_SUCCEEDED(SQLExecute(stmt))) {
        while (SQL_SUCCEEDED(SQLFetch(stmt))) {
            std::string id;
            if (ReadString(stmt, 1, id) && !id.empty()) userIds.push_back(id);
        }
        ok = true;
    } else {
        Logger::Error("SQL      : GetGroupMembers failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

bool MySQLDatabase::GetGroupInfo(const std::string& groupId, GroupRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query =
        "SELECT g.GroupId, g.GroupName, g.OwnerId, "
        "(SELECT COUNT(*) FROM tbl_GroupMembers m WHERE m.GroupId = g.GroupId) "
        "FROM tbl_Groups g WHERE g.GroupId = ?";
    bool ok = false;
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) && BindText(stmt, 1, groupId) && SQL_SUCCEEDED(SQLExecute(stmt))) {
        if (SQL_SUCCEEDED(SQLFetch(stmt))) {
            ReadString(stmt, 1, out.groupId);
            ReadString(stmt, 2, out.groupName);
            ReadString(stmt, 3, out.ownerId);
            std::string count;
            ReadString(stmt, 4, count);
            try { out.memberCount = std::stoi(count); } catch (...) { out.memberCount = 0; }
            ok = true;
        }
    } else {
        Logger::Error("SQL      : GetGroupInfo failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

bool MySQLDatabase::ListGroupsForUser(const std::string& userId,
                                      std::vector<GroupRecord>& groups) {
    groups.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    const char* query =
        "SELECT g.GroupId, g.GroupName, g.OwnerId, "
        "(SELECT COUNT(*) FROM tbl_GroupMembers m WHERE m.GroupId = g.GroupId) "
        "FROM tbl_Groups g "
        "INNER JOIN tbl_GroupMembers gm ON gm.GroupId = g.GroupId "
        "WHERE gm.UserId = ? ORDER BY g.GroupName";
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
    if (SQL_SUCCEEDED(rc) && BindText(stmt, 1, userId) && SQL_SUCCEEDED(SQLExecute(stmt))) {
        while (SQL_SUCCEEDED(SQLFetch(stmt))) {
            GroupRecord group;
            ReadString(stmt, 1, group.groupId);
            ReadString(stmt, 2, group.groupName);
            ReadString(stmt, 3, group.ownerId);
            std::string count;
            ReadString(stmt, 4, count);
            try { group.memberCount = std::stoi(count); } catch (...) { group.memberCount = 0; }
            groups.push_back(group);
        }
    } else {
        Logger::Warn("SQL      : ListGroupsForUser failed: " +
                     OdbcError(SQL_HANDLE_STMT, stmt));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return true;
}

bool MySQLDatabase::GetHistory(const std::string& userId, const std::string& targetId,
                               bool isGroup, int limit,
                               std::vector<HistoryRecord>& messages) {
    messages.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureConnectedLocked()) return false;
    if (limit <= 0) limit = 100;
    if (limit > 500) limit = 500;

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                  reinterpret_cast<SQLHDBC>(m_dbc), &stmt);
    if (!SQL_SUCCEEDED(rc)) return false;

    std::string query;
    if (isGroup) {
        query =
            "SELECT MessageId, UserId, TargetId, ConversationType, ChatMessage, "
            "DATE_FORMAT(Timestamp, '%Y-%m-%dT%H:%i:%sZ') "
            "FROM tbl_ChatDetails WHERE ConversationType = 'group' AND TargetId = ? "
            "ORDER BY UniqueId DESC LIMIT " + std::to_string(limit);
    } else {
        query =
            "SELECT MessageId, UserId, TargetId, ConversationType, ChatMessage, "
            "DATE_FORMAT(Timestamp, '%Y-%m-%dT%H:%i:%sZ') "
            "FROM tbl_ChatDetails WHERE ConversationType = 'personal' AND "
            "((UserId = ? AND TargetId = ?) OR (UserId = ? AND TargetId = ?)) "
            "ORDER BY UniqueId DESC LIMIT " + std::to_string(limit);
    }

    bool ok = false;
    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(
        const_cast<char*>(query.c_str())), SQL_NTS);
    if (SQL_SUCCEEDED(rc)) {
        bool bound = isGroup ? BindText(stmt, 1, targetId)
                             : (BindText(stmt, 1, userId) && BindText(stmt, 2, targetId) &&
                                BindText(stmt, 3, targetId) && BindText(stmt, 4, userId));
        if (bound && SQL_SUCCEEDED(SQLExecute(stmt))) {
            while (SQL_SUCCEEDED(SQLFetch(stmt))) {
                HistoryRecord record;
                ReadString(stmt, 1, record.messageId);
                ReadString(stmt, 2, record.userId);
                ReadString(stmt, 3, record.targetId);
                ReadString(stmt, 4, record.conversationType);
                ReadString(stmt, 5, record.message);
                ReadString(stmt, 6, record.timestamp);
                messages.push_back(record);
            }
            std::reverse(messages.begin(), messages.end());
            ok = true;
        }
    }
    if (!ok) {
        std::string error = OdbcError(SQL_HANDLE_STMT, stmt);
        Logger::Warn("SQL      : GetHistory failed (" + error +
                     "). Apply database/migration_v2_groups.sql.");
        if (IsMissingSchemaError(error)) m_connected = false;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

void MySQLDatabase::CloseLocked() {
    if (m_dbc != nullptr) {
        SQLHDBC dbc = reinterpret_cast<SQLHDBC>(m_dbc);
        if (m_connected) SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        m_dbc = nullptr;
    }
    if (m_env != nullptr) {
        SQLFreeHandle(SQL_HANDLE_ENV, reinterpret_cast<SQLHENV>(m_env));
        m_env = nullptr;
    }
    m_connected = false;
}

void MySQLDatabase::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    CloseLocked();
}
