#include <gtest/gtest.h>
#include <vector>

#include "orderbook/order_book.h"

namespace trading::orderbook::test {
using namespace trading::orderbook;

struct TradeEvent {
    TickPrice price;
    Quantity  qty;
    Side      aggressor; // opposite of resting/matched side
};

struct BookUpdateEvent {
    Side side;
};

class TestListener : public BaseOrderBookCallBackHandler<TestListener> {
public:
    std::vector<TradeEvent>      trades;
    std::vector<BookUpdateEvent> updates;

    void on_add_impl(const Order &) {}
    void on_cancel_impl(const Order &, Quantity) {}
    void on_replace_impl(const Order &, const Order &) {}

    void on_execute_impl(OrderId, const Side matched_side, const TickPrice price,
                         const Quantity filled, Quantity) {
        const Side aggressor = (matched_side == Side::e_BUY) ? Side::e_SELL : Side::e_BUY;
        trades.push_back({price, filled, aggressor});
    }

    void on_book_update_impl(Side side) {
        updates.push_back({side});
    }

    void reset() {
        trades.clear();
        updates.clear();
    }
};

using TestBook = OrderBook<0, 100, TestListener>;

class OrderBookTest : public ::testing::Test {
public:
    TestListener listener_;
    TestBook     book{&listener_};

    const TestBook &    cbook() const { return book; }
    const TestListener &listener() const { return listener_; }
};

TEST_F(OrderBookTest, AddBuyOrder) {
    ASSERT_TRUE(book.add({1, 50, 100}));

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.count, 1);
    EXPECT_EQ(level.total_quantity, 100);
    EXPECT_EQ(level.head->order.order_id, 1);
    EXPECT_EQ(level.head, level.tail);

    ASSERT_EQ(listener().updates.size(), 1);
    EXPECT_EQ(listener().updates[0].side, Side::e_BUY);
    EXPECT_TRUE(listener().trades.empty());
}

TEST_F(OrderBookTest, AddSellOrder) {
    ASSERT_TRUE(book.add({1, 60, 200, Side::e_SELL}));

    const auto &level = cbook().level_at(Side::e_SELL, 60);
    EXPECT_EQ(level.count, 1);
    EXPECT_EQ(level.total_quantity, 200);
    EXPECT_EQ(level.head->order.side, Side::e_SELL);

    ASSERT_EQ(listener().updates.size(), 1);
    EXPECT_EQ(listener().updates[0].side, Side::e_SELL);
    EXPECT_TRUE(listener().trades.empty());
}

TEST_F(OrderBookTest, AddDuplicateIdRejected) {
    ASSERT_TRUE(book.add({1, 50, 100}));
    EXPECT_FALSE(book.add({1, 55, 200}));

    const auto *bo = cbook().find_order(1);
    EXPECT_EQ(bo->order.price, 50);
    EXPECT_EQ(bo->order.quantity, 100);

    // Duplicate add must not fire an additional event
    EXPECT_EQ(listener().updates.size(), 1);
}

TEST_F(OrderBookTest, AddMultipleOrdersSameLevelFIFO) {
    book.add({1, 50, 100});
    book.add({2, 50, 200});
    book.add({3, 50, 300});

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.count, 3);
    EXPECT_EQ(level.total_quantity, 600);

    EXPECT_EQ(level.head->order.order_id, 1);
    EXPECT_EQ(level.head->next->order.order_id, 2);
    EXPECT_EQ(level.tail->order.order_id, 3);

    EXPECT_EQ(listener().updates.size(), 3);
    for (const auto &u: listener().updates)
        EXPECT_EQ(u.side, Side::e_BUY);
}

TEST_F(OrderBookTest, AddOrdersAcrossMultipleLevels) {
    book.add({1, 50, 100});
    book.add({2, 55, 200});
    book.add({3, 60, 300, Side::e_SELL});

    EXPECT_EQ(cbook().level_at(Side::e_BUY, 50).count, 1);
    EXPECT_EQ(cbook().level_at(Side::e_BUY, 55).count, 1);
    EXPECT_EQ(cbook().level_at(Side::e_SELL, 60).count, 1);

    ASSERT_EQ(listener().updates.size(), 3);
    EXPECT_EQ(listener().updates[0].side, Side::e_BUY);
    EXPECT_EQ(listener().updates[1].side, Side::e_BUY);
    EXPECT_EQ(listener().updates[2].side, Side::e_SELL);
}

