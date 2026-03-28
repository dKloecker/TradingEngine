//
// Created by Dominic Kloecker on 23/03/2026.
//


#include <benchmark/benchmark.h>
#include <memory_resource>
#include <random>
#include "pool_allocator.h"

struct alignas(16) SmallObject {
    std::array<std::byte, 16> data;
};

struct alignas(64) LargeObject {
    std::array<int, 1000> data;
    std::string           lorem_ipsum =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";
};

template<typename T>
static void BM_PoolAllocate(benchmark::State &state) {
    dsl::PoolAllocator<sizeof(T), 64, alignof(T)> pool;
    // warm up and initial block allocation so that we do not have any growth.
    void *warmup = pool.allocate(sizeof(T), alignof(T));
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

// General Allocation Performance
BENCHMARK_TEMPLATE(BM_PoolAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PoolAllocate, LargeObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PmrSysPoolAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PmrSysPoolAllocate, LargeObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SystemAllocate, SmallObject)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_SystemAllocate, LargeObject)->MinWarmUpTime(1.0);

static void BM_PoolBlockGrowth(benchmark::State &state) {
    for (auto _: state) {
        // fill initial block
        state.PauseTiming();
        dsl::PoolAllocator<sizeof(SmallObject), 2> pool;
        void *                                     p1 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        void *                                     p2 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p1);
        benchmark::DoNotOptimize(p2);
        state.ResumeTiming();

        // guaranteed growth allocation
        void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p);
    }
}


static void BM_PmrPoolBlockGrowth(benchmark::State &state) {
    for (auto _: state) {
        // fill initial block
        state.PauseTiming();
        std::pmr::synchronized_pool_resource pool{
            std::pmr::pool_options{2, sizeof(SmallObject)}
        };
        void *p1 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        void *p2 = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p1);
        benchmark::DoNotOptimize(p2);
        state.ResumeTiming();
        // guaranteed growth allocation
        void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p);
    }
}

BENCHMARK(BM_PmrPoolBlockGrowth)->MinWarmUpTime(1.0);
BENCHMARK(BM_PoolBlockGrowth)->MinWarmUpTime(1.0);

template<typename T>
static void BM_PoolSustainedLiveObjects(benchmark::State &state) {
    const size_t                                  live_count = state.range(0);
    dsl::PoolAllocator<sizeof(T), 64, alignof(T)> pool;

    // Pre-populate pool with N objects
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(T), alignof(T));
    }

    // Ensure no block growth with warmup
    void *warmup = pool.allocate(sizeof(T), alignof(T));
    pool.deallocate(warmup, sizeof(T), alignof(T));

    // Measure cost of allocation with pool already containing N objects
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

BENCHMARK_TEMPLATE(BM_PmrSyncPoolSustainedLiveObjects, SmallObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PmrSyncPoolSustainedLiveObjects, LargeObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PoolSustainedLiveObjects, SmallObject)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK_TEMPLATE(BM_PoolSustainedLiveObjects, LargeObject)->Range(8, 1024)->MinWarmUpTime(1.0);

static void BM_SysAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        for (int i = 0; i < state.range(0); i++) {
            void *p = ::operator new(sizeof(SmallObject));
            benchmark::DoNotOptimize(p);
            ::operator delete(p);
        }
    }
}

static void BM_PoolAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        dsl::PoolAllocator<sizeof(SmallObject), 64> pool;
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

// Measure sustained allocation performance
BENCHMARK(BM_SysAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK(BM_PmrSysPoolAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);
BENCHMARK(BM_PoolAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);

static void BM_PoolAllocateDeallocationOnFragmentation(benchmark::State &state) {
    dsl::PoolAllocator<sizeof(SmallObject), 8> pool;

    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }
    // free even slots to create fragmentation pattern
    for (size_t i = 0; i < live_count; i += 2) {
        pool.deallocate(live[i], sizeof(SmallObject), alignof(SmallObject));
    }

    for (auto _: state) {
        // free even slots
        for (size_t i = 0; i < live_count; i += 2)
            pool.deallocate(live[i], sizeof(SmallObject), alignof(SmallObject));
        // reallocate into fragmented free list
        for (size_t i = 0; i < live_count; i += 2)
            live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }
}

static void BM_PmrSyncPoolAllocateDeallocationOnFragmentation(benchmark::State &state) {
    std::pmr::synchronized_pool_resource pool{
        std::pmr::pool_options{8, sizeof(SmallObject)}
    };

    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }
    for (size_t i = 0; i < live_count; i += 2) {
        pool.deallocate(live[i], sizeof(SmallObject), alignof(SmallObject));
    }

    for (auto _: state) {
        for (size_t i = 0; i < live_count; i += 2)
            pool.deallocate(live[i], sizeof(SmallObject), alignof(SmallObject));
        for (size_t i = 0; i < live_count; i += 2)
            live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }
}

static void BM_SysAllocateDeallocationOnFragmentation(benchmark::State &state) {
    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = ::operator new(sizeof(SmallObject));
    }
    for (size_t i = 0; i < live_count; i += 2) {
        ::operator delete(live[i]);
    }

    for (auto _: state) {
        for (size_t i = 0; i < live_count; i += 2)
            ::operator delete(live[i]);
        for (size_t i = 0; i < live_count; i += 2)
            live[i] = ::operator new(sizeof(SmallObject));
    }
}

BENCHMARK(BM_SysAllocateDeallocationOnFragmentation)->MinWarmUpTime(1.0);
BENCHMARK(BM_PmrSyncPoolAllocateDeallocationOnFragmentation)->MinWarmUpTime(1.0);
BENCHMARK(BM_PoolAllocateDeallocationOnFragmentation)->MinWarmUpTime(1.0);

static void BM_PoolAllocateMixedAllocAndDeallocPattern(benchmark::State &state) {
    dsl::PoolAllocator<sizeof(SmallObject), 32> pool;

    constexpr size_t    live_count = 100;
    std::vector<void *> live(live_count);
    for (size_t i = 0; i < live_count; i++) {
        live[i] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
    }

    std::mt19937                          gen(42); // fixed seed for reproducibility
    std::uniform_int_distribution<size_t> dist(0, live_count - 1);

    for (auto _: state) {
        for (int i = 0; i < 1000; i++) {
            const size_t randIdx = dist(gen);
            pool.deallocate(live[randIdx], sizeof(SmallObject), alignof(SmallObject));
            live[randIdx] = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        }
    }
}

static void BM_PmrSyncPoolAllocateMixedAllocAndDeallocPattern(benchmark::State &state) {
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

static void BM_SysAllocateMixedAllocAndDeallocPattern(benchmark::State &state) {
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

BENCHMARK(BM_PoolAllocateMixedAllocAndDeallocPattern)->MinWarmUpTime(1.0);
BENCHMARK(BM_PmrSyncPoolAllocateMixedAllocAndDeallocPattern)->MinWarmUpTime(1.0);
BENCHMARK(BM_SysAllocateMixedAllocAndDeallocPattern)->MinWarmUpTime(1.0);

// TODO: make benchmarks for using Allocator as Store for Vector
