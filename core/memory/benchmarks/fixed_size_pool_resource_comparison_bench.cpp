#include <benchmark/benchmark.h>
#include <memory_resource>
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
static void BM_PoolAllocate(benchmark::State &state) {
    dsl::fixed_size_pool_resource<sizeof(T), 64, alignof(T)> pool;
    void *                                                   warmup = pool.allocate(sizeof(T), alignof(T));
    pool.deallocate(warmup, sizeof(T), alignof(T));
    for (auto _: state) {
        void *p = pool.allocate(sizeof(T), alignof(T));
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, sizeof(T), alignof(T));
    }
}

template<typename T>
static void BM_PmrSysPoolAllocate(benchmark::State &state) {
    std::pmr::synchronized_pool_resource pool;
    void *                               warmup = pool.allocate(sizeof(T), alignof(T));
    pool.deallocate(warmup, sizeof(T), alignof(T));

    for (auto _: state) {
        void *p = pool.allocate(sizeof(T), alignof(T));
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, sizeof(T), alignof(T));
    }
}

template<typename T>
static void BM_SystemAllocate(benchmark::State &state) {
    for (auto _: state) {
        void *p = ::operator new(sizeof(T));
        benchmark::DoNotOptimize(p);
        ::operator delete(p);
    }
}

BENCHMARK_TEMPLATE(BM_PoolAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PoolAllocate, LargeObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PmrSysPoolAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PmrSysPoolAllocate, LargeObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SystemAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SystemAllocate, LargeObject)->MinWarmUpTime(1.0);

static void BM_PoolBlockGrowth(benchmark::State &state) {
    for (auto _: state) {
        state.PauseTiming();
        dsl::fixed_size_pool_resource<sizeof(SmallObject), 2, alignof(SmallObject)> pool;
        void *p1 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        void *p2 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p1);
        benchmark::DoNotOptimize(p2);
        state.ResumeTiming();

        void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p);
    }
}

static void BM_PmrPoolBlockGrowth(benchmark::State &state) {
    for (auto _: state) {
        state.PauseTiming();
        std::pmr::synchronized_pool_resource pool{
            std::pmr::pool_options{2, sizeof(SmallObject)}
        };
        void *p1 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        void *p2 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p1);
        benchmark::DoNotOptimize(p2);
        state.ResumeTiming();

        void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p);
    }
}

BENCHMARK(BM_PoolBlockGrowth)->MinWarmUpTime(1.0);
BENCHMARK(BM_PmrPoolBlockGrowth)->MinWarmUpTime(1.0);

template<typename T>
static void BM_PoolSustainedLiveObjects(benchmark::State &state) {
    const size_t                                             live_count = state.range(0);
    dsl::fixed_size_pool_resource<sizeof(T), 64, alignof(T)> pool;

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
static void BM_PmrSyncPoolSustainedLiveObjects(benchmark::State &state) {
    const size_t                         live_count = state.range(0);
    std::pmr::synchronized_pool_resource pool{
        std::pmr::pool_options{64, sizeof(T)}
    };

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

BENCHMARK_TEMPLATE(BM_PoolSustainedLiveObjects, SmallObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PoolSustainedLiveObjects, LargeObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PmrSyncPoolSustainedLiveObjects, SmallObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PmrSyncPoolSustainedLiveObjects, LargeObject)->Range(8, 1024)->MinWarmUpTime(1.0);

static void BM_PoolAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        dsl::fixed_size_pool_resource<sizeof(SmallObject), 64, alignof(SmallObject)> pool;
        for (int i = 0; i < state.range(0); i++) {
            void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
            benchmark::DoNotOptimize(p);
        }
    }
}

static void BM_PmrSysPoolAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        std::pmr::synchronized_pool_resource pool{
            std::pmr::pool_options{64, sizeof(SmallObject)}
        };

        for (int i = 0; i < state.range(0); i++) {
            void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
            benchmark::DoNotOptimize(p);
        }
    }
}

static void BM_SysAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        for (int i = 0; i < state.range(0); i++) {
            void *p = ::operator new(sizeof(SmallObject));
            benchmark::DoNotOptimize(p);
            ::operator delete(p);
        }
    }
}

BENCHMARK(BM_PoolAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK(BM_PmrSysPoolAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK(BM_SysAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);

static void BM_PoolFragmentation(benchmark::State &state) {
    dsl::fixed_size_pool_resource<sizeof(SmallObject), 8, alignof(SmallObject)> pool;

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

static void BM_PmrSyncPoolFragmentation(benchmark::State &state) {
    std::pmr::synchronized_pool_resource pool{
        std::pmr::pool_options{8, sizeof(SmallObject)}
    };

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

static void BM_SysFragmentation(benchmark::State &state) {
    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = ::operator new(sizeof(SmallObject));
    }

    for (auto _: state) {
        for (size_t i = 0; i < live_count; i += 2)
            ::operator delete(live[i]);
        for (size_t i = 0; i < live_count; i += 2)
            live[i] = ::operator new(sizeof(SmallObject));
    }
}

BENCHMARK(BM_PoolFragmentation)->MinWarmUpTime(1.0);
BENCHMARK(BM_PmrSyncPoolFragmentation)->MinWarmUpTime(1.0);
BENCHMARK(BM_SysFragmentation)->MinWarmUpTime(1.0);

static void BM_PoolMixedPattern(benchmark::State &state) {
    dsl::fixed_size_pool_resource<sizeof(SmallObject), 32, alignof(SmallObject)> pool;

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

static void BM_PmrSyncPoolMixedPattern(benchmark::State &state) {
    std::pmr::synchronized_pool_resource pool{
        std::pmr::pool_options{32, sizeof(SmallObject)}
    };

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

static void BM_SysMixedPattern(benchmark::State &state) {
    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = ::operator new(sizeof(SmallObject));
    }

    std::mt19937                          gen(42);
    std::uniform_int_distribution<size_t> dist(0, live_count - 1);

    for (auto _: state) {
        for (int i = 0; i < 1000; i++) {
            const size_t randIdx = dist(gen);
            ::operator delete(live[randIdx]);
            live[randIdx] = ::operator new(sizeof(SmallObject));
        }
    }
}

BENCHMARK(BM_PoolMixedPattern)->MinWarmUpTime(1.0);
BENCHMARK(BM_PmrSyncPoolMixedPattern)->MinWarmUpTime(1.0);
BENCHMARK(BM_SysMixedPattern)->MinWarmUpTime(1.0);
