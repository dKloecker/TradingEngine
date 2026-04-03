//
// Created by Dominic Kloecker on 02/04/2026.
//
#include <random>
#include <benchmark/benchmark.h>
#include <thread>

#include <boost/lockfree/spsc_queue.hpp>

#include "spsc_queue.h"

struct SimpleObject {
    int someValue = 42;
};

struct ComplexObject {
    int                        someValue1 = 1;
    int                        someValue2 = 2;
    int                        someValue3 = 3;
    std::array<std::byte, 256> data1{};
    std::array<std::byte, 256> data2{};
};

template<typename T, size_t Capacity>
struct BoostQueueFactory {
    using value_type                 = T;
    using queue_type                 = boost::lockfree::spsc_queue<T>;
    static constexpr size_t capacity = Capacity;

    static queue_type create() { return queue_type{Capacity}; }
    static bool       pop(queue_type &q, T &out) { return q.pop(out); }
    static bool       push(queue_type &q, const T &val) { return q.push(val); }
};

template<typename T, size_t Capacity>
struct DslQueueFactory {
    using value_type                 = T;
    using queue_type                 = dsl::spsc_queue<T, Capacity>;
    static constexpr size_t capacity = Capacity;

    static queue_type create() { return queue_type{}; }
    static bool       pop(queue_type &q, T &out) { return q.pop(out); }
    static bool       push(queue_type &q, const T &val) { return q.push(val); }
};


template<typename Factory>
static void BM_SinglePush(benchmark::State &state) {
    auto q = Factory::create();
    for (auto _: state) {
        Factory::push(q, {});

        state.PauseTiming();
        typename Factory::value_type out;
        Factory::pop(q, out);
        state.ResumeTiming();
    }
}

BENCHMARK_TEMPLATE(BM_SinglePush, DslQueueFactory<SimpleObject, 8>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SinglePush, DslQueueFactory<ComplexObject, 8>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SinglePush, BoostQueueFactory<SimpleObject, 8>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SinglePush, BoostQueueFactory<ComplexObject, 8>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_SinglePop(benchmark::State &state) {
    auto q = Factory::create();
    for (auto _: state) {
        state.PauseTiming();
        Factory::push(q, {});
        state.ResumeTiming();

        typename Factory::value_type out;
        Factory::pop(q, out);
        benchmark::DoNotOptimize(out);
    }
}

BENCHMARK_TEMPLATE(BM_SinglePop, DslQueueFactory<SimpleObject, 8>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SinglePop, DslQueueFactory<ComplexObject, 8>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SinglePop, BoostQueueFactory<SimpleObject, 8>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SinglePop, BoostQueueFactory<ComplexObject, 8>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_PushOntoFullQueue(benchmark::State &state) {
    auto q = Factory::create();
    while (Factory::push(q, {})) {
    }
    typename Factory::value_type out;
    Factory::pop(q, out);

    for (auto _: state) {
        Factory::push(q, {});

        state.PauseTiming();
        Factory::pop(q, out);
        state.ResumeTiming();
    }
}

BENCHMARK_TEMPLATE(BM_PushOntoFullQueue, DslQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushOntoFullQueue, DslQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushOntoFullQueue, BoostQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushOntoFullQueue, BoostQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_PopFromFullQueue(benchmark::State &state) {
    auto q = Factory::create();
    while (Factory::push(q, {})) {
    }

    for (auto _: state) {
        typename Factory::value_type out;
        Factory::pop(q, out);
        benchmark::DoNotOptimize(out);

        state.PauseTiming();
        Factory::push(q, {});
        state.ResumeTiming();
    }
}

BENCHMARK_TEMPLATE(BM_PopFromFullQueue, DslQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PopFromFullQueue, DslQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PopFromFullQueue, BoostQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PopFromFullQueue, BoostQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_SustainedPushPop(benchmark::State &state) {
    auto q = Factory::create();
    while (Factory::push(q, {})) {
    }
    typename Factory::value_type out;
    Factory::pop(q, out);

    constexpr size_t ops_per_iter = 1'000'000;
    for (auto _: state) {
        for (size_t i = 0; i < ops_per_iter; i++) {
            Factory::push(q, {});
            Factory::pop(q, out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * ops_per_iter);
}

BENCHMARK_TEMPLATE(BM_SustainedPushPop, DslQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SustainedPushPop, DslQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SustainedPushPop, BoostQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SustainedPushPop, BoostQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_QueueFill(benchmark::State &state) {
    auto q = Factory::create();
    for (auto _: state) {
        while (Factory::push(q, {})) {
        }

        state.PauseTiming();
        typename Factory::value_type out;
        while (Factory::pop(q, out)) {
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * Factory::capacity);
}

BENCHMARK_TEMPLATE(BM_QueueFill, DslQueueFactory<SimpleObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueFill, DslQueueFactory<ComplexObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueFill, BoostQueueFactory<SimpleObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueFill, BoostQueueFactory<ComplexObject, 8192>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_QueueDrain(benchmark::State &state) {
    auto q = Factory::create();
    for (auto _: state) {
        state.PauseTiming();
        while (Factory::push(q, {})) {
        }
        state.ResumeTiming();

        typename Factory::value_type out;
        while (Factory::pop(q, out)) {
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * Factory::capacity);
}

BENCHMARK_TEMPLATE(BM_QueueDrain, DslQueueFactory<SimpleObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueDrain, DslQueueFactory<ComplexObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueDrain, BoostQueueFactory<SimpleObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueDrain, BoostQueueFactory<ComplexObject, 8192>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_FillAndDrain(benchmark::State &state) {
    auto q = Factory::create();
    for (auto _: state) {
        while (Factory::push(q, {})) {
        }

        typename Factory::value_type out;
        while (Factory::pop(q, out)) {
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * Factory::capacity * 2);
}

BENCHMARK_TEMPLATE(BM_FillAndDrain, DslQueueFactory<SimpleObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_FillAndDrain, DslQueueFactory<ComplexObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_FillAndDrain, BoostQueueFactory<SimpleObject, 8192>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_FillAndDrain, BoostQueueFactory<ComplexObject, 8192>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_ProducerConsumer(benchmark::State &state) {
    auto                         q = Factory::create();
    std::atomic_bool             done{false};
    typename Factory::value_type out;

    std::thread consumer([&] {
        while (!done.load(std::memory_order_acquire)) {
            Factory::pop(q, out);
        }
        // Drain remaining
        while (Factory::pop(q, out)) {
        }
    });

    for (auto _: state) {
        // Spin until push succeeds (queue might be full if consumer is slow)
        while (!Factory::push(q, {})) {
        }
    }

    done.store(true, std::memory_order_release);
    consumer.join();
}

BENCHMARK_TEMPLATE(BM_ProducerConsumer, DslQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(0.5);
BENCHMARK_TEMPLATE(BM_ProducerConsumer, DslQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(0.5);
BENCHMARK_TEMPLATE(BM_ProducerConsumer, BoostQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(0.5);
BENCHMARK_TEMPLATE(BM_ProducerConsumer, BoostQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(0.5);

