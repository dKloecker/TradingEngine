//
// Created by Dominic Kloecker on 04/04/2026.
//
#include <filesystem>
#include <benchmark/benchmark.h>

#include "async_logger.h"

namespace dsl::benchmarks::logging {
namespace fs = std::filesystem;


static constexpr std::string_view LOG_DIR = "./benchmark_logs/";

static void cleanup_logs() {
    if (fs::exists(LOG_DIR)) {
        fs::remove_all(LOG_DIR);
    }
}

static void BM_EnqueueWithinBuffer(benchmark::State &state) {
    const size_t      msg_len = state.range(0);
    const std::string message(msg_len, 'X');

    Logger::instance().reset();
    Logger::instance().init({
        .min_level = LogLevel::e_DEBUG,
        .log_file  = std::string(LOG_DIR) + "BM_EnqueueWithinBuffer.log"
    });

    for (auto _: state) {
        Logger::instance().info(message);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(msg_len));

    Logger::instance().reset();
    cleanup_logs();
}

BENCHMARK(BM_EnqueueWithinBuffer)->Range(8, log_defaults::MAX_MESSAGE_LENGTH);

static void BM_EnqueueWithFlush(benchmark::State &state) {
    const size_t      msg_len = state.range(0);
    const std::string message(msg_len, 'X');

    Logger::instance().reset();
    Logger::instance().init({
        .min_level = LogLevel::e_DEBUG,
        .log_file  = std::string(LOG_DIR) + "BM_EnqueueWithFlush.log"
    });

    for (auto _: state) {
        Logger::instance().error(message);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(msg_len));

    Logger::instance().reset();
    cleanup_logs();
}

BENCHMARK(BM_EnqueueWithFlush)->Range(8, log_defaults::MAX_MESSAGE_LENGTH);

static void BM_FilteredOutMessages(benchmark::State &state) {
    Logger::instance().reset();
    Logger::instance().init({
        .min_level = LogLevel::e_ERROR,
        .log_file  = std::string(LOG_DIR) + "BM_FilteredOut.log"
    });

    for (auto _: state) {
        Logger::instance().debug("Some Message that should be filtered out");
    }
    state.SetItemsProcessed(state.iterations());
    Logger::instance().reset();
    cleanup_logs();
}

BENCHMARK(BM_FilteredOutMessages);

static void BM_BurstBlocking(benchmark::State &state) {
    Logger::instance().reset();
    Logger::instance().init({
        .min_level            = LogLevel::e_DEBUG,
        .log_file             = std::string(LOG_DIR) + "BM_BurstBlocking.log",
        .back_pressure_policy = BackPressurePolicy::e_BLOCK
    });

    constexpr size_t number_of_messages = 1000;
    for (auto _: state) {
        for (int i = 0; i < number_of_messages; ++i) {
            Logger::instance().info("Burst message under backpressure");
        }
    }

    state.SetItemsProcessed(state.iterations() * number_of_messages);

    Logger::instance().reset();
    cleanup_logs();
}

BENCHMARK(BM_BurstBlocking);

static void BM_BurstPolicyComparison(benchmark::State &state) {
    state.SetLabel(std::string(to_string(static_cast<BackPressurePolicy>(state.range(1)))));
    const auto num_messages = state.range(0);
    const auto policy       = static_cast<BackPressurePolicy>(state.range(1));

    for (auto _: state) {
        state.PauseTiming();
        Logger::instance().reset();
        Logger::instance().init({
            .min_level            = LogLevel::e_INFO,
            .log_file             = std::string(LOG_DIR) + "BM_BurstPolicyComparison.log",
            .back_pressure_policy = policy,
            .drop_threshold       = LogLevel::e_WARN
        });
        state.ResumeTiming();

        for (int64_t i = 0; i < num_messages; ++i) {
            Logger::instance().info("Some informational log that might be filtered");
            Logger::instance().warn("Some warning log that will not be filtered");
        }
    }

    state.SetItemsProcessed(state.iterations() * num_messages * 2);
    Logger::instance().reset();
    cleanup_logs();
}

BENCHMARK(BM_BurstPolicyComparison)
    ->Args({100, 0})
    ->Args({100, 1})
    ->Args({100, 2})
    ->Args({1'000, 0})
    ->Args({1'000, 1})
    ->Args({1'000, 2})
    ->Args({10'000, 0})
    ->Args({10'000, 1})
    ->Args({10'000, 2})
    ->Unit(benchmark::kMicrosecond);
}



