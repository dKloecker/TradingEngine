//
// Created by Dominic Kloecker on 06/04/2026.
//
#include <benchmark/benchmark.h>

#include <random>
#include <string>
#include <vector>

#include "orderbook/order_book.h"

namespace trading::orderbook::benchmarks {
static constexpr TickPrice MIN_TICK = 0;
static constexpr TickPrice MAX_TICK = 10'000;
static constexpr TickPrice MID      = (MIN_TICK + MAX_TICK) / 2; // 5000


static VoidHandler void_handler{};
using TestBook = OrderBook<MIN_TICK, MAX_TICK, VoidHandler>;


static Order make_order(const OrderId id, const TickPrice price, const Quantity qty, const Side side) {
    return Order{
        .order_id   = id,
        .price      = price,
        .quantity   = qty,
        .side       = side,
        .order_type = OrderType::e_LIMIT
    };
}

static TickPrice price_for(const size_t i, const Side side, const int level_mode) {
    if (level_mode == 0) return MID;
    return (side == Side::e_BUY) ? MID + static_cast<TickPrice>(i) : MID - 1;
}

static const char *level_label(const int level_mode) {
    return level_mode == 0 ? "SameLevel" : "AcrossLevels";
}

static const char *fill_label(const int fill_mode) {
    return fill_mode == 0 ? "Partial" : "Full";
}


static void BM_AddOrder(benchmark::State &state) {
    const size_t num_ops    = static_cast<size_t>(state.range(0));
    const int    level_mode = static_cast<int>(state.range(1));
    state.SetLabel(level_label(level_mode));

    TestBook book{&void_handler};
    for (auto _: state) {
        for (size_t i = 0; i < num_ops; ++i) {
            const Side side = (i % 2 == 0) ? Side::e_BUY : Side::e_SELL;
            book.add(make_order(i, price_for(i, side, level_mode), 100, side));
        }
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) book.remove(i);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_ops));
}


BENCHMARK(BM_AddOrder)
    ->ArgsProduct({benchmark::CreateRange(2, MAX_TICK / 2, 8), {0, 1}})
    ->ArgNames({"num_ops", "level_mode"})
    ->MinWarmUpTime(1.0);


static void BM_RemoveOrder(benchmark::State &state) {
    const size_t num_ops    = static_cast<size_t>(state.range(0));
    const int    level_mode = static_cast<int>(state.range(1));
    state.SetLabel(level_label(level_mode));

    TestBook book{&void_handler};
    for (auto _: state) {
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) {
            const Side side = (i % 2 == 0) ? Side::e_BUY : Side::e_SELL;
            book.add(make_order(i, price_for(i, side, level_mode), 100, side));
        }
        state.ResumeTiming();
        for (size_t i = 0; i < num_ops; ++i) book.remove(i);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_ops));
}

BENCHMARK(BM_RemoveOrder)
    ->ArgsProduct({benchmark::CreateRange(2, MAX_TICK / 2, 8), {0, 1}})
    ->ArgNames({"num_ops", "level_mode"})
    ->MinWarmUpTime(1.0);


static void BM_CancelOrder(benchmark::State &state) {
    const size_t num_ops    = static_cast<size_t>(state.range(0));
    const int    level_mode = static_cast<int>(state.range(1));
    const int    fill_mode  = static_cast<int>(state.range(2));
    state.SetLabel(std::string(level_label(level_mode)) + "/" + fill_label(fill_mode));
    constexpr Quantity full_qty   = 100;
    const Quantity     cancel_qty = (fill_mode == 0) ? full_qty / 2 : full_qty;

    TestBook book{&void_handler};
    for (auto _: state) {
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) {
            const Side side = (i % 2 == 0) ? Side::e_BUY : Side::e_SELL;
            book.add(make_order(i, price_for(i, side, level_mode), full_qty, side));
        }
        state.ResumeTiming();
        for (size_t i = 0; i < num_ops; ++i) book.cancel(i, cancel_qty);
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) book.remove(i);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_ops));
}

BENCHMARK(BM_CancelOrder)
    ->ArgsProduct({benchmark::CreateRange(2, MAX_TICK / 2, 8), {0, 1}, {0, 1}})
    ->ArgNames({"num_ops", "level_mode", "fill_mode"})
    ->MinWarmUpTime(1.0);

