//
// Created by Dominic Kloecker on 23/03/2026.
//
#include <gtest/gtest.h>
#include "core/queue/spsc_queue.h"

#include <thread>

namespace dsl::test::queue {

TEST(SpscQueue, TopReturnsNullptrWhenEmpty) {
    dsl::spsc_queue<int, 16> q;
    EXPECT_EQ(q.top(), nullptr);
}

TEST(SpscQueue, TryPopReturnsFalseWhenEmpty) {
    dsl::spsc_queue<int, 16> q;
    int                      val = 42;
    EXPECT_FALSE(q.pop(val));
    EXPECT_EQ(val, 42);
}

TEST(SpscQueue, PopReturnsNulloptWhenEmpty) {
    dsl::spsc_queue<int, 16> q;
    EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(SpscQueue, PushMakesTopAvailable) {
    dsl::spsc_queue<int, 16> q;
    q.push(1);
    ASSERT_NE(q.top(), nullptr);
    EXPECT_EQ(*q.top(), 1);
}

TEST(SpscQueue, PushDoesNotAdvanceTop) {
    dsl::spsc_queue<int, 16> q;
    q.push(1);
    q.push(2);
    ASSERT_NE(q.top(), nullptr);
    EXPECT_EQ(*q.top(), 1);
}

TEST(SpscQueue, PushFailsWhenFull) {
    dsl::spsc_queue<int, 16> q;
    for (int i = 0; i < 16; i++)
        ASSERT_TRUE(q.push(i));
    EXPECT_FALSE(q.push(999));
}

TEST(SpscQueue, TryPopReturnsOldestElement) {
    dsl::spsc_queue<int, 16> q;
    q.push(1);
    q.push(2);
    int val = 0;
    ASSERT_TRUE(q.pop(val));
    EXPECT_EQ(val, 1);
}

TEST(SpscQueue, PopReturnsOldestElement) {
    dsl::spsc_queue<int, 16> q;
    q.push(1);
    q.push(2);
    EXPECT_EQ(q.try_pop(), 1);
}

TEST(SpscQueue, FifoOrdering) {
    dsl::spsc_queue<int, 16> q;
    for (int i = 0; i < 16; i++) q.push(i);

    for (int i = 0; i < 16; i++) {
        ASSERT_NE(q.top(), nullptr);
        EXPECT_EQ(*q.top(), i);
        EXPECT_EQ(q.try_pop(), i);
    }
    EXPECT_EQ(q.top(), nullptr);
}

TEST(SpscQueue, PushPopAcrossWrapBoundary) {
    dsl::spsc_queue<int, 4> q;
    // Fill and drain twice to force the indices past capacity
    for (int round = 0; round < 3; round++) {
        // fill
        for (int i = 0; i < 4; i++)
            ASSERT_TRUE(q.push(round * 4 + i));
        // drain
        for (int i = 0; i < 4; i++)
            EXPECT_EQ(q.try_pop(), round * 4 + i);
    }
}

TEST(SpscQueue, DestructorDestroysRemainingElements) {
    int destructions = 0;
    struct Tracked {
        int *counter_;

        explicit Tracked(int *c)
            : counter_(c) {
        }

        ~Tracked() { ++(*counter_); }
    };

    {
        dsl::spsc_queue<Tracked, 8> q;
        for (int i = 0; i < 8; i++) q.push(Tracked(&destructions));
    }
    EXPECT_EQ(destructions, 8);
}

TEST(SpscQueue, ProducerConsumerThreads) {
    dsl::spsc_queue<std::string, 16> q;
    std::vector<std::string>         elements{
        "Hello",
        "World",
        "How",
        "Are",
        "You",
        "Today",
        "My",
        "Friend"
    };

    std::vector<std::string> popped;
    std::atomic_bool         complete = false;

    std::thread t1{
        [&] {
            for (const auto &el: elements) {
                q.push(el);
            }
            complete.store(true);
        }
    };
    std::thread t2{
        [&] {
            std::string pop;
            while (!complete.load(std::memory_order_acquire)) {
                if (q.pop(pop)) popped.push_back(pop);
            }
            // Drain anything remaining
            while (q.pop(pop)) popped.push_back(pop);
        }
    };
    t1.join();
    t2.join();
    EXPECT_EQ(elements, popped);
    EXPECT_TRUE(q.empty());
}

} // namespace dsl::test::queue
