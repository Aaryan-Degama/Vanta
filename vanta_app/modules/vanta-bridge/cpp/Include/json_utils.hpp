#pragma once

/**
 * @file json_utils.hpp
 * Minimal, dependency-free JSON helpers for the Vanta C++ engine.
 *
 * The engine currently builds small JSON responses by hand. These helpers
 * ensure strings are escaped correctly so file paths containing quotes or
 * backslashes do not break the response sent to JavaScript.
 */

#include <string>
#include <sstream>
#include <vector>

/**
 * Escapes a string for safe inclusion in a JSON value.
 *
 * Handles backslash, double quote, and control characters per the JSON spec.
 *
 * @param s Raw UTF-8 string.
 * @return Escaped string suitable for JSON.
 */
inline std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size());

    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }

    return out;
}

/**
 * Builds a JSON object field like `"key":"value"`.
 *
 * @param key Field name.
 * @param value String value (will be escaped).
 * @return JSON fragment with trailing comma.
 */
inline std::string json_string_field(const std::string& key, const std::string& value) {
    return "\"" + escape_json_string(key) + "\":\"" + escape_json_string(value) + "\"";
}

/**
 * Builds a JSON object field like `"key":123`.
 *
 * @param key Field name.
 * @param value Numeric value.
 * @return JSON fragment.
 */
template <typename T>
inline std::string json_number_field(const std::string& key, T value) {
    return "\"" + escape_json_string(key) + "\":" + std::to_string(value);
}
