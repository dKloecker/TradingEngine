#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "orderbook/matching_engine.h"

namespace trading::orderbook::test {
using TestEngine = MatchingEngine<0, 200>;

class MatchingEngineTest : public ::testing::Test {
public:
    std::mutex                   mu_;
    std::vector<ExecutionReport> reports_;

    TestEngine engine{
        // Drain from outputs into local vector
        [this](ExecutionReport &&r) {
            std::lock_guard g{mu_};
            reports_.push_back(std::move(r));
        }
    };

    // Submit a resting LIMIT order and discard its e_NEW report.
    void seed(const Order &order) {
        ASSERT_TRUE(engine.submit(AddMessage{order}));
        wait_for_reports(1);
        auto first = drain();
        ASSERT_EQ(first.size(), 1);
        ASSERT_EQ(first[0].exec_type, ExecType::e_NEW);
    }

    // Block until at least `n` reports have accumulated since the last drain, up to a generous timeout.
    void wait_for_reports(const size_t                    n,
                          const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard g{mu_};
                if (reports_.size() >= n) return;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    // Pull all reports collected so far, clearing the buffer.
    std::vector<ExecutionReport> drain() {
        std::lock_guard              g{mu_};
        std::vector<ExecutionReport> out;
        out.swap(reports_);
        return out;
    }
};

TEST_F(MatchingEngineTest, LimitNoMatch_RestsInBook) {
    engine.submit(AddMessage{{1, 50, 100, Side::e_BUY, OrderType::e_LIMIT}});
    wait_for_reports(1);

    auto reports = drain();
    ASSERT_EQ(reports.size(), 1);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_NEW);
    EXPECT_EQ(reports[0].order_id, 1);
    EXPECT_EQ(reports[0].fill_quantity, 0);
    EXPECT_EQ(reports[0].leaves_quantity, 100);
}

TEST_F(MatchingEngineTest, LimitPartialFill_RemainderRests) {
    seed({99, 40, 50, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 50, 100, Side::e_BUY, OrderType::e_LIMIT}});
    wait_for_reports(2);

    auto reports = drain();
    ASSERT_EQ(reports.size(), 2);

    EXPECT_EQ(reports[0].exec_type, ExecType::e_PARTIAL_FILL);
    EXPECT_EQ(reports[0].fill_quantity, 50);
    EXPECT_EQ(reports[0].leaves_quantity, 50);

    EXPECT_EQ(reports[1].exec_type, ExecType::e_NEW);
    EXPECT_EQ(reports[1].fill_quantity, 50);
    EXPECT_EQ(reports[1].leaves_quantity, 50);
}

TEST_F(MatchingEngineTest, LimitFullFill) {
    seed({99, 40, 100, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 50, 100, Side::e_BUY, OrderType::e_LIMIT}});
    wait_for_reports(1);

    const auto reports = drain();
    ASSERT_EQ(reports.size(), 1);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_FILL);
    EXPECT_EQ(reports[0].fill_quantity, 100);
    EXPECT_EQ(reports[0].leaves_quantity, 0);
}


TEST_F(MatchingEngineTest, MarketFullFill) {
    seed({99, 30, 60, Side::e_SELL, OrderType::e_LIMIT});
    seed({98, 40, 40, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 0, 100, Side::e_BUY, OrderType::e_MARKET}});
    wait_for_reports(2);

    const auto reports = drain();
    ASSERT_EQ(reports.size(), 2);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_PARTIAL_FILL);
    EXPECT_EQ(reports[0].fill_quantity, 60);
    EXPECT_EQ(reports[0].leaves_quantity, 40);
    EXPECT_EQ(reports[1].exec_type, ExecType::e_FILL);
    EXPECT_EQ(reports[1].fill_quantity, 40);
    EXPECT_EQ(reports[1].leaves_quantity, 0);
}

TEST_F(MatchingEngineTest, MarketPartialFill_CancelsRemainder) {
    seed({99, 40, 50, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 0, 100, Side::e_BUY, OrderType::e_MARKET}});
    wait_for_reports(2);

    auto reports = drain();
    ASSERT_EQ(reports.size(), 2);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_PARTIAL_FILL);
    EXPECT_EQ(reports[0].fill_quantity, 50);
    EXPECT_EQ(reports[1].exec_type, ExecType::e_CANCEL);
    EXPECT_EQ(reports[1].fill_quantity, 50);
    EXPECT_EQ(reports[1].leaves_quantity, 50);
}

