#pragma once

#include <string>
#include <string_view>

namespace speecher {

inline void appendJsonString(std::string &json, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    json.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': json += "\\\""; break;
        case '\\': json += "\\\\"; break;
        case '\b': json += "\\b"; break;
        case '\f': json += "\\f"; break;
        case '\n': json += "\\n"; break;
        case '\r': json += "\\r"; break;
        case '\t': json += "\\t"; break;
        default:
            if (character < 0x20) {
                json += "\\u00";
                json.push_back(hex[character >> 4]);
                json.push_back(hex[character & 0x0f]);
            } else {
                json.push_back(static_cast<char>(character));
            }
        }
    }
    json.push_back('"');
}

inline std::string ydotoolSetupStateText(bool packageInstalled,
                                         std::string_view serviceFile,
                                         std::string_view targetUser)
{
    std::string state = "{\"packageInstalledBySpeecher\":";
    state += packageInstalled ? "true" : "false";
    state += ",\"serviceFile\":";
    appendJsonString(state, serviceFile);
    state += ",\"targetUser\":";
    appendJsonString(state, targetUser);
    state += '}';
    return state;
}

} // namespace speecher