TEST_F(OrderBookTest, FindExistingOrder) {
    book.add({42, 50, 300});

    const auto *bo = cbook().find_order(42);
    ASSERT_NE(bo, nullptr);
    EXPECT_EQ(bo->order.order_id, 42);
    EXPECT_EQ(bo->order.quantity, 300);
}

TEST_F(OrderBookTest, FindNonExistingOrderReturnsNull) {
    EXPECT_EQ(cbook().find_order(999), nullptr);
}


TEST_F(OrderBookTest, CancelPartialReducesQuantity) {
    book.add({1, 50, 100});
    ASSERT_TRUE(book.cancel(1, 40));

    const auto *bo = cbook().find_order(1);
    ASSERT_NE(bo, nullptr);
    EXPECT_EQ(bo->order.quantity, 60);

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.total_quantity, 60);
    EXPECT_EQ(level.count, 1);

    EXPECT_EQ(listener().updates.size(), 2);
    EXPECT_EQ(listener().updates[1].side, Side::e_BUY);
    EXPECT_TRUE(listener().trades.empty());
}

TEST_F(OrderBookTest, CancelFullRemovesOrder) {
    book.add({1, 50, 100});
    ASSERT_TRUE(book.cancel(1, 100));

    EXPECT_EQ(cbook().find_order(1), nullptr);

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.count, 0);
    EXPECT_EQ(level.total_quantity, 0);
    EXPECT_EQ(level.head, nullptr);
    EXPECT_EQ(level.tail, nullptr);

    EXPECT_EQ(listener().updates.size(), 2);
    EXPECT_TRUE(listener().trades.empty());
}

TEST_F(OrderBookTest, CancelExceedingQuantityRemovesOrder) {
    book.add({1, 50, 100});
    ASSERT_TRUE(book.cancel(1, 999));
    EXPECT_EQ(cbook().find_order(1), nullptr);
}

TEST_F(OrderBookTest, CancelNonExistingOrderReturnsFalse) {
    EXPECT_FALSE(book.cancel(999, 10));
    EXPECT_TRUE(listener().updates.empty());
}

TEST_F(OrderBookTest, RemoveOrder) {
    book.add({1, 50, 100});
    ASSERT_TRUE(book.remove(1));

    EXPECT_EQ(cbook().find_order(1), nullptr);

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.count, 0);
    EXPECT_EQ(level.head, nullptr);
    EXPECT_EQ(level.tail, nullptr);

    EXPECT_EQ(listener().updates.size(), 2);
    EXPECT_TRUE(listener().trades.empty());
}

TEST_F(OrderBookTest, RemoveNonExistingReturnsFalse) {
    EXPECT_FALSE(book.remove(999));
    EXPECT_TRUE(listener().updates.empty());
}

TEST_F(OrderBookTest, RemoveMiddleOfQueue) {
    book.add({1, 50, 100});
    book.add({2, 50, 200});
    book.add({3, 50, 300});

    ASSERT_TRUE(book.remove(2));

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.count, 2);
    EXPECT_EQ(level.total_quantity, 400);
    EXPECT_EQ(level.head->order.order_id, 1);
    EXPECT_EQ(level.tail->order.order_id, 3);
    EXPECT_EQ(level.head->next, level.tail);
    EXPECT_EQ(level.tail->prev, level.head);

    EXPECT_EQ(listener().updates.size(), 4);
}

TEST_F(OrderBookTest, RemoveHeadOfQueue) {
    book.add({1, 50, 100});
    book.add({2, 50, 200});

    ASSERT_TRUE(book.remove(1));

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.head->order.order_id, 2);
    EXPECT_EQ(level.head, level.tail);
    EXPECT_EQ(level.head->prev, nullptr);
}

TEST_F(OrderBookTest, RemoveTailOfQueue) {
    book.add({1, 50, 100});
    book.add({2, 50, 200});

    ASSERT_TRUE(book.remove(2));

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.tail->order.order_id, 1);
    EXPECT_EQ(level.head, level.tail);
    EXPECT_EQ(level.tail->next, nullptr);
}

