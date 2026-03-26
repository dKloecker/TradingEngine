//
// Created by Dominic Kloecker on 23/03/2026.
//


#include <benchmark/benchmark.h>
#include <memory_resource>
#include "pool_allocator.h"

struct alignas(16) SmallObject {
    std::array<std::byte, 16> data;
};

// --- pool allocator ---
static void BM_PoolAllocate(benchmark::State &state) {
    dsl::PoolAllocator<sizeof(SmallObject), 64> pool;
    for (auto _: state) {
        void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, sizeof(SmallObject), alignof(SmallObject));
    }
}

BENCHMARK(BM_PoolAllocate)->MinWarmUpTime(1.0);

// --- system allocator baseline ---
static void BM_SystemAllocate(benchmark::State &state) {
    for (auto _: state) {
        void *p = ::operator new(sizeof(SmallObject));
        benchmark::DoNotOptimize(p);
        ::operator delete(p);
    }
}

BENCHMARK(BM_SystemAllocate)->MinWarmUpTime(1.0);

// --- sustained allocation without free (measures block growth) ---
static void BM_PoolAllocateSustained(benchmark::State &state) {
    for (auto _: state) {
        dsl::PoolAllocator<sizeof(SmallObject), 64> pool;
        for (int i = 0; i < state.range(0); i++) {
            void *p = pool.allocate(sizeof(SmallObject), alignof(SmallObject));
            benchmark::DoNotOptimize(p);
        }
    }
}

BENCHMARK(BM_PoolAllocateSustained)->Range(8, 1024)->MinWarmUpTime(1.0);
