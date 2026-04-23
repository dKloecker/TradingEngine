//
// Created by Dominic Kloecker on 23/04/2026.
//
#include <gtest/gtest.h>
#include <cstring>
#include <memory_resource>
#include <unordered_map>
#include <vector>

#include "core/memory/pool_resource.h"

namespace dsl::test::memory {
/** Test upstream resource to count operations */
class counting_resource : public std::pmr::memory_resource {
public:
    size_t alloc_count   = 0;
    size_t dealloc_count = 0;
    size_t last_bytes    = 0;

    void reset() {
        alloc_count   = 0;
        dealloc_count = 0;
        last_bytes    = 0;
    }

private:
    void *do_allocate(const size_t bytes, const size_t alignment) override {
        ++alloc_count;
        last_bytes = bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *p, const size_t bytes, const size_t alignment) override {
        ++dealloc_count;
        last_bytes = bytes;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const memory_resource &other) const noexcept override {
        return this == &other;
    }
};

// largest_required_chunk=64 gives pools at {8, 16, 32, 64}.
constexpr pool_resource::pool_options default_options{
    .max_chunks_per_block   = 8,
    .largest_required_chunk = 64
};

TEST(PoolResource, AllocateReturnsNonNull) {
    pool_resource pool{default_options};
    void *        p = pool.allocate(16, alignof(std::max_align_t));
    ASSERT_NE(p, nullptr);
}

TEST(PoolResource, RoutesSmallRequestToSmallestPool) {
    pool_resource pool{default_options};
    auto *        p1 = pool.allocate(8);
    pool.deallocate(p1, 8);
    auto *p2 = pool.allocate(8);
    EXPECT_EQ(p1, p2);
}

TEST(PoolResource, RoutesDifferentSizesToDifferentPools) {
    pool_resource pool{default_options};
    // Allocate from two different size classes
    auto *p_small = pool.allocate(8);
    auto *p_large = pool.allocate(64);
    // Free both
    pool.deallocate(p_small, 8);
    pool.deallocate(p_large, 64);
    // Reallocate same sizes
    auto *p_small_again = pool.allocate(8);
    auto *p_large_again = pool.allocate(64);

    EXPECT_EQ(p_small, p_small_again);
    EXPECT_EQ(p_large, p_large_again);
    EXPECT_NE(p_small_again, p_large_again);
}

TEST(PoolResource, SizesWithinSamePoolShareChunks) {
    pool_resource pool{default_options};
    auto *        p9 = pool.allocate(9);
    pool.deallocate(p9, 9);
    auto *p16 = pool.allocate(16);
    EXPECT_EQ(p9, p16);
}

TEST(PoolResource, SizesAcrossPowerOfTwoBoundaryDoNotShare) {
    pool_resource pool{default_options};
    auto *        p16 = pool.allocate(16);
    auto *        p17 = pool.allocate(17);
    pool.deallocate(p16, 16);
    pool.deallocate(p17, 17);

    auto *p16_again = pool.allocate(16);
    auto *p17_again = pool.allocate(17);
    EXPECT_EQ(p16, p16_again);
    EXPECT_EQ(p17, p17_again);
    EXPECT_NE(p16_again, p17_again);
}

TEST(PoolResource, OversizeRequestGoesToUpstream) {
    counting_resource upstream;
    pool_resource     pool{default_options, &upstream};

    // largest_required_chunk is 64 but ask for 128
    constexpr size_t oversize = 128;
    void *           p        = pool.allocate(oversize);

    EXPECT_EQ(upstream.alloc_count, 1);
    EXPECT_EQ(upstream.last_bytes, oversize);
    ASSERT_NE(p, nullptr);
    // Cleanup
    pool.deallocate(p, oversize);
}

TEST(PoolResource, SmallRequestDoesNotTouchUpstream) {
    counting_resource upstream;
    pool_resource     pool{default_options, &upstream};

    void *p = pool.allocate(16);
    EXPECT_EQ(upstream.alloc_count, 0);

    pool.deallocate(p, 16);
    EXPECT_EQ(upstream.dealloc_count, 0);
}

TEST(PoolResource, OversizeDeallocateReturnsToUpstream) {
    counting_resource upstream;
    pool_resource     pool{default_options, &upstream};

    constexpr size_t oversize = 256;
    void *           p        = pool.allocate(oversize);
    EXPECT_EQ(upstream.alloc_count, 1);

    pool.deallocate(p, oversize);
    EXPECT_EQ(upstream.dealloc_count, 1);
    EXPECT_EQ(upstream.last_bytes, oversize);
}

TEST(PoolResource, MixedSmallAndOversizeRequests) {
    counting_resource upstream;
    pool_resource     pool{default_options, &upstream};

    auto *small1 = pool.allocate(8);
    auto *big1   = pool.allocate(200);
    auto *small2 = pool.allocate(32);
    auto *big2   = pool.allocate(500);

    EXPECT_EQ(upstream.alloc_count, 2);

    pool.deallocate(small1, 8);
    pool.deallocate(big1, 200);
    pool.deallocate(small2, 32);
    pool.deallocate(big2, 500);

    EXPECT_EQ(upstream.dealloc_count, 2u);
}

TEST(PoolResource, DefaultConstructorUsesDefaultResource) {
    // No explicit upstream, will result to should fall back to std::pmr::get_default_resource()
    pool_resource pool{default_options};
    // Oversize allocation must still succeed via the default resource
    constexpr size_t oversize = 1024;
    void *           p        = pool.allocate(oversize);
    ASSERT_NE(p, nullptr);
    pool.deallocate(p, oversize);
}

TEST(PoolResource, ClampsLargestRequiredChunkAboveLimit) {
    constexpr pool_resource::pool_options opts{
        .max_chunks_per_block   = 4,
        .largest_required_chunk = 1 << 20
    };
    counting_resource upstream;
    pool_resource     pool{opts, &upstream};

    // A request just above max_chunk_size should go upstream
    constexpr size_t oversize = (1 << 12) + 1;
    void *           p        = pool.allocate(oversize);
    EXPECT_EQ(upstream.alloc_count, 1);
    pool.deallocate(p, oversize);
}

TEST(PoolResource, ClampsLargestRequiredChunkBelowMin) {
    // Below sizeof(void*). Should still construct and at least one pool must exist.
    constexpr pool_resource::pool_options opts{
        .max_chunks_per_block   = 4,
        .largest_required_chunk = 1
    };
    pool_resource pool{opts};

    // Smallest possible request still goes to the smallest pool
    void *p = pool.allocate(1);
    ASSERT_NE(p, nullptr);
    pool.deallocate(p, 1);
}

TEST(PoolResource, InterleavedAllocationsAcrossPools) {
    pool_resource pool{default_options};

    auto *p8  = pool.allocate(8);
    auto *p16 = pool.allocate(16);
    auto *p32 = pool.allocate(32);
    auto *p64 = pool.allocate(64);

    // Write distinct pattern into each
    std::memset(p8, 0xA, 8);
    std::memset(p16, 0xB, 16);
    std::memset(p32, 0xC, 32);
    std::memset(p64, 0xD, 64);

    // All pointers must be distinct
    std::vector<void *> ptrs{p8, p16, p32, p64};
    for (size_t i = 0; i < ptrs.size(); ++i)
        for (size_t j = i + 1; j < ptrs.size(); ++j)
            EXPECT_NE(ptrs[i], ptrs[j]);

    // Deallocate in reverse order (independent pools should not interfere)
    pool.deallocate(p64, 64);
    pool.deallocate(p32, 32);
    pool.deallocate(p16, 16);
    pool.deallocate(p8, 8);

    // Reallocate same sizes — each pool's free list should restore its chunk
    EXPECT_EQ(p8, pool.allocate(8));
    EXPECT_EQ(p16, pool.allocate(16));
    EXPECT_EQ(p32, pool.allocate(32));
    EXPECT_EQ(p64, pool.allocate(64));
}


TEST(PoolResource, AllAllocationsMaxAligned) {
    pool_resource pool{default_options};

    constexpr size_t    alignment = alignof(std::max_align_t);
    constexpr size_t    sizes[]   = {8, 16, 24, 32, 48, 64};
    std::vector<void *> ptrs;

    for (size_t sz: sizes) {
        void *p = pool.allocate(sz);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignment, 0u)
            << "Allocation of size " << sz << " not aligned to max_align_t";
        ptrs.push_back(p);
    }

    for (size_t i = 0; i < ptrs.size(); ++i) pool.deallocate(ptrs[i], sizes[i]);
}

TEST(PoolResource, WorksWithPmrVector) {
    constexpr pool_resource::pool_options opts{
        .max_chunks_per_block   = 16,
        .largest_required_chunk = 4096
    };
    pool_resource         pool{opts};
    std::pmr::vector<int> v{&pool};
    for (int i = 0; i < 5000; ++i) v.push_back(i);
    for (int i = 0; i < 5000; ++i)
        EXPECT_EQ(v[i], i);
}

TEST(PoolResource, WorksWithPmrUnorderedMap) {
    constexpr pool_resource::pool_options opts{
        .max_chunks_per_block   = 32,
        .largest_required_chunk = 4096
    };
    pool_resource                     pool{opts};
    std::pmr::unordered_map<int, int> m{&pool};

    for (int i = 0; i < 1000; ++i) m.emplace(i, i * 2);
    EXPECT_EQ(m.size(), 1000);
    for (int i = 0; i < 1000; ++i) {
        const auto it = m.find(i);
        ASSERT_NE(it, m.end());
        EXPECT_EQ(it->second, i * 2);
    }
}
} // namespace dsl::test::memory
