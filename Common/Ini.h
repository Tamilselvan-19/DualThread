#pragma once

#include <string>

class IniFile {
public:
    static std::string ReadValue(const std::string& path, const std::string& section,
                                const std::string& key, const std::string& defaultValue = "");
    static int ReadInt(const std::string& path, const std::string& section,
                       const std::string& key, int defaultValue = 0);
    static bool ReadBool(const std::string& path, const std::string& section,
                         const std::string& key, bool defaultValue = false);
};
