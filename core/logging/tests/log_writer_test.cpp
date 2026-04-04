#include <chrono>
#include <cstring>
#include <sstream>
#include <gtest/gtest.h>

#include "async_logger.h"
#include "loggerr_enums.h"
#include "logger_utils.h"

namespace dsl::test::logging {
class WriteLogTest : public ::testing::Test {
protected:
    std::ostringstream out;

    static LogRecord make_record(LogLevel level, const char *msg) {
        LogRecord record{};
        record.level          = level;
        record.message_length = std::min(std::strlen(msg), log_defaults::MAX_MESSAGE_LENGTH);
        std::memcpy(record.message, msg, record.message_length);
        return record;
    }
};

TEST_F(WriteLogTest, MessageOnly) {
    auto record = make_record(LogLevel::e_INFO, "hello world");
    write_log(out, "%m", record);
    EXPECT_EQ(out.str(), "hello world\n");
}

TEST_F(WriteLogTest, LevelOnly) {
    auto record = make_record(LogLevel::e_ERROR, "msg");
    write_log(out, "%L", record);
    EXPECT_EQ(out.str(), "ERROR\n");
}

TEST_F(WriteLogTest, AllLevels) {
    const std::pair<LogLevel, std::string> cases[] = {
        {LogLevel::e_DEBUG, "DEBUG"},
        {LogLevel::e_INFO, "INFO"},
        {LogLevel::e_WARN, "WARN"},
        {LogLevel::e_ERROR, "ERROR"},
        {LogLevel::e_FATAL, "FATAL"},
    };

    for (const auto &[level, expected]: cases) {
        out.str("");
        out.clear();
        const auto record = make_record(level, "msg");
        write_log(out, "%L", record);
        EXPECT_EQ(out.str(), expected + "\n");
    }
}

TEST_F(WriteLogTest, LiteralText) {
    const auto record = make_record(LogLevel::e_INFO, "msg");
    write_log(out, "hello world", record);
    EXPECT_EQ(out.str(), "hello world\n");
}

TEST_F(WriteLogTest, LiteralPercentEscape) {
    const auto record = make_record(LogLevel::e_INFO, "msg");
    write_log(out, "100%% complete", record);
    EXPECT_EQ(out.str(), "100% complete\n");
}

TEST_F(WriteLogTest, CombinedFormat) {
    const auto record = make_record(LogLevel::e_WARN, "some warning");
    write_log(out, "[%L] %m", record);
    EXPECT_EQ(out.str(), "[WARN] some warning\n");
}

TEST_F(WriteLogTest, EmptyMessage) {
    const auto record = make_record(LogLevel::e_INFO, "");
    write_log(out, "%L %m", record);
    EXPECT_EQ(out.str(), "INFO \n");
}

TEST_F(WriteLogTest, EmptyFormat) {
    const auto record = make_record(LogLevel::e_INFO, "hello");
    write_log(out, "", record);
    EXPECT_EQ(out.str(), "\n");
}

TEST_F(WriteLogTest, TrailingPercentIgnored) {
    auto record = make_record(LogLevel::e_INFO, "msg");
    write_log(out, "hello%", record);
    EXPECT_EQ(out.str(), "hello%\n");
}

TEST_F(WriteLogTest, TimestampFormat) {
    auto record = make_record(LogLevel::e_INFO, "msg");

    // Set timestamp
    using namespace std::chrono;
    constexpr auto days_since_epoch = sys_days{2026y / January / 15d};
    record.time_stamp               = days_since_epoch + 12h + 30min + 45s + 123us;

    write_log(out, "%T", record);
    EXPECT_EQ(out.str(), "2026-01-15 12:30:45.000123\n");
}

TEST_F(WriteLogTest, FullFormatIntegration) {
    auto record = make_record(LogLevel::e_ERROR, "connection lost");

    using namespace std::chrono;
    constexpr auto days_since_epoch = sys_days{2026y / March / 20d};
    record.time_stamp               = days_since_epoch + 8h + 15min + 30s + 42us;

    write_log(out, "%T [%L] %m", record);
    EXPECT_EQ(out.str(), "2026-03-20 08:15:30.000042 [ERROR] connection lost\n");
}

TEST_F(WriteLogTest, TruncatedMessage) {
    const std::string long_msg(log_defaults::MAX_MESSAGE_LENGTH + 50, 'A');
    const auto        record = make_record(LogLevel::e_INFO, long_msg.c_str());

    write_log(out, "%m", record);

    // -1 for new line character
    EXPECT_EQ(out.str().length() - 1, log_defaults::MAX_MESSAGE_LENGTH);
}
} // namespace dsl::test::logging