TEST_F(OrderBookTest, ExecutePartialFill) {
    book.add({1, 60, 500, Side::e_SELL});
    ASSERT_TRUE(book.execute(1, 200));

    const auto *bo = cbook().find_order(1);
    ASSERT_NE(bo, nullptr);
    EXPECT_EQ(bo->order.quantity, 300);

    ASSERT_EQ(listener().trades.size(), 1);
    EXPECT_EQ(listener().trades[0].price, 60);
    EXPECT_EQ(listener().trades[0].qty, 200);
    EXPECT_EQ(listener().trades[0].aggressor, Side::e_BUY);
}

TEST_F(OrderBookTest, ExecuteFullFillRemovesOrder) {
    book.add({1, 60, 500, Side::e_SELL});
    ASSERT_TRUE(book.execute(1, 500));
    EXPECT_EQ(cbook().find_order(1), nullptr);

    ASSERT_EQ(listener().trades.size(), 1);
    EXPECT_EQ(listener().trades[0].qty, 500);
}

TEST_F(OrderBookTest, ReplaceChangesIdAndPrice) {
    book.add({1, 50, 100});
    ASSERT_TRUE(book.replace(1, {2, 55, 200}));

    EXPECT_EQ(cbook().find_order(1), nullptr);

    const auto *bo = cbook().find_order(2);
    ASSERT_NE(bo, nullptr);
    EXPECT_EQ(bo->order.price, 55);
    EXPECT_EQ(bo->order.quantity, 200);

    EXPECT_EQ(cbook().level_at(Side::e_BUY, 50).count, 0);
    EXPECT_EQ(cbook().level_at(Side::e_BUY, 55).count, 1);

    EXPECT_EQ(listener().updates.size(), 2);
    EXPECT_EQ(listener().updates[1].side, Side::e_BUY);
    EXPECT_TRUE(listener().trades.empty());
}

TEST_F(OrderBookTest, ReplaceSameIdUpdatesInPlace) {
    book.add({1, 50, 100});
    ASSERT_TRUE(book.replace(1, {1, 55, 200}));

    const auto *bo = cbook().find_order(1);
    ASSERT_NE(bo, nullptr);
    EXPECT_EQ(bo->order.price, 55);
    EXPECT_EQ(bo->order.quantity, 200);

    EXPECT_EQ(listener().updates.size(), 2);
}

TEST_F(OrderBookTest, ReplaceNonExistingReturnsFalse) {
    EXPECT_FALSE(book.replace(999, {2, 55, 200}));
    EXPECT_TRUE(listener().updates.empty());
}

TEST_F(OrderBookTest, ReplaceLosesTimePriority) {
    book.add({1, 50, 100});
    book.add({2, 50, 200});

    ASSERT_TRUE(book.replace(1, {3, 50, 150}));

    const auto &level = cbook().level_at(Side::e_BUY, 50);
    EXPECT_EQ(level.head->order.order_id, 2);
    EXPECT_EQ(level.tail->order.order_id, 3);

    EXPECT_EQ(listener().updates.size(), 3);
}

TEST_F(OrderBookTest, BestBidAcrossOrders) {
    book.add({1, 50, 1});
    book.add({2, 60, 2});
    book.add({3, 55, 3});

    EXPECT_EQ(cbook().level_at(Side::e_BUY, 50).total_quantity, 1);
    EXPECT_EQ(cbook().level_at(Side::e_BUY, 60).total_quantity, 2);
    EXPECT_EQ(cbook().level_at(Side::e_BUY, 55).total_quantity, 3);

    EXPECT_EQ(*book.best_bid(), 60);

    EXPECT_EQ(listener().updates.size(), 3);
}

