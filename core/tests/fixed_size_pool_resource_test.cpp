//
// Created by Dominic Kloecker on 23/03/2026.
//
#include <gtest/gtest.h>
#include "fixed_size_pool_resource.h"

// Concrete sizes for testing
using SmallPool = dsl::fixed_size_pool_resource_bench<16, 4>; // 16-byte chunks, 4 per block

TEST(PoolAllocator, AllocateReturnsNonNull) {
    SmallPool pool;
    void *    p = pool.allocate(16, alignof(std::max_align_t));
    ASSERT_NE(p, nullptr);
}

TEST(PoolAllocator, AllocateIsAligned) {
    dsl::fixed_size_pool_resource_bench<sizeof(int), 4, 16> pool;
    void *                                            p = pool.allocate(16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0);
}

TEST(PoolAllocator, CanAllocateDifferentAllignment) {
    dsl::fixed_size_pool_resource_bench<sizeof(int), 4, 4> pool1;
    void *                                           p1 = pool1.allocate(8, 4);
    ASSERT_NE(p1, nullptr);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(p1) % 4, 0);

    dsl::fixed_size_pool_resource_bench<sizeof(int), 4, 8> pool2;
    void *                                           p2 = pool2.allocate(4, 8);
    ASSERT_NE(p2, nullptr);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(p2) % 8, 0); // satisfies requested alignment

    ASSERT_NE(p1, p2);
}


TEST(PoolAllocator, GrowthDoesNotInvalidate) {
    constexpr size_t chunk_size       = 16;
    constexpr size_t chunks_per_block = 2;
    constexpr size_t alignment        = alignof(std::max_align_t);

    dsl::fixed_size_pool_resource_bench<chunk_size, chunks_per_block> pool;

    // fill first block (2 chunks)
    auto *ptr1 = static_cast<char *>(pool.allocate(chunk_size, alignment));
    auto *ptr2 = static_cast<char *>(pool.allocate(chunk_size, alignment));
    std::memcpy(ptr1, "0123456789abcde", chunk_size);
    std::memcpy(ptr2, "0123456789ABCDE", chunk_size);

    // check before growth
    EXPECT_STREQ(ptr1, "0123456789abcde");
    EXPECT_STREQ(ptr2, "0123456789ABCDE");
    // force growth
    auto *ptr3 = static_cast<char *>(pool.allocate(chunk_size, alignment));
    std::memcpy(ptr3, "hello_c_3_blk_2", chunk_size);

    // old pointers still valid
    EXPECT_STREQ(ptr1, "0123456789abcde") << "ptr1 invalidated by block growth";
    EXPECT_STREQ(ptr2, "0123456789ABCDE") << "ptr2 invalidated by block growth";
    EXPECT_STREQ(ptr3, "hello_c_3_blk_2");

    // all pointers are aligned
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % alignment, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % alignment, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % alignment, 0);
}

TEST(PoolAllocator, DeallocateAllowsReuse) {
    SmallPool pool;
    auto *    ptr1 = static_cast<char *>(pool.allocate(16));
    std::memcpy(ptr1, "0123456789", 10);
    // size and alignment should be ignored
    pool.deallocate(ptr1, 0, 0);
    auto *ptr2 = static_cast<char *>(pool.allocate(16));
    std::memcpy(ptr2, "9876543210", 10);
    // since we de-allocated, should re-use the adresse of ptr1
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_STREQ(ptr1, "9876543210");
}

TEST(PoolAllocator, DeallocateUsesPreviousFree) {
    SmallPool pool;
    // allocate twice within same blocks
    auto *ptr1 = static_cast<char *>(pool.allocate(8));
    auto *ptr2 = static_cast<char *>(pool.allocate(8));

    // before: free_: []
    // after : deallocate(ptr2) -> free_: [ptr2]
    // after : deallocate(ptr1) -> free_: [ptr1 -> ptr2]
    pool.deallocate(ptr2, 0, 0);
    pool.deallocate(ptr1, 0, 0);
    // so next alloc returns ptr1, then ptr2
    EXPECT_EQ(ptr1, static_cast<char*>(pool.allocate(8)));
    EXPECT_EQ(ptr2, static_cast<char*>(pool.allocate(8)));
}

TEST(PoolAllocator, DeallocateAcrossBlocks) {
    dsl::fixed_size_pool_resource_bench<16, 2> pool; // 2 chunks per block — easy to force growth

    auto *p1 = pool.allocate(16); // block 1, chunk 0
    auto *p2 = pool.allocate(16); // block 1, chunk 1
    auto *p3 = pool.allocate(16); // forces block 2, chunk 0

    // free from different blocks
    pool.deallocate(p1, 0, 0); // block 1 chunk back on free list
    pool.deallocate(p3, 0, 0); // block 2 chunk back on free list

    // both should be reusable regardless of which block they came from
    auto *r1 = pool.allocate(16);
    auto *r2 = pool.allocate(16);

    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    ASSERT_NE(r1, r2);

    // should re-use in LIFO but that could be any freed resource.
    const bool r1_reused = (r1 == p1 || r1 == p3);
    const bool r2_reused = (r2 == p1 || r2 == p3);
    EXPECT_TRUE(r1_reused) << "Expected a previously freed chunk";
    EXPECT_TRUE(r2_reused) << "Expected a previously freed chunk";
}

TEST(PoolAllocator, UseAllocatorInPmrContainer) {
    dsl::fixed_size_pool_resource_bench<10024, 16> pool;
    std::pmr::vector<int>                    v{&pool};
    for (int i = 0; i < 100; i++) v.push_back(i);
    for (int i = 0; i < 100; i++)
        EXPECT_EQ(v[i], i);
}


