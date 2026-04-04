//
// Created by Dominic Kloecker on 04/04/2026.
//

#ifndef TRADING_LOGGER_ENUMS_H
#define TRADING_LOGGER_ENUMS_H
#include <cstdint>

enum class LogLevel : std::uint8_t {
    e_FATAL = 0,
    e_ERROR = 1,
    e_WARN  = 2,
    e_INFO  = 3,
    e_DEBUG = 4
};

constexpr std::string_view to_string(const LogLevel level) {
    switch (level) {
        case LogLevel::e_FATAL: return "FATAL";
        case LogLevel::e_ERROR: return "ERROR";
        case LogLevel::e_WARN: return "WARN";
        case LogLevel::e_INFO: return "INFO";
        case LogLevel::e_DEBUG: return "DEBUG";
    }
    return "";
}

/// Policy when the internal queue is full
enum class BackPressurePolicy : std::uint8_t {
    e_BLOCK,           // Block logging until queue has space
    e_DROP,            // Silently drop the message
    e_DROP_BELOW_LEVEL // Drop if message level < drop_threshold
};

constexpr std::string_view to_string(const BackPressurePolicy policy) {
    switch (policy) {
        case BackPressurePolicy::e_BLOCK: return "BLOCK";
        case BackPressurePolicy::e_DROP: return "DROP";
        case BackPressurePolicy::e_DROP_BELOW_LEVEL: return "DROP_BELOW";
    }
    return "";
}

#endif //TRADING_LOGGER_ENUMS_H