static void BM_Execute(benchmark::State &state) {
    const size_t num_ops    = static_cast<size_t>(state.range(0));
    const int    level_mode = static_cast<int>(state.range(1));
    const int    fill_mode  = static_cast<int>(state.range(2));
    state.SetLabel(std::string(level_label(level_mode)) + "/" + fill_label(fill_mode));
    constexpr Quantity full_qty = 100;
    const Quantity     fill_qty = (fill_mode == 0) ? full_qty / 2 : full_qty;

    TestBook book{&void_handler};
    for (auto _: state) {
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) {
            const Side side = (i % 2 == 0) ? Side::e_BUY : Side::e_SELL;
            book.add(make_order(i, price_for(i, side, level_mode), full_qty, side));
        }
        state.ResumeTiming();
        for (size_t i = 0; i < num_ops; ++i) book.execute(i, fill_qty);
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) book.remove(i);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_ops));
}

BENCHMARK(BM_Execute)
    ->ArgsProduct({benchmark::CreateRange(2, MAX_TICK / 2, 8), {0}, {0, 1}})
    ->ArgNames({"num_ops", "level_mode", "fill_mode"})
    ->MinWarmUpTime(1.0);
BENCHMARK(BM_Execute)
    ->ArgsProduct({benchmark::CreateRange(2, MAX_TICK / 2, 8), {1}, {0, 1}})
    ->ArgNames({"num_ops", "level_mode", "fill_mode"})
    ->MinWarmUpTime(1.0);

static void BM_Replace(benchmark::State &state) {
    const size_t num_ops    = static_cast<size_t>(state.range(0));
    const int    level_mode = static_cast<int>(state.range(1));
    state.SetLabel(level_label(level_mode));
    constexpr Quantity qty = 100;

    TestBook book{&void_handler};
    for (auto _: state) {
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) {
            const Side side = (i % 2 == 0) ? Side::e_BUY : Side::e_SELL;
            book.add(make_order(i, price_for(i, side, level_mode), qty, side));
        }
        state.ResumeTiming();
        for (size_t i = 0; i < num_ops; ++i) {
            const Side      side      = (i % 2 == 0) ? Side::e_BUY : Side::e_SELL;
            const TickPrice src_price = price_for(i, side, level_mode);
            const TickPrice dst_price = (level_mode == 1)
                                            ? src_price + (side == Side::e_BUY ? -1 : 1)
                                            : src_price;
            book.replace(i, make_order(num_ops + i, dst_price, qty, side));
        }
        state.PauseTiming();
        for (size_t i = 0; i < num_ops; ++i) book.remove(num_ops + i);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(num_ops));
}

BENCHMARK(BM_Replace)
    ->ArgsProduct({benchmark::CreateRange(2, MAX_TICK, 8), {0}})
    ->ArgNames({"num_ops", "level_mode"})
    ->MinWarmUpTime(1.0);
BENCHMARK(BM_Replace)
    ->ArgsProduct({benchmark::CreateRange(2, MAX_TICK / 2, 8), {1}})
    ->ArgNames({"num_ops", "level_mode"})
    ->MinWarmUpTime(1.0);


// Performance of OB across a large number of varying events. Will keep track of events as
// a profile of weights of
// the different actions.
static constexpr size_t NUM_EVENTS = 100'000;
static constexpr size_t MAX_LIVE   = 5'000;

struct WorkloadProfile {
    std::string name;
    int         add_pct;
    int         remove_pct;
    int         cancel_pct;
    int         execute_pct;
    int         replace_pct;
};

static constexpr WorkloadProfile PROFILES[] = {
    {"MatchHeavy", 35, 20, 20, 15, 10},
    {"CancelHeavy", 25, 20, 35, 10, 10},
    {"MatchingHeavy", 25, 20, 10, 35, 10},
    {"ReplaceHeavy", 25, 15, 15, 15, 30},
};
static constexpr size_t NUM_PROFILES = std::size(PROFILES);

enum class OpType : uint8_t { Add, Remove, Cancel, Execute, Replace };

struct Event {
    OpType   op;
    OrderId  target_id;
    Order    new_order; // Add, Replace
    Quantity qty;       // Cancel, Execute
};