TEST_F(MatchingEngineTest, MarketNoLiquidity_CancelsEntire) {
    engine.submit(AddMessage{{1, 0, 100, Side::e_BUY, OrderType::e_MARKET}});
    wait_for_reports(1);

    auto reports = drain();
    ASSERT_EQ(reports.size(), 1);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_CANCEL);
    EXPECT_EQ(reports[0].fill_quantity, 0);
    EXPECT_EQ(reports[0].leaves_quantity, 100);
}

TEST_F(MatchingEngineTest, IocPartialFill_CancelsRemainder) {
    seed({99, 40, 50, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 50, 100, Side::e_BUY, OrderType::e_IOC}});
    wait_for_reports(2);

    auto reports = drain();
    ASSERT_EQ(reports.size(), 2);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_PARTIAL_FILL);
    EXPECT_EQ(reports[0].fill_quantity, 50);
    EXPECT_EQ(reports[1].exec_type, ExecType::e_CANCEL);
    EXPECT_EQ(reports[1].fill_quantity, 50);
    EXPECT_EQ(reports[1].leaves_quantity, 50);
}

TEST_F(MatchingEngineTest, IocNoMatchWithinLimit_CancelsAll) {
    seed({99, 60, 100, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 50, 100, Side::e_BUY, OrderType::e_IOC}});
    wait_for_reports(1);

    const auto reports = drain();
    ASSERT_EQ(reports.size(), 1);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_CANCEL);
    EXPECT_EQ(reports[0].fill_quantity, 0);
    EXPECT_EQ(reports[0].leaves_quantity, 100);
}

TEST_F(MatchingEngineTest, FokSufficientLiquidity_FullFill) {
    seed({99, 40, 100, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 50, 100, Side::e_BUY, OrderType::e_FOK}});
    wait_for_reports(1);

    const auto reports = drain();
    ASSERT_EQ(reports.size(), 1);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_FILL);
    EXPECT_EQ(reports[0].fill_quantity, 100);
    EXPECT_EQ(reports[0].leaves_quantity, 0);
}

TEST_F(MatchingEngineTest, FokInsufficientLiquidity_Rejected) {
    seed({99, 40, 50, Side::e_SELL, OrderType::e_LIMIT});

    engine.submit(AddMessage{{1, 50, 100, Side::e_BUY, OrderType::e_FOK}});
    wait_for_reports(1);

    const auto reports = drain();
    ASSERT_EQ(reports.size(), 1);
    EXPECT_EQ(reports[0].exec_type, ExecType::e_REJECT);
    EXPECT_EQ(reports[0].fill_quantity, 0);
    EXPECT_EQ(reports[0].leaves_quantity, 100);
}

