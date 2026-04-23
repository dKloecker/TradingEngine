//
// Created by Dominic Kloecker on 03/04/2026.
//

#ifndef TRADING_ASYNC_LOGGER_H
#define TRADING_ASYNC_LOGGER_H

#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <source_location>
#include <thread>

#include "core/logging/logger_enums.h"
#include "core/queue/spsc_queue.h"

namespace dsl {
namespace log_defaults {
inline constexpr size_t MAX_MESSAGE_LENGTH = 256;
inline constexpr size_t QUEUE_CAPACITY     = 512;
inline constexpr size_t FLUSH_THRESHOLD    = 32;
}


/**
 * @brief A single log entry to be enqueued and written by the consumer thread
 */
struct LogRecord {
    LogLevel             level                                     = LogLevel::e_INFO;
    size_t               message_length                            = 0;
    char                 message[log_defaults::MAX_MESSAGE_LENGTH] = {};
    std::source_location location{};
    // TODO: use a different time system here
    std::chrono::time_point<std::chrono::system_clock> time_stamp = std::chrono::system_clock::now();
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

    BackPressurePolicy back_pressure_policy = BackPressurePolicy::e_DROP_BELOW_LEVEL;

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
 * @tparam QueueCapacity     Capacity of the underlying SPSC queue
 * @tparam FlushThreshold    Number of messages worth of stream buffer space.
 */
template<size_t QueueCapacity = log_defaults::QUEUE_CAPACITY,
    size_t FlushThreshold = log_defaults::FLUSH_THRESHOLD>
class AsyncLogger {
    static constexpr size_t STREAM_BUFFER_SIZE = FlushThreshold * log_defaults::MAX_MESSAGE_LENGTH;
    LoggerStatus            status_            = LoggerStatus::e_UNKNOWN;
    LogConfig               config_{};

    std::thread      consumer_thread_{};
    std::stop_source stop_{};

    char                                 stream_buffer_[STREAM_BUFFER_SIZE]{};
    spsc_queue<LogRecord, QueueCapacity> queue_{};
    std::ofstream                        log_file_{};

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

    /**
     * Handler for enqueuing Log Record based on Loggers configured BackPressure policy.
     * Defaults to void function when uninitialized
     */
    using enqueue_fn            = void (*)(AsyncLogger &, const LogRecord &);
    enqueue_fn enqueue_handler_ = nullptr;

    static void enqueue_block(AsyncLogger &self, const LogRecord &record) {
        while (!self.queue_.push(record)) {}
    }

    static void enqueue_drop(AsyncLogger &self, const LogRecord &record) {
        self.queue_.push(record);
    }

    static void enqueue_drop_below_level(AsyncLogger &self, const LogRecord &record) {
        if (record.level > self.config_.drop_threshold) {
            self.queue_.push(record);
            return;
        }
        while (!self.queue_.push(record)) {}
    }

public:
    static constexpr size_t QUEUE_CAPACITY     = QueueCapacity;
    static constexpr size_t FLUSH_THRESHOLD    = FlushThreshold;
    static constexpr size_t MAX_MESSAGE_LENGTH = log_defaults::MAX_MESSAGE_LENGTH;

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
