#include <benchmark/benchmark.h>
#include <random>

#include "core/memory/fixed_size_pool_resource.h"

struct alignas(16) SmallObject {
    std::array<std::byte, 16> data;
};

struct alignas(64) LargeObject {
    std::array<int, 1000> data;
    std::array<char, 460> text;
};

template<typename T>
static void BM_TemplatePoolAllocate(benchmark::State &state) {
    dsl::static_fixed_size_pool_resource<sizeof(T), 64, alignof(T)> pool;
    void *warmup = pool.allocate(sizeof(T), alignof(T));
    pool.deallocate(warmup, sizeof(T), alignof(T));
    for (auto _: state) {
        void *p = pool.allocate(sizeof(T), alignof(T));
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, sizeof(T), alignof(T));
    }
}

template<typename T>
static void BM_RuntimePoolAllocate(benchmark::State &state) {
    dsl::fixed_size_pool_resource pool{{sizeof(T), 64, alignof(T)}};
    void *warmup = pool.allocate(sizeof(T), alignof(T));
    pool.deallocate(warmup, sizeof(T), alignof(T));
    for (auto _: state) {
        void *p = pool.allocate(sizeof(T), alignof(T));
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, sizeof(T), alignof(T));
    }
}

BENCHMARK_TEMPLATE(BM_TemplatePoolAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_TemplatePoolAllocate, LargeObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_RuntimePoolAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_RuntimePoolAllocate, LargeObject)->MinWarmUpTime(1.0);

static void BM_TemplatePoolBlockGrowth(benchmark::State &state) {
    for (auto _: state) {
        state.PauseTiming();
        dsl::static_fixed_size_pool_resource<sizeof(SmallObject), 2, alignof(SmallObject)> pool;
        void *p1 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        void *p2 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p1);
        benchmark::DoNotOptimize(p2);
        state.ResumeTiming();

        void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p);
    }
}

static void BM_RuntimePoolBlockGrowth(benchmark::State &state) {
    for (auto _: state) {
        state.PauseTiming();
        dsl::fixed_size_pool_resource pool{{sizeof(SmallObject), 2, alignof(SmallObject)}};
        void *p1 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        void *p2 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p1);
        benchmark::DoNotOptimize(p2);
        state.ResumeTiming();

        void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p);
    }
}

BENCHMARK(BM_TemplatePoolBlockGrowth)->MinWarmUpTime(1.0);
BENCHMARK(BM_RuntimePoolBlockGrowth)->MinWarmUpTime(1.0);

template<typename T>
static void BM_TemplatePoolSustainedLiveObjects(benchmark::State &state) {
    const size_t                                              live_count = state.range(0);
    dsl::static_fixed_size_pool_resource<sizeof(T), 64, alignof(T)> pool;

    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(T), alignof(T));
    }

    void *warmup = pool.allocate(sizeof(T), alignof(T));
    pool.deallocate(warmup, sizeof(T), alignof(T));

    for (auto _: state) {
        void *p = pool.allocate(sizeof(T), alignof(T));
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, sizeof(T), alignof(T));
    }
}

template<typename T>
static void BM_RuntimePoolSustainedLiveObjects(benchmark::State &state) {
    const size_t                   live_count = state.range(0);
    dsl::fixed_size_pool_resource pool{{sizeof(T), 64, alignof(T)}};

    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(T), alignof(T));
    }

    void *warmup = pool.allocate(sizeof(T), alignof(T));
    pool.deallocate(warmup, sizeof(T), alignof(T));

    for (auto _: state) {
        void *p = pool.allocate(sizeof(T), alignof(T));
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, sizeof(T), alignof(T));
    }
}

BENCHMARK_TEMPLATE(BM_TemplatePoolSustainedLiveObjects, SmallObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_TemplatePoolSustainedLiveObjects, LargeObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_RuntimePoolSustainedLiveObjects, SmallObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_RuntimePoolSustainedLiveObjects, LargeObject)->Range(8, 1024)->MinWarmUpTime(1.0);

static void BM_TemplatePoolAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        dsl::static_fixed_size_pool_resource<sizeof(SmallObject), 64, alignof(SmallObject)> pool;
        for (int i = 0; i < state.range(0); i++) {
            void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
            benchmark::DoNotOptimize(p);
        }
    }
}

static void BM_RuntimePoolAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        dsl::fixed_size_pool_resource pool{{sizeof(SmallObject), 64, alignof(SmallObject)}};
        for (int i = 0; i < state.range(0); i++) {
            void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
            benchmark::DoNotOptimize(p);
        }
    }
}

BENCHMARK(BM_TemplatePoolAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK(BM_RuntimePoolAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);

static void BM_TemplatePoolFragmentation(benchmark::State &state) {
    dsl::static_fixed_size_pool_resource<sizeof(SmallObject), 8, alignof(SmallObject)> pool;

    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }

    for (auto _: state) {
        for (size_t i = 0; i < live_count; i += 2)
            pool.deallocate(live[i], sizeof(SmallObject), alignof(SmallObject));
        for (size_t i = 0; i < live_count; i += 2)
            live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }
}

static void BM_RuntimePoolFragmentation(benchmark::State &state) {
    dsl::fixed_size_pool_resource pool{{sizeof(SmallObject), 8, alignof(SmallObject)}};

    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }

    for (auto _: state) {
        for (size_t i = 0; i < live_count; i += 2)
            pool.deallocate(live[i], sizeof(SmallObject), alignof(SmallObject));
        for (size_t i = 0; i < live_count; i += 2)
            live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }
}

BENCHMARK(BM_TemplatePoolFragmentation)->MinWarmUpTime(1.0);
BENCHMARK(BM_RuntimePoolFragmentation)->MinWarmUpTime(1.0);

static void BM_TemplatePoolMixedPattern(benchmark::State &state) {
    dsl::static_fixed_size_pool_resource<sizeof(SmallObject), 32, alignof(SmallObject)> pool;

    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }

    std::mt19937                          gen(42);
    std::uniform_int_distribution<size_t> dist(0, live_count - 1);

    for (auto _: state) {
        for (int i = 0; i < 1000; i++) {
            const size_t randIdx = dist(gen);
            pool.deallocate(live[randIdx], sizeof(SmallObject), alignof(SmallObject));
            live[randIdx] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        }
    }
}

static void BM_RuntimePoolMixedPattern(benchmark::State &state) {
    dsl::fixed_size_pool_resource pool{{sizeof(SmallObject), 32, alignof(SmallObject)}};

    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }

    std::mt19937                          gen(42);
    std::uniform_int_distribution<size_t> dist(0, live_count - 1);

    for (auto _: state) {
        for (int i = 0; i < 1000; i++) {
            const size_t randIdx = dist(gen);
            pool.deallocate(live[randIdx], sizeof(SmallObject), alignof(SmallObject));
            live[randIdx] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        }
    }
}

BENCHMARK(BM_TemplatePoolMixedPattern)->MinWarmUpTime(1.0);
BENCHMARK(BM_RuntimePoolMixedPattern)->MinWarmUpTime(1.0);