TEST_F(MatchingEngineTest, FullHistoryTest) {
    // Seed book with non-matching limit orders on both sides
    engine.submit(AddMessage{1, 10, 10, Side::e_BUY, OrderType::e_LIMIT});
    engine.submit(AddMessage{2, 10, 10, Side::e_BUY, OrderType::e_LIMIT});
    engine.submit(AddMessage{3, 20, 10, Side::e_BUY, OrderType::e_LIMIT});
    engine.submit(AddMessage{4, 30, 50, Side::e_SELL, OrderType::e_LIMIT});
    engine.submit(AddMessage{5, 25, 10, Side::e_SELL, OrderType::e_LIMIT});
    // Add a resting BUY then replace it with the replacement rests at the best ask
    engine.submit(AddMessage{6, 24, 10, Side::e_BUY, OrderType::e_LIMIT});
    engine.submit(ReplaceMessage{6, {7, 25, 5, Side::e_BUY, OrderType::e_MARKET}});
    // Partial cancel of order 1 (10 → 5 remaining)
    engine.submit(CancelMessage{1, 5});
    // FOK SELL 1000@10: only 30 qty available across all bids therefore rejected
    engine.submit(AddMessage{6, 10, 1000, Side::e_SELL, OrderType::e_FOK});
    // Cross-level SELL 20@20: absorbs bid@25 (5 qty, order 7) then bid@20 (10 qty, order 3), rests 5
    engine.submit(AddMessage{8, 20, 20, Side::e_SELL, OrderType::e_LIMIT});
    // MARKET BUY 15: clears ask@20 (5 qty, order 8) then ask@25 (10 qty, order 5), fully filled
    engine.submit(AddMessage{9, 0, 15, Side::e_BUY, OrderType::e_MARKET});

    wait_for_reports(15);
    const auto r = drain();
    ASSERT_EQ(r.size(), 15);

    // 5 seeded resting orders, all rest with no prior fills
    EXPECT_EQ(r[0].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[0].order_id, 1);
    EXPECT_EQ(r[0].fill_quantity, 0);
    EXPECT_EQ(r[0].leaves_quantity, 10);
    EXPECT_EQ(r[1].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[1].order_id, 2);
    EXPECT_EQ(r[1].fill_quantity, 0);
    EXPECT_EQ(r[1].leaves_quantity, 10);
    EXPECT_EQ(r[2].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[2].order_id, 3);
    EXPECT_EQ(r[2].fill_quantity, 0);
    EXPECT_EQ(r[2].leaves_quantity, 10);
    EXPECT_EQ(r[3].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[3].order_id, 4);
    EXPECT_EQ(r[3].fill_quantity, 0);
    EXPECT_EQ(r[3].leaves_quantity, 50);
    EXPECT_EQ(r[4].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[4].order_id, 5);
    EXPECT_EQ(r[4].fill_quantity, 0);
    EXPECT_EQ(r[4].leaves_quantity, 10);

    // Order 6 rests at 24 (best ask is 25, no match)
    EXPECT_EQ(r[5].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[5].order_id, 6);
    EXPECT_EQ(r[5].fill_quantity, 0);
    EXPECT_EQ(r[5].leaves_quantity, 10);

    // Replace: cancel 6, order 7 rests at 25
    EXPECT_EQ(r[6].exec_type, ExecType::e_CANCEL);
    EXPECT_EQ(r[6].order_id, 6);
    EXPECT_EQ(r[6].fill_quantity, 0);
    EXPECT_EQ(r[6].leaves_quantity, 10);
    EXPECT_EQ(r[7].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[7].order_id, 7);
    EXPECT_EQ(r[7].fill_quantity, 0);
    EXPECT_EQ(r[7].leaves_quantity, 5);

    // Partial cancel of order 1: cancels 5, 5 remain
    EXPECT_EQ(r[8].exec_type, ExecType::e_CANCEL);
    EXPECT_EQ(r[8].order_id, 1);
    EXPECT_EQ(r[8].fill_quantity, 0);
    EXPECT_EQ(r[8].leaves_quantity, 5);

    // FOK SELL 1000@10: bid qty sum = 5+10+(5+10) = 30 < 1000, rejected
    EXPECT_EQ(r[9].exec_type, ExecType::e_REJECT);
    EXPECT_EQ(r[9].order_id, 6);
    EXPECT_EQ(r[9].fill_quantity, 0);
    EXPECT_EQ(r[9].leaves_quantity, 1000);

    // Cross-level SELL 20@20: fills order 7 at 25 (5 qty), then order 3 at 20 (10 qty), rests 5
    EXPECT_EQ(r[10].exec_type, ExecType::e_PARTIAL_FILL);
    EXPECT_EQ(r[10].order_id, 8);
    EXPECT_EQ(r[10].fill_quantity, 5);
    EXPECT_EQ(r[10].leaves_quantity, 15);
    EXPECT_EQ(r[10].price, 25);
    EXPECT_EQ(r[11].exec_type, ExecType::e_PARTIAL_FILL);
    EXPECT_EQ(r[11].order_id, 8);
    EXPECT_EQ(r[11].fill_quantity, 10);
    EXPECT_EQ(r[11].leaves_quantity, 5);
    EXPECT_EQ(r[11].price, 20);
    EXPECT_EQ(r[12].exec_type, ExecType::e_NEW);
    EXPECT_EQ(r[12].order_id, 8);
    EXPECT_EQ(r[12].fill_quantity, 15);
    EXPECT_EQ(r[12].leaves_quantity, 5);

    // MARKET BUY 15: fills order 8 at 20 (5 qty), then order 5 at 25 (10 qty)
    EXPECT_EQ(r[13].exec_type, ExecType::e_PARTIAL_FILL);
    EXPECT_EQ(r[13].order_id, 9);
    EXPECT_EQ(r[13].fill_quantity, 5);
    EXPECT_EQ(r[13].leaves_quantity, 10);
    EXPECT_EQ(r[13].price, 20);
    EXPECT_EQ(r[14].exec_type, ExecType::e_FILL);
    EXPECT_EQ(r[14].order_id, 9);
    EXPECT_EQ(r[14].fill_quantity, 10);
    EXPECT_EQ(r[14].leaves_quantity, 0);
    EXPECT_EQ(r[14].price, 25);
}
} // namespace trading::orderbook::test
