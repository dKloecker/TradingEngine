//
// Created by Dominic Kloecker on 03/04/2026.
//

#ifndef TRADING_ASYNC_LOGGER_H
#define TRADING_ASYNC_LOGGER_H

#include <filesystem>
#include <fstream>
#include <source_location>
#include <thread>

#include "loggerr_enums.h"
#include "spsc_queue.h"

namespace dsl {
/// Maximum length of a log message in bytes. Messages exceeding this are truncated.
inline constexpr size_t MAX_MESSAGE_LENGTH = 256;

/**
 * @brief A single log entry to be enqueued and written by the consumer thread
 */
struct LogRecord {
    LogLevel             level                       = LogLevel::e_INFO;
    size_t               message_length              = 0;
    char                 message[MAX_MESSAGE_LENGTH] = {};
    std::source_location location{};
    // TODO: timestamp
};

/**
 * @brief Configuration for @c AsyncLogger
 */
struct LogConfig {
    // Minimum level that will be logged. Messages below this are discarded.
    LogLevel min_level = LogLevel::e_INFO;

    // TODO: Support log file rolling.
    std::filesystem::path log_file = "./logs/async_logger.log";

    /**
     * Format String using % placeholders.
     * @p %T - Timestamp
     * @p %L - Log Level
     * @p %f - File Name
     * @p %l - Line Number
     * @p %F - Function Name
     * @p %m - Log Message
     */
    std::string format = "%T [%L] %f:%l (%F) %m";


    QueueFullPolicy back_preassure_policy = QueueFullPolicy::e_DROP_BELOW_LEVEL;

    /**
     * Threshold used by @c e_DROP_BELOW_LEVEL policy.
     * Messages with severity below this level are dropped when the queue is full.
     * Messages at or above this level block until space is available.
     */
    LogLevel drop_threshold = LogLevel::e_WARN;
};

/**
 * @brief Singleton lock-free asynchronous logger.
 *
 * Log records are placed onto an SPSC queue by the producing thread and
 * written to a log file by a dedicated consumer thread.
 * The output stream is buffered, but @c FATAL and @c ERROR records trigger an immediate flush.
 *
 * @tparam QueueSize     Capacity of the underlying SPSC queue
 * @tparam MessageBuffer Number of messages worth of stream buffer space.
 */
template<size_t QueueSize = 512, size_t MessageBuffer = 32>
class AsyncLogger {
    static constexpr size_t BUFFER_SIZE = MessageBuffer * MAX_MESSAGE_LENGTH;

    // TODO: Add Status for logger (i.e. running, stopped etc) and prevent usage if not started
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

    AsyncLogger(const AsyncLogger &) = delete;

    AsyncLogger &operator=(const AsyncLogger &) = delete;

    AsyncLogger(AsyncLogger &&) = delete;

    AsyncLogger &operator=(AsyncLogger &&) = delete;

#ifdef TESTING
    /// @brief Shuts down the logger and resets the internal state. Test-only.
    void reset();
#endif

    /** Access to Logger Instance */
    static AsyncLogger &instance() {
        static AsyncLogger logger;
        return logger;
    }

    /**
     * Initialize logger based  provided configuration.
     * Must be called before any logs are written.
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
     * @brief Enqueue a log record.
     *
     * Captures the current timestamp and source location, constructs a
     * @c LogRecord, and enqueues it according to the configured backpressure policy.
     * Messages longer than @c MAX_MESSAGE_LENGTH are truncated.
     *
     * @param level   Severity of this log entry
     * @param message Log message content
     * @param loc     Source location (captured automatically at the call site).
     */
    void log(LogLevel                    level,
             std::string_view            message,
             const std::source_location &loc = std::source_location::current());

    void debug(const std::string_view      message,
               const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::e_DEBUG, message, loc);
    };

    void info(const std::string_view      message,
              const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::e_INFO, message, loc);
    };

    void warn(const std::string_view      message,
              const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::e_WARN, message, loc);
    };

    void error(const std::string_view      message,
               const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::e_ERROR, message, loc);
    }

    void fatal(const std::string_view      message,
               const std::source_location &loc = std::source_location::current()) {
        log(LogLevel::e_FATAL, message, loc);
    };
};

/// @brief Default logger type alias.
using Logger = AsyncLogger<512, 32>;

/// @name Convenience logging macros
/// Support @c std::format syntax. Source location is captured automatically.
/// @{
#define LOG_DEBUG(FMT, ...) Logger::instance().debug(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_INFO(FMT, ...) Logger::instance().info(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_WARN(FMT, ...) Logger::instance().warn(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_ERROR(FMT, ...) Logger::instance().error(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_FATAL(FMT, ...) Logger::instance().fatal(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
///@}
}

#endif //TRADING_ASYNC_LOGGER_H
