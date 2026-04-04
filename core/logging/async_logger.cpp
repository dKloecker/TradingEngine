//
// Created by Dominic Kloecker on 03/04/2026.
//
#include "async_logger.h"
#include <fstream>

namespace dsl {
namespace {
// TODO: Come up with better implementation for this
void write_log(std::ofstream &file, const std::string &format, const LogRecord &record) {
    const char *p = format.c_str();

    while (*p) {
        if (*p == '%' && *(p + 1)) {
            switch (*(p + 1)) {
                case 'T': {
                    // TODO: Add Time
                    file << "TIMESTAMP";
                    break;
                }
                case 'L': {
                    file << to_string(record.level);
                    break;
                }
                case 'm': {
                    file.write(record.message, record.message_length);
                    break;
                }
                case '%': {
                    file << '%';
                    break;
                }
                default: {
                    file << '%' << *(p + 1);
                    break;
                }
            }
            p += 2;
        } else {
            file << *p;
            ++p;
        }
    }
    file << '\n';
}
}

template<size_t QS, size_t MB>
void AsyncLogger<QS, MB>::init(LogConfig config) {
    config_ = std::move(config);
    start_up();
}

template<size_t QS, size_t MB>
AsyncLogger<QS, MB>::~AsyncLogger() {
    shut_down();
}

template<size_t QS, size_t MB>
void AsyncLogger<QS, MB>::start_up() {
    const auto &path = config_.log_file;
    // Create path if necessary
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    // Open Log file handler with the requested buffer size
    log_file_.rdbuf()->pubsetbuf(stream_buffer_, BUFFER_SIZE);
    log_file_.open(path, std::ios::out | std::ios::app);
    if (!log_file_.is_open()) {
        throw std::runtime_error("AsyncLogger: failed to open log file: " + path.string());
    }

    // Launch Consumer Thread
    consumer_thread_ = std::thread([this] {
        LogRecord record;
        while (!this->stop_.stop_requested() || !this->queue_.empty()) {
            // We can go back to sleep if nothing is happening
            if (this->queue_.empty() || !this->queue_.pop(record)) {
                // TODO: move behind iteration so that we try a few times before we yield.
                std::this_thread::yield();
                continue;
            }

            // Write log to log file
            write_log(this->log_file_, this->config_.format, record);
            // If we have a error flush buffe[
            if (record.level <= LogLevel::e_ERROR) {
                this->log_file_.flush();
            }
        }
        // Flush remaining messages before shutdown
        this->log_file_.flush();
    });
}

template<size_t QS, size_t MB>
void AsyncLogger<QS, MB>::shut_down() {
    stop_.request_stop();
    if (consumer_thread_.joinable()) consumer_thread_.join();
    if (log_file_.is_open()) log_file_.close();
}

template<size_t QS, size_t MB>
void AsyncLogger<QS, MB>::log(const LogLevel level, std::string_view message) {
    if (level > config_.min_level) return; // Early return if irrelevant log level

    LogRecord record{};
    record.level          = level;
    record.message_length = std::min(message.length(), MAX_MESSAGE_LENGTH);
    std::memcpy(record.message, message.data(), record.message_length);

    // TODO: Use function pointer here after init for logging policy to avoid runtime check
    if (config_.queue_full_policy == QueueFullPolicy::e_BLOCK) {
        while (!queue_.push(record)) {}
    } else if (config_.queue_full_policy == QueueFullPolicy::e_DROP) {
        queue_.push(record);
    } else if (config_.queue_full_policy == QueueFullPolicy::e_DROP_BELOW_LEVEL) {
        // Attempt one publish and return
        if (record.level > config_.drop_threshold) {
            queue_.push(record);
            return;
        }
        while (!queue_.push(record)) {}
    }
}

#ifdef TESTING
template<size_t QueueSize, size_t MessageBuffer>
void AsyncLogger<QueueSize, MessageBuffer>::reset() {
    shut_down();
    stop_ = std::stop_source{};
}

// Small Queue for Tests
template class AsyncLogger<4, 32>;
#endif

template class AsyncLogger<512, 32>;
}


