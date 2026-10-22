#include "DatabaseWriter.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <string>

#include "../Common/Logger.h"

#pragma comment(lib, "odbc32.lib")

namespace {
    std::string NormalizeTimestamp(std::string ts) {
        for (char& ch : ts) {
            if (ch == 'T') ch = ' ';
            if (ch == 'Z') ch = ' ';
        }
        while (!ts.empty() && ts.back() == ' ') ts.pop_back();
        return ts;
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

    std::string OdbcError(SQLSMALLINT handleType, SQLHANDLE handle) {
        SQLCHAR state[6] = {};
        SQLCHAR message[1024] = {};
        SQLINTEGER nativeError = 0;
        SQLSMALLINT messageLength = 0;

        SQLRETURN rc = SQLGetDiagRecA(
            handleType, handle, 1, state, &nativeError,
            message, sizeof(message), &messageLength);

        if (SQL_SUCCEEDED(rc)) {
            return std::string(reinterpret_cast<char*>(state)) +
                ": " + reinterpret_cast<char*>(message);
        }
        return "No ODBC diagnostic information available";
    }

    void ReleaseOdbc(SQLHENV& env, SQLHDBC& conn, bool connected) {
        if (conn != SQL_NULL_HDBC) {
            if (connected) SQLDisconnect(conn);
            SQLFreeHandle(SQL_HANDLE_DBC, conn);
            conn = SQL_NULL_HDBC;
        }
        if (env != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            env = SQL_NULL_HENV;
        }
    }

    bool BindText(SQLHSTMT stmt, SQLUSMALLINT index,
                  const std::string& value, SQLLEN& indicator) {
        indicator = SQL_NTS;
        return SQL_SUCCEEDED(SQLBindParameter(
            stmt, index, SQL_PARAM_INPUT,
            SQL_C_CHAR, SQL_VARCHAR,
            static_cast<SQLULEN>(std::max<size_t>(value.size(), 1)),
            0,
            const_cast<char*>(value.c_str()),
            static_cast<SQLLEN>(value.size() + 1),
            &indicator));
    }

    bool IsMissingColumnError(const std::string& error) {
        return error.find("42S22") != std::string::npos ||
               error.find("Unknown column") != std::string::npos;
    }

    std::string ConnectionString(const DbWriterConfig& config) {
        return "DRIVER=" + BraceValue(config.mysqlDriver) + ";" +
               "SERVER=" + config.mysqlHost + ";" +
               "PORT=" + std::to_string(config.mysqlPort) + ";" +
               "DATABASE=" + config.mysqlDb + ";" +
               "UID=" + config.mysqlUser + ";" +
               "PWD=" + BraceValue(config.mysqlPassword) + ";" +
               "CHARSET=utf8mb4;";
    }
}

bool DatabaseWriter::Connect(const DbWriterConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ReleaseAll();
    m_cfg = config;
    m_connected = false;

    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC conn = SQL_NULL_HDBC;

    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("DBWriter  : ODBC environment allocation failed");
        return false;
    }

    rc = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION,
                       reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("DBWriter  : " + OdbcError(SQL_HANDLE_ENV, env));
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return false;
    }

    rc = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("DBWriter  : ODBC connection allocation failed");
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return false;
    }

    SQLSetConnectAttr(conn, SQL_ATTR_LOGIN_TIMEOUT,
                      reinterpret_cast<SQLPOINTER>(static_cast<SQLLEN>(5)), 0);

    std::string connectionString = ConnectionString(config);
    SQLCHAR output[1024] = {};
    SQLSMALLINT outputLength = 0;
    rc = SQLDriverConnectA(
        conn, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(connectionString.c_str())),
        SQL_NTS, output, sizeof(output), &outputLength,
        SQL_DRIVER_NOPROMPT);

    if (!SQL_SUCCEEDED(rc)) {
        std::string error = OdbcError(SQL_HANDLE_DBC, conn);
        Logger::Error("DBWriter  : ODBC connect failed: " + error);
        if (error.find("IM002") != std::string::npos) {
            Logger::Error("DBWriter  : driver '" + config.mysqlDriver +
                          "' not found. Install MySQL Connector/ODBC (64-bit) "
                          "and set odbc_driver in config/dbwriter.ini.");
        }
        SQLFreeHandle(SQL_HANDLE_DBC, conn);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return false;
    }

    m_envRaw = env;
    m_connRaw = conn;
    m_connected = true;
    Logger::Info("DBWriter  : connected to MySQL through ODBC (" +
                 config.mysqlDb + ")");

    // Detect whether migration_v2_groups.sql has been applied.
    SQLHSTMT probe = SQL_NULL_HSTMT;
    if (SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, conn, &probe))) {
        const char* query = "SELECT ConversationType, TargetId FROM tbl_ChatDetails LIMIT 1";
        SQLRETURN probeRc = SQLExecDirectA(
            probe, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query)), SQL_NTS);
        if (!SQL_SUCCEEDED(probeRc)) {
            m_schemaExtended = false;
            Logger::Warn("DBWriter  : ConversationType/TargetId columns missing. "
                         "Run database/migration_v2_groups.sql. Using legacy insert.");
        }
        SQLFreeHandle(SQL_HANDLE_STMT, probe);
    }
    return true;
}

