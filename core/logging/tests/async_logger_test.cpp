//
// Created by Dominic Kloecker on 03/04/2026.
//

#include <filesystem>
#include <gtest/gtest.h>

#include "async_logger.h"
#include "loggerr_enums.h"

namespace dsl::test::logging {
namespace fs = std::filesystem;

class AsyncLoggerTest : public ::testing::Test {
protected:
    fs::path log_path = "./test_async_logger.txt";

    void SetUp() override {
        // Ensure clean state before each test
        AsyncLogger<>::instance().reset();
    }

    void TearDown() override {
        AsyncLogger<>::instance().reset();
        if (fs::exists(log_path)) {
            fs::remove(log_path);
        }
    }
};

TEST_F(AsyncLoggerTest, CreatesLogFile) {
    LogConfig cfg{
        .min_level = LogLevel::e_DEBUG,
        .log_file  = log_path
    };

    AsyncLogger<>::instance().init(cfg);
    EXPECT_TRUE(fs::exists(log_path));
}

TEST_F(AsyncLoggerTest, CreatesParentDirectories) {
    log_path = "./logs/nested/test.txt";

    const LogConfig cfg{
        .min_level = LogLevel::e_DEBUG,
        .log_file  = log_path
    };
    AsyncLogger<>::instance().init(cfg);
    EXPECT_TRUE(fs::exists(log_path));
    fs::remove_all("./logs");
}

TEST_F(AsyncLoggerTest, LogsWrittenToFile) {
    const LogConfig cfg{
        .min_level = LogLevel::e_DEBUG,
        .log_file  = log_path,
        .format    = "%L %m"
    };

    AsyncLogger<>::instance().init(cfg);
    AsyncLogger<>::instance().debug("Some DEBUG Message");
    AsyncLogger<>::instance().info("Some INFO Message");
    AsyncLogger<>::instance().warn("Some WARNING Message");
    AsyncLogger<>::instance().error("Some ERROR Message");
    AsyncLogger<>::instance().fatal("Some FATAL Message");

    // Flush all records to disk
    AsyncLogger<>::instance().reset();

    std::ifstream            log_file(log_path);
    std::vector<std::string> lines;
    std::string              line;
    while (std::getline(log_file, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 5);
    EXPECT_EQ(lines[0], "DEBUG Some DEBUG Message");
    EXPECT_EQ(lines[1], "INFO Some INFO Message");
    EXPECT_EQ(lines[2], "WARN Some WARNING Message");
    EXPECT_EQ(lines[3], "ERROR Some ERROR Message");
    EXPECT_EQ(lines[4], "FATAL Some FATAL Message");
}

TEST_F(AsyncLoggerTest, LogsAreTruncatedIfTooLarge) {
    const LogConfig cfg{
        .min_level = LogLevel::e_DEBUG,
        .log_file  = log_path,
        .format    = "%m"
    };

    // Build a message longer than max message length
    std::string long_message(MAX_MESSAGE_LENGTH + 100, 'X');

    AsyncLogger<>::instance().init(cfg);
    AsyncLogger<>::instance().info(long_message);
    AsyncLogger<>::instance().reset();

    std::ifstream log_file(log_path);
    std::string   line;
    std::getline(log_file, line);

    EXPECT_EQ(line.length(), MAX_MESSAGE_LENGTH);
    EXPECT_EQ(line, std::string(MAX_MESSAGE_LENGTH, 'X'));
}

TEST_F(AsyncLoggerTest, LogsWrittenAfterBufferFlush) {
    const LogConfig cfg{
        .min_level = LogLevel::e_DEBUG,
        .log_file  = log_path,
        .format    = "%m"
    };

    AsyncLogger<>::instance().init(cfg);

    // Write enough records to exceed the stream buffer
    constexpr int num_records = 500;
    for (int i = 0; i < num_records; ++i) {
        AsyncLogger<>::instance().info("Filler message to exceed stream buffer capacity");
    }

    AsyncLogger<>::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_EQ(line_count, num_records);
}

TEST_F(AsyncLoggerTest, LogsBelowMinLevelDropped) {
    const LogConfig cfg{
        .min_level = LogLevel::e_WARN,
        .log_file  = log_path,
        .format    = "%L %m"
    };

    AsyncLogger<>::instance().init(cfg);
    AsyncLogger<>::instance().debug("should be dropped");
    AsyncLogger<>::instance().info("should also be dropped");
    AsyncLogger<>::instance().warn("should appear");
    AsyncLogger<>::instance().error("should also appear");
    AsyncLogger<>::instance().reset();

    std::ifstream            log_file(log_path);
    std::vector<std::string> lines;
    std::string              line;
    while (std::getline(log_file, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "WARN should appear");
    EXPECT_EQ(lines[1], "ERROR should also appear");
}

TEST_F(AsyncLoggerTest, DropQueuePolicyDrops) {
    using SmallLogger = AsyncLogger<4, 32>;
    const LogConfig cfg{
        .min_level         = LogLevel::e_DEBUG,
        .log_file          = log_path,
        .format            = "%m",
        .queue_full_policy = QueueFullPolicy::e_DROP
    };

    SmallLogger::instance().init(cfg);

    // Flood the queue with messages (some will be dropped)
    for (int i = 0; i < 100; ++i) {
        SmallLogger::instance().info("flood message");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    // Some messages should have been dropped
    EXPECT_LT(line_count, 100);
    EXPECT_GT(line_count, 0);
}

TEST_F(AsyncLoggerTest, DropBelowLevelDrops) {
    using SmallLogger = AsyncLogger<4, 32>;

    const LogConfig cfg{
        .min_level         = LogLevel::e_DEBUG,
        .log_file          = log_path,
        .format            = "%L %m",
        .queue_full_policy = QueueFullPolicy::e_DROP_BELOW_LEVEL,
        .drop_threshold    = LogLevel::e_WARN
    };

    SmallLogger::instance().init(cfg);

    for (int i = 0; i < 100; ++i) {
        SmallLogger::instance().debug("low priority flood");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_LT(line_count, 100);
}

TEST_F(AsyncLoggerTest, DropBelowLevelLogsAboveThreshold) {
    using SmallLogger = AsyncLogger<4, 32>;

    const LogConfig cfg{
        .min_level         = LogLevel::e_DEBUG,
        .log_file          = log_path,
        .format            = "%L %m",
        .queue_full_policy = QueueFullPolicy::e_DROP_BELOW_LEVEL,
        .drop_threshold    = LogLevel::e_WARN
    };

    SmallLogger::instance().init(cfg);

    // High Priority should not have been blocked
    for (int i = 0; i < 20; ++i) {
        SmallLogger::instance().error("critical message");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_EQ(line_count, 20);
}

TEST_F(AsyncLoggerTest, BlockPolicyBlocksUntilProcessed) {
    using SmallLogger = AsyncLogger<4, 32>;

    const LogConfig cfg{
        .min_level         = LogLevel::e_DEBUG,
        .log_file          = log_path,
        .format            = "%m",
        .queue_full_policy = QueueFullPolicy::e_BLOCK
    };

    SmallLogger::instance().init(cfg);

    // With block policy, all messages must eventually be written
    constexpr int num_records = 100;
    for (int i = 0; i < num_records; ++i) {
        SmallLogger::instance().info("blocking message");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_EQ(line_count, num_records);
}
}