struct Workload {
    std::vector<Event>   events;
    std::vector<OrderId> final_live_ids;
};

// Generate Event Workload ahead of time for a given profile
static Workload generate_workload(const WorkloadProfile &profile) {
    std::vector<Event>   events;
    std::vector<OrderId> live_ids;
    events.reserve(NUM_EVENTS);
    live_ids.reserve(MAX_LIVE);

    // Fixed distribution of events
    std::mt19937_64          rng{42}; // NOLINT(cert-msc51-cpp)
    const std::array<int, 5> weights{
        profile.add_pct,
        profile.remove_pct,
        profile.cancel_pct,
        profile.execute_pct,
        profile.replace_pct
    };
    std::discrete_distribution<int> op_dist{weights.begin(), weights.end()};

    OrderId next_id = 0;

    auto make_live_order = [&](const OrderId id) {
        const Side      side  = (id % 2 == 0) ? Side::e_BUY : Side::e_SELL;
        const TickPrice price = (side == Side::e_BUY)
                                    ? MID - static_cast<TickPrice>(id % 500) - 1
                                    : MID + static_cast<TickPrice>(id % 500);
        return make_order(id, price, 10'000, side);
    };

    while (events.size() < NUM_EVENTS) {
        auto op = static_cast<OpType>(op_dist(rng));
        if (live_ids.empty() && op != OpType::Add) op = OpType::Add;
        if (live_ids.size() >= MAX_LIVE && op == OpType::Add) op = OpType::Remove;

        if (op == OpType::Add) {
            const OrderId id = next_id++;
            events.push_back({OpType::Add, id, make_live_order(id), 0});
            live_ids.push_back(id);
        } else {
            std::uniform_int_distribution<size_t> pick{0, live_ids.size() - 1};
            const size_t                          idx    = pick(rng);
            const OrderId                         target = live_ids[idx];

            switch (op) {
                case OpType::Remove: events.push_back({OpType::Remove, target, {}, 0});
                    live_ids[idx] = live_ids.back();
                    live_ids.pop_back();
                    break;
                case OpType::Cancel: events.push_back({OpType::Cancel, target, {}, 50});
                    break;
                case OpType::Execute: events.push_back({OpType::Execute, target, {}, 50});
                    break;
                case OpType::Replace: {
                    const OrderId new_id = next_id++;
                    events.push_back({OpType::Replace, target, make_live_order(new_id), 0});
                    live_ids[idx] = new_id;
                    break;
                }
                default: break;
            }
        }
    }

    return {std::move(events), std::move(live_ids)};
}

static void BM_MixedWorkload(benchmark::State &state) {
    const int              profile_idx = static_cast<int>(state.range(0));
    const WorkloadProfile &profile     = PROFILES[profile_idx];
    const Workload         workload    = generate_workload(profile);

    state.SetLabel(
        std::string(profile.name)
        + " A:" + std::to_string(profile.add_pct)
        + "% Rm:" + std::to_string(profile.remove_pct)
        + "% Ca:" + std::to_string(profile.cancel_pct)
        + "% Ex:" + std::to_string(profile.execute_pct)
        + "% Rp:" + std::to_string(profile.replace_pct) + "%");

    TestBook book{&void_handler};
    for (auto _: state) {
        for (const auto &e: workload.events) {
            switch (e.op) {
                case OpType::Add: book.add(e.new_order);
                    break;
                case OpType::Remove: book.remove(e.target_id);
                    break;
                case OpType::Cancel: book.cancel(e.target_id, e.qty);
                    break;
                case OpType::Execute: book.execute(e.target_id, e.qty);
                    break;
                case OpType::Replace: book.replace(e.target_id, e.new_order);
                    break;
            }
        }
        state.PauseTiming();
        // Kill any remaining orders before next run
        for (const OrderId id: workload.final_live_ids) book.remove(id);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(NUM_EVENTS));
}

BENCHMARK(BM_MixedWorkload)
    ->DenseRange(0, static_cast<int64_t>(NUM_PROFILES - 1), 1)
    ->ArgNames({"profile"})
    ->MinWarmUpTime(1.0);
} // namespace trading::orderbook::benchmarks
