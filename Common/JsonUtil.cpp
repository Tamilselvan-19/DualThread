#include "JsonUtil.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace {
    void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    int HexValue(char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    void SkipWs(const std::string& s, size_t& i) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }

    bool ParseString(const std::string& s, size_t& i, std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        out.clear();
        while (i < s.size()) {
            char ch = s[i++];
            if (ch == '"') return true;
            if (ch != '\\') {
                out += ch;
                continue;
            }
            if (i >= s.size()) return false;
            char esc = s[i++];
            switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                uint32_t cp = 0;
                for (int k = 0; k < 4; ++k) {
                    int hv = HexValue(s[i + k]);
                    if (hv < 0) return false;
                    cp = (cp << 4) | static_cast<uint32_t>(hv);
                }
                i += 4;
                // Combine UTF-16 surrogate pairs when present.
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() &&
                    s[i] == '\\' && s[i + 1] == 'u') {
                    uint32_t low = 0;
                    bool ok = true;
                    for (int k = 0; k < 4; ++k) {
                        int hv = HexValue(s[i + 2 + k]);
                        if (hv < 0) { ok = false; break; }
                        low = (low << 4) | static_cast<uint32_t>(hv);
                    }
                    if (ok && low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                }
                AppendUtf8(out, cp);
                break;
            }
            default:
                out += esc;
                break;
            }
        }
        return false;
    }
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    static const char* hex = "0123456789abcdef";
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                out += "\\u00";
                out += hex[(ch >> 4) & 0x0F];
                out += hex[ch & 0x0F];
            } else {
                out += static_cast<char>(ch);
            }
            break;
        }
    }
    return out;
}

std::string JsonBuild(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string result = "{";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) result += ",";
        result += "\"" + JsonEscape(fields[i].first) + "\":\"" +
                  JsonEscape(fields[i].second) + "\"";
    }
    result += "}";
    return result;
}

bool JsonParse(const std::string& json, std::map<std::string, std::string>& out) {
    out.clear();
    size_t i = 0;
    SkipWs(json, i);
    if (i >= json.size() || json[i] != '{') return false;
    ++i;
    SkipWs(json, i);
    if (i < json.size() && json[i] == '}') {
        ++i;
        SkipWs(json, i);
        return i == json.size();
    }

    while (i < json.size()) {
        SkipWs(json, i);
        std::string key;
        if (!ParseString(json, i, key)) return false;
        SkipWs(json, i);
        if (i >= json.size() || json[i] != ':') return false;
        ++i;
        SkipWs(json, i);
        if (i >= json.size()) return false;

        std::string value;
        if (json[i] == '"') {
            if (!ParseString(json, i, value)) return false;
        } else {
            size_t start = i;
            while (i < json.size() && json[i] != ',' && json[i] != '}') {
                ++i;
            }
            while (i > start && std::isspace(static_cast<unsigned char>(json[i - 1]))) --i;
            value = json.substr(start, i - start);
            if (value == "null") value.clear();
        }
        out[key] = value;

        SkipWs(json, i);
        if (i >= json.size()) return false;
        if (json[i] == ',') {
            ++i;
            continue;
        }
        if (json[i] == '}') {
            ++i;
            SkipWs(json, i);
            return i == json.size();
        }
        return false;
    }
    return false;
}

std::string JsonGet(const std::map<std::string, std::string>& obj, const std::string& key,
                   const std::string& defaultValue) {
    auto it = obj.find(key);
    return it == obj.end() ? defaultValue : it->second;
}

int JsonGetInt(const std::map<std::string, std::string>& obj, const std::string& key,
               int defaultValue) {
    auto it = obj.find(key);
    if (it == obj.end() || it->second.empty()) return defaultValue;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

bool JsonGetBool(const std::map<std::string, std::string>& obj, const std::string& key,
                 bool defaultValue) {
    auto it = obj.find(key);
    if (it == obj.end()) return defaultValue;
    return it->second == "true" || it->second == "1" || it->second == "yes";
}
