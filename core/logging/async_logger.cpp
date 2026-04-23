//
// Created by Dominic Kloecker on 03/04/2026.
//
#include <fstream>

#include "core/logging/async_logger.h"
#include "core/logging/logger_utils.h"

namespace dsl {
template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::start_up() {
    status_          = LoggerStatus::e_STARTING;
    const auto &path = config_.log_file;
    // Create path if necessary
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    // Open Log file handler with the requested buffer size
    log_file_.rdbuf()->pubsetbuf(stream_buffer_, STREAM_BUFFER_SIZE);
    log_file_.open(path, std::ios::out | std::ios::app);
    if (!log_file_.is_open()) {
        throw std::runtime_error("AsyncLogger: failed to open log file: " + path.string());
    }

    // Pick Handling based off desired back pressure policy
    switch (config_.back_pressure_policy) {
        case BackPressurePolicy::e_BLOCK: {
            enqueue_handler_ = &enqueue_block;
        }
        break;
        case BackPressurePolicy::e_DROP: {
            enqueue_handler_ = &enqueue_drop;
        }
        break;
        case BackPressurePolicy::e_DROP_BELOW_LEVEL: {
            enqueue_handler_ = &enqueue_drop_below_level;
        }
        break;
    }

    // Launch Consumer Thread
    consumer_thread_ = std::thread([this] {
        while (!this->stop_.stop_requested() || !this->queue_.empty()) {
            LogRecord record;
            // We can go back to sleep if nothing is happening
            if (this->queue_.empty() || !this->queue_.pop(record)) {
                // TODO: move behind iteration so that we try a few times before we yield.
                std::this_thread::yield();
                continue;
            }

            // Write log to log file
            write_log(this->log_file_, this->config_.format, record);
            // If we have a error flush buffer
            if (record.level <= LogLevel::e_ERROR) {
                this->log_file_.flush();
            }
        }
        // Flush remaining messages before shutdown
        this->log_file_.flush();
    });
    status_ = LoggerStatus::e_RUNNING;
}

template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::shut_down() {
    status_ = LoggerStatus::e_STOPPING;
    stop_.request_stop();
    if (consumer_thread_.joinable()) consumer_thread_.join();
    if (log_file_.is_open()) log_file_.close();
    status_ = LoggerStatus::e_STOPPED;
}

template<size_t QS, size_t MB>
AsyncLogger<QS, MB>::~AsyncLogger() {
    shut_down();
}

template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::init(LogConfig config) {
    config_ = std::move(config);
    start_up();
}

template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::log(const LogLevel              level,
                                                     const std::string_view      message,
                                                     const std::source_location &loc) {
    if (level > config_.min_level) return;

    LogRecord record{};
    record.level = level;
    // Ensure that we truncate the log message where necessary
    record.message_length = std::min(message.length(), MAX_MESSAGE_LENGTH);
    std::memcpy(record.message, message.data(), record.message_length);
    record.location = loc;
    // Enqueue onto system based on chosen Policy
    enqueue_handler_(*this, record);
}

#ifdef TESTING
template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::reset() {
    shut_down();
    stop_   = std::stop_source{};
    status_ = LoggerStatus::e_UNKNOWN;
}

// Small Queue for Tests
template class AsyncLogger<4, 32>;
#endif

template class AsyncLogger<512, 32>;
}


