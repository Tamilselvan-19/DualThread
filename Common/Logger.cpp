#include "Logger.h"

#include <fstream>
#include <iostream>
#include <mutex>

namespace {
    std::mutex g_logMutex;
    std::ofstream g_logFile;
}

void Logger::Init(const std::string& file) {
    g_logFile.open(file, std::ios::out | std::ios::app);
}

void Logger::Info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << "[INFO] " << msg << std::endl;
    if (g_logFile.is_open()) {
        g_logFile << "[INFO] " << msg << std::endl;
        g_logFile.flush();
    }
}

void Logger::Warn(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << "[WARN] " << msg << std::endl;
    if (g_logFile.is_open()) {
        g_logFile << "[WARN] " << msg << std::endl;
        g_logFile.flush();
    }
}

void Logger::Error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << "[ERROR] " << msg << std::endl;
    if (g_logFile.is_open()) {
        g_logFile << "[ERROR] " << msg << std::endl;
        g_logFile.flush();
    }
}

void Logger::Close() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile.close();
    }
}