TEST_F(OrderBookTest, BestAskAcrossOrders) {
    book.add({1, 70, 1, Side::e_SELL});
    book.add({2, 60, 2, Side::e_SELL});
    book.add({3, 65, 3, Side::e_SELL});

    EXPECT_EQ(cbook().level_at(Side::e_SELL, 70).total_quantity, 1);
    EXPECT_EQ(cbook().level_at(Side::e_SELL, 60).total_quantity, 2);
    EXPECT_EQ(cbook().level_at(Side::e_SELL, 65).total_quantity, 3);

    EXPECT_EQ(*book.best_ask(), 60);

    EXPECT_EQ(listener().updates.size(), 3);
    for (const auto &u: listener().updates)
        EXPECT_EQ(u.side, Side::e_SELL);
}

TEST_F(OrderBookTest, BestBidUpdatesOnRemoval) {
    book.add({1, 60, 100});
    book.add({2, 50, 100});

    EXPECT_EQ(book.best_bid(), 60);

    book.remove(1);
    EXPECT_EQ(book.best_bid(), 50);

    EXPECT_EQ(listener().updates.size(), 3);
}

TEST_F(OrderBookTest, BestAskUpdatesOnRemoval) {
    book.add({1, 60, 100, Side::e_SELL});
    book.add({2, 70, 100, Side::e_SELL});

    EXPECT_EQ(book.best_ask(), 60);

    book.remove(1);
    EXPECT_EQ(book.best_ask(), 70);

    EXPECT_EQ(listener().updates.size(), 3);
}

TEST_F(OrderBookTest, AvailableBetweenBids) {
    book.add({1, 90, 100, Side::e_BUY});
    book.add({2, 90, 100, Side::e_BUY});
    book.add({3, 70, 100, Side::e_BUY});
    book.add({4, 70, 100, Side::e_BUY});
    book.add({5, 10, 100, Side::e_BUY});

    // Best bid = 90, walking DOWN toward limit
    EXPECT_EQ(book.available_between(Side::e_BUY, 90, 100), 100);  // Only 100 of 200 @ P90
    EXPECT_EQ(book.available_between(Side::e_BUY, 90, 1000), 200); // All 200 @ P90
    EXPECT_EQ(book.available_between(Side::e_BUY, 70, 1000), 400); // 200 @ P90 + 200 @ P70
    EXPECT_EQ(book.available_between(Side::e_BUY, 70, 300), 300);  // Capped by out_of
    EXPECT_EQ(book.available_between(Side::e_BUY, 10, 499), 499);  // 499 of 500 total
    EXPECT_EQ(book.available_between(Side::e_BUY, 0, 1000), 500);  // All volume, limit below book
}

TEST_F(OrderBookTest, AsksUpToRange) {
    book.add({1, 10, 100, Side::e_SELL});
    book.add({2, 50, 200, Side::e_SELL});
    book.add({3, 90, 300, Side::e_SELL});

    // Walk from best ask (10) up to price 60
    Quantity total = 0;
    for (const auto &level: book.asks_up_to(60)) {
        total += level.total_quantity;
    }
    EXPECT_EQ(total, 300); // 100 @ P10 + 200 @ P50

    // Walk up to price 90 — should include everything
    total = 0;
    for (const auto &level: book.asks_up_to(90)) {
        total += level.total_quantity;
    }
    EXPECT_EQ(total, 600);

    // Walk up to price 5 — below best ask, should be empty
    total = 0;
    for (const auto &level: book.asks_up_to(5)) {
        total += level.total_quantity;
    }
    EXPECT_EQ(total, 0);
}

TEST_F(OrderBookTest, BidsDownToRange) {
    book.add({1, 90, 300, Side::e_BUY});
    book.add({2, 50, 200, Side::e_BUY});
    book.add({3, 10, 100, Side::e_BUY});

    // Walk from best bid (90) down to price 40
    Quantity total = 0;
    for (const auto &level: book.bids_down_to(40)) {
        total += level.total_quantity;
    }
    EXPECT_EQ(total, 500); // 300 @ P90 + 200 @ P50

    // Walk down to price 10 — everything
    total = 0;
    for (const auto &level: book.bids_down_to(10)) {
        total += level.total_quantity;
    }
    EXPECT_EQ(total, 600);

    // Walk down to price 95 — above best bid, should be empty
    total = 0;
    for (const auto &level: book.bids_down_to(95)) {
        total += level.total_quantity;
    }
    EXPECT_EQ(total, 0);
}
} // namespace trading::orderbook::test
