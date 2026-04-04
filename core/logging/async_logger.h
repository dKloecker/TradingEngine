//
// Created by Dominic Kloecker on 03/04/2026.
//

#ifndef TRADING_ASYNC_LOGGER_H
#define TRADING_ASYNC_LOGGER_H

#include <filesystem>
#include <fstream>
#include <thread>

#include "loggerr_enums.h"
#include "spsc_queue.h"

namespace dsl {
inline constexpr size_t MAX_MESSAGE_LENGTH = 256;

struct LogRecord {
    LogLevel level                       = LogLevel::e_INFO;
    size_t   message_length              = 0;
    char     message[MAX_MESSAGE_LENGTH] = {};
    // TODO: timestamp
};

/**
 * Configuration structure for the logging system.
 */
struct LogConfig {
    // Minimum level that will be logged. Messages below this are discarded.
    LogLevel min_level = LogLevel::e_INFO;

    // TODO: Log Rolling
    // Output destination
    std::filesystem::path log_file = "./logs/async_logger.log";

    // Format string using % placeholders
    //   %T  — timestamp
    //   %L  — log level
    //   %m  — message
    // Everything else is literal.
    std::string format = "%T [%L] %m";

    // Backpressure policy
    QueueFullPolicy queue_full_policy = QueueFullPolicy::e_DROP_BELOW_LEVEL;
    LogLevel        drop_threshold    = LogLevel::e_WARN;
};

/**
 * Asynchronous Logger
 */
template<size_t QueueSize = 512, size_t MessageBuffer = 32>
class AsyncLogger {
    static constexpr size_t BUFFER_SIZE = MessageBuffer * MAX_MESSAGE_LENGTH;

    // TODO: Add Status for logger (i.e. running, stopped etc)
    // TODO: Could I include file number / function name in log?
    LogConfig config_{};

    std::thread      consumer_thread_{};
    std::stop_source stop_{};

    char                             stream_buffer_[BUFFER_SIZE]{};
    spsc_queue<LogRecord, QueueSize> queue_{};
    std::ofstream                    log_file_{};

    AsyncLogger() = default;

    /**
     * Opens log file
     * Launches Consumer Thread
     * @throws std::runtime_error on file open failure
     */
    void start_up();

    /**
     * Signals shutdown
     * Joins Consumer Thread
     * Flushes remaining records
     */
    void shut_down();

public:
    ~AsyncLogger();

#ifdef TESTING
    void reset();
#endif

    // Non-copyable, non-movable
    AsyncLogger(const AsyncLogger &) = delete;

    AsyncLogger &operator=(const AsyncLogger &) = delete;

    AsyncLogger(AsyncLogger &&) = delete;

    AsyncLogger &operator=(AsyncLogger &&) = delete;

    /** Access to Logger Instance */
    static AsyncLogger &instance() {
        static AsyncLogger logger;
        return logger;
    }

    /**
     * Initialize logger based  provided configuration.
     * Must be configured before any logs are written.
     * @param config for Logging
     * @throws std::runtime_error on failure
     */
    void init(LogConfig config);

    /**
     * @return current minimum log level
     */
    LogLevel min_level() const {
        return config_.min_level;
    }

    /**
     * Update the minimum log level
     * @param level new minimum log level
     */
    void set_min_level(const LogLevel level) {
        config_.min_level = level;
    }

    /**
     * Constructs a LogRecord, with the current timestamp, applies the back-pressure policy,
     * and adds record to the queue.
     */
    void log(LogLevel level, std::string_view message);

    void debug(const std::string_view message) {
        log(LogLevel::e_DEBUG, message);
    };

    void info(const std::string_view message) {
        log(LogLevel::e_INFO, message);
    };

    void warn(const std::string_view message) {
        log(LogLevel::e_WARN, message);
    };

    void error(const std::string_view message) {
        log(LogLevel::e_ERROR, message);
    }

    void fatal(const std::string_view message) {
        log(LogLevel::e_FATAL, message);
    };
};

using Logger = AsyncLogger<512, 32>;


// TODO: Add convenience Macros
}

#endif //TRADING_ASYNC_LOGGER_H
