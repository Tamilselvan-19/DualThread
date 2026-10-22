#pragma once

#include <string>

class Logger {
public:
    static void Init(const std::string& file);
    static void Info(const std::string& msg);
    static void Warn(const std::string& msg);
    static void Error(const std::string& msg);
    static void Close();
};
