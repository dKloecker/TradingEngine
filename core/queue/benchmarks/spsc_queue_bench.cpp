#include <benchmark/benchmark.h>
#include <thread>

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

template<typename T>
static void BM_PushPop(benchmark::State &state) {
    constexpr size_t         batch = 10'000;
    dsl::spsc_queue<T, 1024> q;
    T                        out;

    for (auto _: state) {
        for (size_t i = 0; i < batch; i++) {
            q.push({});
            q.pop(out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK_TEMPLATE(BM_PushPop, SimpleObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPop, ComplexObject)->MinWarmUpTime(1.0);

template<typename T>
static void BM_PushPopNearFull(benchmark::State &state) {
    constexpr size_t         batch = 10'000;
    dsl::spsc_queue<T, 1024> q;
    while (q.push({})) {}
    T out;
    q.pop(out);

    for (auto _: state) {
        for (size_t i = 0; i < batch; i++) {
            q.push({});
            q.pop(out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK_TEMPLATE(BM_PushPopNearFull, SimpleObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPopNearFull, ComplexObject)->MinWarmUpTime(1.0);

template<typename T>
static void BM_QueueFill(benchmark::State &state) {
    constexpr size_t         cap = dsl::spsc_queue<T, 8192>::capacity;
    dsl::spsc_queue<T, 8192> q;
    T                        out;

    for (auto _: state) {
        for (size_t i = 0; i < cap; i++)
            q.push({});

        for (size_t i = 0; i < cap; i++)
            q.pop(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * cap);
}

BENCHMARK_TEMPLATE(BM_QueueFill, SimpleObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueFill, ComplexObject)->MinWarmUpTime(1.0);

template<typename T>
static void BM_QueueDrain(benchmark::State &state) {
    constexpr size_t         cap = dsl::spsc_queue<T, 8192>::capacity;
    dsl::spsc_queue<T, 8192> q;
    T                        out;

    for (auto _: state) {
        for (size_t i = 0; i < cap; i++)
            q.push({});

        for (size_t i = 0; i < cap; i++)
            q.pop(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * cap);
}

BENCHMARK_TEMPLATE(BM_QueueDrain, SimpleObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_QueueDrain, ComplexObject)->MinWarmUpTime(1.0);

template<typename T>
static void BM_FillAndDrain(benchmark::State &state) {
    constexpr size_t         cap = dsl::spsc_queue<T, 8192>::capacity;
    dsl::spsc_queue<T, 8192> q;
    T                        out;

    for (auto _: state) {
        for (size_t i = 0; i < cap; i++)
            q.push({});
        for (size_t i = 0; i < cap; i++)
            q.pop(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * cap * 2);
}

BENCHMARK_TEMPLATE(BM_FillAndDrain, SimpleObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_FillAndDrain, ComplexObject)->MinWarmUpTime(1.0);

template<typename T>
static void BM_ProducerConsumer(benchmark::State &state) {
    dsl::spsc_queue<T, 1024> q;
    std::atomic_bool         done{false};
    T                        out;

    std::thread consumer([&] {
        while (!done.load(std::memory_order_acquire)) {
            q.pop(out);
        }
        while (q.pop(out)) {}
    });

    for (auto _: state) {
        while (!q.push({})) {}
    }

    done.store(true, std::memory_order_release);
    consumer.join();
}

BENCHMARK_TEMPLATE(BM_ProducerConsumer, SimpleObject)->MinWarmUpTime(0.5);
BENCHMARK_TEMPLATE(BM_ProducerConsumer, ComplexObject)->MinWarmUpTime(0.5);