bool DatabaseWriter::EnsureConnected(const DbWriterConfig& config) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_connected && m_connRaw != nullptr) return true;
    }
    return Connect(config);
}

bool DatabaseWriter::InsertMessage(const std::string& messageId,
                                   const std::string& userId,
                                   const std::string& timestamp,
                                   const std::string& message) {
    return InsertMessage(messageId, userId, timestamp, message, "personal", "");
}

bool DatabaseWriter::InsertMessage(const std::string& messageId,
                                   const std::string& userId,
                                   const std::string& timestamp,
                                   const std::string& message,
                                   const std::string& conversationType,
                                   const std::string& targetId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_connected || m_connRaw == nullptr) return false;

    if (m_schemaExtended) {
        if (InsertPrepared(messageId, userId, timestamp, message,
                           conversationType, targetId, true)) {
            return true;
        }
        if (m_schemaExtended) return false;
    }
    return InsertPrepared(messageId, userId, timestamp, message,
                          conversationType, targetId, false);
}

bool DatabaseWriter::InsertPrepared(const std::string& messageId,
                                    const std::string& userId,
                                    const std::string& timestamp,
                                    const std::string& message,
                                    const std::string& conversationType,
                                    const std::string& targetId,
                                    bool extended) {
    SQLHDBC conn = reinterpret_cast<SQLHDBC>(m_connRaw);
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("DBWriter  : statement allocation failed");
        return false;
    }

    const std::string normalized = NormalizeTimestamp(timestamp);
    const std::string query = extended
        ? "INSERT INTO tbl_ChatDetails "
          "(MessageId, UserId, Timestamp, ChatMessage, ConversationType, TargetId) "
          "VALUES (?, ?, ?, ?, ?, ?) "
          "ON DUPLICATE KEY UPDATE MessageId = MessageId"
        : "INSERT INTO tbl_ChatDetails "
          "(MessageId, UserId, Timestamp, ChatMessage) "
          "VALUES (?, ?, ?, ?) "
          "ON DUPLICATE KEY UPDATE MessageId = MessageId";

    rc = SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(
        const_cast<char*>(query.c_str())), SQL_NTS);
    if (!SQL_SUCCEEDED(rc)) {
        Logger::Error("DBWriter  : prepare failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return false;
    }

    SQLLEN indicators[6] = {};
    bool bound = BindText(stmt, 1, messageId, indicators[0]) &&
                 BindText(stmt, 2, userId, indicators[1]) &&
                 BindText(stmt, 3, normalized, indicators[2]) &&
                 BindText(stmt, 4, message, indicators[3]);
    if (extended) {
        bound = bound &&
                BindText(stmt, 5, conversationType, indicators[4]) &&
                BindText(stmt, 6, targetId, indicators[5]);
    }

    if (!bound) {
        Logger::Error("DBWriter  : parameter binding failed: " +
                      OdbcError(SQL_HANDLE_STMT, stmt));
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return false;
    }

    rc = SQLExecute(stmt);
    if (!SQL_SUCCEEDED(rc)) {
        std::string error = OdbcError(SQL_HANDLE_STMT, stmt);
        Logger::Error("DBWriter  : insert failed: " + error);
        if (extended && IsMissingColumnError(error)) {
            m_schemaExtended = false;
            Logger::Warn("DBWriter  : falling back to legacy insert; run the "
                         "v2 migration to store group/personal routing data.");
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return false;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    Logger::Info("DBWriter  : stored message " + messageId);
    return true;
}

void DatabaseWriter::ReleaseAll() {
    if (m_connRaw != nullptr || m_envRaw != nullptr) {
        SQLHENV env = reinterpret_cast<SQLHENV>(m_envRaw);
        SQLHDBC conn = reinterpret_cast<SQLHDBC>(m_connRaw);
        ReleaseOdbc(env, conn, m_connected);
        m_envRaw = nullptr;
        m_connRaw = nullptr;
    }
    m_connected = false;
}

void DatabaseWriter::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ReleaseAll();
}
