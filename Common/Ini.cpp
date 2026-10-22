#include "Ini.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {
    std::string Trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        std::string out = s.substr(start, end - start + 1);
        if (out.size() >= 2 && ((out.front() == '"' && out.back() == '"') ||
                                (out.front() == '\'' && out.back() == '\''))) {
            out = out.substr(1, out.size() - 2);
        }
        return out;
    }

    bool EqualsNoCase(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            char ca = a[i];
            char cb = b[i];
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
            if (ca != cb) return false;
        }
        return true;
    }
}

std::string IniFile::ReadValue(const std::string& path, const std::string& section,
                               const std::string& key, const std::string& defaultValue) {
    std::ifstream in(path);
    if (!in) return defaultValue;

    std::string line;
    std::string currentSection;
    while (std::getline(in, line)) {
        auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = Trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }
        size_t pos = trimmed.find('=');
        if (pos == std::string::npos) continue;
        std::string k = Trim(trimmed.substr(0, pos));
        std::string v = Trim(trimmed.substr(pos + 1));
        if (EqualsNoCase(k, key) && EqualsNoCase(currentSection, section)) return v;
    }
    return defaultValue;
}

int IniFile::ReadInt(const std::string& path, const std::string& section,
                     const std::string& key, int defaultValue) {
    std::string val = ReadValue(path, section, key, std::to_string(defaultValue));
    try {
        size_t used = 0;
        int value = std::stoi(val, &used);
        return used == 0 ? defaultValue : value;
    } catch (...) {
        return defaultValue;
    }
}

bool IniFile::ReadBool(const std::string& path, const std::string& section,
                       const std::string& key, bool defaultValue) {
    std::string val = ReadValue(path, section, key, defaultValue ? "true" : "false");
    return EqualsNoCase(val, "true") || val == "1" || EqualsNoCase(val, "yes");
}
