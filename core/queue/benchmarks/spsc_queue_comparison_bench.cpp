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
static void BM_PushPop(benchmark::State &state) {
    constexpr size_t             batch = 10'000;
    auto                         q     = Factory::create();
    typename Factory::value_type out;

    for (auto _: state) {
        for (size_t i = 0; i < batch; i++) {
            Factory::push(q, {});
            Factory::pop(q, out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK_TEMPLATE(BM_PushPop, DslQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPop, DslQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPop, BoostQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPop, BoostQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_PushPopNearFull(benchmark::State &state) {
    constexpr size_t             batch = 10'000;
    auto                         q     = Factory::create();
    while (Factory::push(q, {})) {}
    typename Factory::value_type out;
    Factory::pop(q, out);

    for (auto _: state) {
        for (size_t i = 0; i < batch; i++) {
            Factory::push(q, {});
            Factory::pop(q, out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK_TEMPLATE(BM_PushPopNearFull, DslQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPopNearFull, DslQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPopNearFull, BoostQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PushPopNearFull, BoostQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(1.0);

template<typename Factory>
static void BM_FillAndDrain(benchmark::State &state) {
    auto q = Factory::create();

    for (auto _: state) {
        while (Factory::push(q, {})) {}

        typename Factory::value_type out;
        while (Factory::pop(q, out)) {}
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
        while (Factory::pop(q, out)) {}
    });

    for (auto _: state) {
        while (!Factory::push(q, {})) {}
    }

    done.store(true, std::memory_order_release);
    consumer.join();
}

BENCHMARK_TEMPLATE(BM_ProducerConsumer, DslQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(0.5);
BENCHMARK_TEMPLATE(BM_ProducerConsumer, DslQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(0.5);
BENCHMARK_TEMPLATE(BM_ProducerConsumer, BoostQueueFactory<SimpleObject, 1024>)->MinWarmUpTime(0.5);
BENCHMARK_TEMPLATE(BM_ProducerConsumer, BoostQueueFactory<ComplexObject, 1024>)->MinWarmUpTime(0.5);
