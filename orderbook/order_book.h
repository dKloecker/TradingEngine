#ifndef TRADING_ORDER_BOOK_H
#define TRADING_ORDER_BOOK_H

#include <array>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <__ranges/views.h>

#include "core/memory/fixed_size_pool_resource.h"
#include "orderbook/order.h"
#include "orderbook/order_book_listener.h"

namespace trading::orderbook {
/**
 * Intrusive linked list node wrapping an @c Order for placement
 * within a PriceLevel queue.
 */
struct OrderNode {
    Order      order;
    OrderNode *prev = nullptr;
    OrderNode *next = nullptr;
};

/**
 * FIFO queue of orders at a single price.
 * Aggregate statistics are maintained incrementally.
 */
struct PriceLevel {
    OrderNode *head           = nullptr; // Oldest (highest priority) order
    OrderNode *tail           = nullptr; // Youngest (lowest priority) order
    uint32_t   count          = 0;       // Number of live orders at this level
    Quantity   total_quantity = 0;       // Sum of quantities across all orders
};


// TODO: Mechanism to handle Prices Outside of Normal And Expected Range?
// TODO: Benchmarks for order book
/**
 * @brief Basic Limit Order Book
 *
 * @tparam MinTick    Minimum representable price in ticks
 * @tparam MaxTick    Maximum representable price in ticks
 * @tparam Handler   Order Book Listener to handle updates in OB State. Ownership is external.
 */
template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler Handler>
class OrderBook {
public:
    explicit OrderBook(Handler *handler) : handler_(handler) {};

    static_assert(MinTick < MaxTick, "Maximum Tick Amount must be Larger than Minimum Tick");

    static constexpr size_t NUM_CHUNKS  = 4096;
    static constexpr size_t NUM_LEVELS  = MaxTick - MinTick + 1;
    static constexpr size_t BUCKET_SIZE = NUM_LEVELS * 4;


    Handler *handler_;

    /** @brief Pool allocator for @c OrderNode instances. */
    dsl::fixed_size_pool_resource<sizeof(OrderNode), NUM_CHUNKS> pool_;

    /**
     * Bids and asks stored in Sorted Arrays Representing Price Levels
     * Each level is directly accessible by its index expressed in the TickPrice
     * @code
     * MinTick = 0, MaxTick = 2000 ($)
     * Price  -> Index  : Orders at Price
     * 0.000$ -> 0      : [...] (Bids at 0.000$)
     * 0.001$ -> 1      : [...] (Bids at 0.001$)
     *       ...
     * 1.042$ -> 1042   : [...] (Bids at 1.042$)
     *       ...
     * 2.000$ -> 2000   : [...] (Bids at 2.000$)
     * @endcode
     */
    std::array<PriceLevel, NUM_LEVELS> bids_{};
    std::array<PriceLevel, NUM_LEVELS> asks_{};
    // TODO: Maybe replace with PMR vector so we are not bound in-between two prices?
    // That seems like it would otherwise be an issue


    /**
     * @brief Maps order IDs to their @c OrderNode
     * @todo Replace with a custom hash table backed by variable size pool allocator.
     */
    std::unordered_map<OrderId, OrderNode *> order_map_{BUCKET_SIZE};

    /**
     * @brief Converts a tick price to an array index.
     * @param tick_price Price expressed in ticks.
     * @return Index into the bids/asks arrays.
     * @warning Undefined behaviour if @c tick_price is outside [@c MinTick, @c MaxTick).
     */
    static constexpr size_t to_index(const TickPrice tick_price) {
        return static_cast<size_t>(tick_price - MinTick);
    }

    PriceLevel &level_at(const Side side, const TickPrice tick) {
        return (side == Side::e_BUY) ? bids_[to_index(tick)] : asks_[to_index(tick)];
    }

    OrderNode *find_order(const OrderId order_id) {
        const auto it = order_map_.find(order_id);
        return (it != order_map_.end()) ? it->second : nullptr;
    }

    /**
     * @brief Allocates and constructs an @c OrderNode from the pool
     * @return Pointer to the newly constructed node
     * @throws std::bad_alloc if allocation fails
     */
    template<typename... Args>
    [[nodiscard]] OrderNode *make_order(Args &&... args) {
        void *memory = pool_.allocate(sizeof(OrderNode), alignof(OrderNode));
        return new(memory) OrderNode{std::forward<Args>(args)...};
    }

    /**
     * @brief Destroys a @c OrderNode and returns memory to pool
     * @param book_order Pointer of Order to be de-allocated
     */
    void destroy_order(OrderNode *&book_order) {
        book_order->~OrderNode();
        pool_.deallocate(book_order, sizeof(OrderNode), alignof(OrderNode));
        book_order = nullptr;
    }

    /** Unlinks a node from its price level. Does not erase from map or free memory. */
    void unlink_from_level(OrderNode *book_order);

    /** Appends a node to the tail of its price level. */
    void append_to_level(OrderNode *book_order);

    /**
     * Reduces order quantity by a given amount, or removes Order entirely if amount >= remaining
     * @param book_order        Reference to @c OrderNode pointer. Will be set to @c nullptr when order removed
     * @param amount    Amount by which order will be reduced
     */
    std::pair<Quantity, Quantity> reduce(OrderNode *&book_order, Quantity amount);

public:
    /** Adds order to book. No-op if ID already exists. */
    bool add(const Order &order);

    /** Partial cancel. Removes if the quantity reaches zero. */
    Quantity cancel(OrderId order_id, Quantity amount);

    /** Removes order entirely. */
    bool remove(OrderId order_id);

    /** Fills order. Removes if fully filled. */
    Quantity execute(OrderId order_id, Quantity fill);

    /**
     * Removes an old order and adds a new one, re-using the old OrderNode's resources.
     * @warning no-op if old order not found
     * @param old_order_id old order to remove
     * @param new_order new order to add instead
     */
    bool replace(OrderId old_order_id, const Order &new_order);

    /** @return Reference to the listener instance. */
    [[nodiscard]] const Handler &handler() const { return *handler_; }
    [[nodiscard]] Handler &      handler() { return *handler_; }

    /** @return Best bid price, or MinTick if no bids */
    [[nodiscard]] std::optional<TickPrice> best_bid() const;

    /** @return Best ask price, or MaxTick if no asks. */
    [[nodiscard]] std::optional<TickPrice> best_ask() const;

    /** @retunr Difference between ask and bid */
    [[nodiscard]] std::optional<TickPrice> spread() const {
        const std::optional<TickPrice> ba = best_ask();
        const std::optional<TickPrice> bb = best_bid();
        // If either has no orders at all, there is no spread
        if (!ba.has_value() || !bb.has_value()) return std::nullopt;
        return *ba - *bb;
    }

    [[nodiscard]] const PriceLevel &level_at(const Side side, const TickPrice tick) const {
        return (side == Side::e_BUY) ? bids_[to_index(tick)] : asks_[to_index(tick)];
    }

    [[nodiscard]] const OrderNode *find_order(const OrderId order_id) const {
        const auto it = order_map_.find(order_id);
        return (it != order_map_.end()) ? it->second : nullptr;
    }

    // Find the available volume in Order Book for a given side and limit
    [[nodiscard]] Quantity available_between(Side side, TickPrice limit, Quantity out_of);

    /**
     * @brief Walk bid levels from the best bid downward to a price floor or limit.
     * @param limit  Lowest price to include
     * @return A reverse range of PriceLevel references, ordered highest to lowest, spanning [limit, best_bid].
     *         Empty if no limit > best_bid
     */
    auto bids_down_to(TickPrice limit);

    /**
     * @brief Walk ask levels from best ask upward to a price ceiling.
     * @param limit  Highest price to include
     * @return A forward range of PriceLevel references, ordered lowest to highest, spanning [best_ask, limit].
     *         Empty if no asks or limit < best_ask.
     */
    auto asks_up_to(TickPrice limit);
};

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
void OrderBook<MinTick, MaxTick, OBListener>::unlink_from_level(OrderNode *book_order) {
    auto &[head, tail, count, total_quantity] = level_at(book_order->order.side, book_order->order.price);

    if (book_order->prev) book_order->prev->next = book_order->next;
    if (book_order->next) book_order->next->prev = book_order->prev;
    if (book_order == head) head = book_order->next;
    if (book_order == tail) tail = book_order->prev;

    --count;
    total_quantity -= book_order->order.quantity;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
void OrderBook<MinTick, MaxTick, OBListener>::append_to_level(OrderNode *book_order) {
    auto &[head, tail, count, total_quantity] = level_at(book_order->order.side, book_order->order.price);

    book_order->prev = tail;
    book_order->next = nullptr;

    if (tail) {
        tail->next = book_order;
    } else {
        head = book_order;
    }
    tail = book_order;

    ++count;
    total_quantity += book_order->order.quantity;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
std::pair<Quantity, Quantity> OrderBook<MinTick, MaxTick, OBListener>::reduce(
    OrderNode *&   book_order,
    const Quantity amount) {
    if (!book_order) return {0, 0};
    // If order will be filled / we can remove it
    if (amount >= book_order->order.quantity) {
        const OrderId  id           = book_order->order.order_id;
        const Quantity order_amount = book_order->order.quantity;
        // Unlink will decrement relevant level
        unlink_from_level(book_order);
        order_map_.erase(id);
        destroy_order(book_order);
        return std::make_pair(order_amount, 0u);
    }
    // If not filled, update amount and level aggregate
    PriceLevel &level          = level_at(book_order->order.side, book_order->order.price);
    level.total_quantity       -= amount;
    book_order->order.quantity -= amount;
    return std::make_pair(amount, book_order->order.quantity);
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::add(const Order &order) {
    if (order_map_.contains(order.order_id)) return false;
    // Create the order and append it to it's level
    OrderNode *book_order = make_order(OrderNode{.order = order});
    append_to_level(book_order);
    order_map_[order.order_id] = book_order;
    handler().on_add(book_order->order);
    handler().on_book_update(book_order->order.side);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
Quantity OrderBook<MinTick, MaxTick, OBListener>::cancel(const OrderId order_id, const Quantity amount) {
    OrderNode *book_order = find_order(order_id);
    if (!book_order) return 0;
    // Snapshot before reduce, node may be destroyed.
    const Order snapshot               = book_order->order;
    const auto  [cancelled, remaining] = reduce(book_order, amount);
    handler().on_cancel(snapshot, cancelled);
    handler().on_book_update(snapshot.side);
    return cancelled;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::remove(const OrderId order_id) {
    OrderNode *book_order = find_order(order_id);
    if (!book_order) return false;
    const Order snapshot = book_order->order;
    reduce(book_order, snapshot.quantity); // Reduce by entire amount to remove
    handler().on_cancel(snapshot, snapshot.quantity);
    handler().on_book_update(snapshot.side);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
Quantity OrderBook<MinTick, MaxTick, OBListener>::execute(const OrderId order_id, const Quantity fill) {
    OrderNode *book_order = find_order(order_id);
    if (!book_order) return 0;
    // Snapshot before reduce, node may be destroyed when fully filled.
    const OrderId   matched_id          = book_order->order.order_id;
    const TickPrice matched_price       = book_order->order.price;
    const Side      matched_side        = book_order->order.side;
    const auto      [filled, remaining] = reduce(book_order, fill);
    handler().on_execute(matched_id, matched_side, matched_price, filled, remaining);
    handler().on_book_update(matched_side);
    return filled;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::replace(const OrderId old_order_id, const Order &new_order) {
    OrderNode *book_order = find_order(old_order_id);
    if (!book_order) return false;
    const Order old_snapshot = book_order->order;
    const bool  bids_updated = old_snapshot.side == Side::e_BUY || new_order.side == Side::e_BUY;
    const bool  asks_updated = old_snapshot.side == Side::e_SELL || new_order.side == Side::e_SELL;
    // Unlink Order from old Price Level
    unlink_from_level(book_order);
    // If order has a different ID, delete old record from map
    if (old_order_id != new_order.order_id) order_map_.erase(old_order_id);
    // Re-use pointer for new order
    book_order->order = new_order;
    append_to_level(book_order);
    order_map_[new_order.order_id] = book_order;
    handler().on_replace(old_snapshot, new_order);
    handler().on_book_update(old_snapshot.side);
    if (old_snapshot.side != new_order.side) {
        handler().on_book_update(new_order.side);
    }
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
std::optional<TickPrice> OrderBook<MinTick, MaxTick, OBListener>::best_bid() const {
    for (TickPrice tp = MaxTick; tp >= MinTick; --tp) {
        if (bids_[to_index(tp)].count > 0) return tp;
        if (tp == MinTick) break; // Prevent underflow for unsigned
    }
    return std::nullopt;
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler OBListener>
std::optional<TickPrice> OrderBook<MinTick, MaxTick, OBListener>::best_ask() const {
    for (TickPrice tp = MinTick; tp <= MaxTick; ++tp) {
        if (asks_[to_index(tp)].count > 0) return tp;
    }
    return std::nullopt;
}


/**
 * @brief Returns the available total volume in order book available betweent the best price and the limit price
 *
 * @code
 *  Price | Qty
 *  101   | ██        <- above bid limit
 *  100   | ████      <- BUY limit-+
 *   99   | ██████                 |
 *   98   | ████████               |
 *   97   | ██████    <- best_ask -+
 *        |---spread---
 *   95   | ████      <- best_bid -+
 *   94   | ██████                 |
 *   93   | ████                   |
 *   92   | ███       <- Ask Limit-+
 *   91   | ██        <- below ask limit
 *   90   | █
 * @endcode
 *
 * @param side     Which side of the book to query (e_BUY = bids, e_SELL = asks)
 * @param limit    The furthest price level to include (inclusive)
 * @param out_of   Maximum quantity of interest — walk stops early once satisfied
 * @return         min(accumulated volume, out_of)
 */
template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler Handler>
Quantity OrderBook<MinTick, MaxTick, Handler>::available_between(
    const Side      side,
    const TickPrice limit,
    const Quantity  out_of) {
    Quantity available  = 0;
    auto     accumulate = [&](const auto &range) {
        for (const auto &level: range) {
            available += level.total_quantity;
            if (available >= out_of) break;
        }
    };

    switch (side) {
        case Side::e_BUY: accumulate(bids_down_to(limit));
            break;
        case Side::e_SELL: accumulate(asks_up_to(limit));
            break;
    }
    return std::min(available, out_of);
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler Handler>
auto OrderBook<MinTick, MaxTick, Handler>::bids_down_to(const TickPrice limit) {
    // Return reverse iterators, starting from the best bid, and walking down to the limit
    const auto bb = best_bid();
    if (!bb || limit > *bb)
        return std::ranges::subrange(bids_.crbegin(), bids_.crbegin());

    const TickPrice clamped_start = std::min(MaxTick, *bb);
    const TickPrice clamped_limit = std::max(limit, MinTick);
    const auto      rbegin        = bids_.crbegin() + (NUM_LEVELS - to_index(clamped_start) - 1);
    const auto      rend          = bids_.crbegin() + (NUM_LEVELS - to_index(clamped_limit));
    return std::ranges::subrange(rbegin, rend);
}

template<TickPrice MinTick, TickPrice MaxTick, CallBackHandler Handler>
auto OrderBook<MinTick, MaxTick, Handler>::asks_up_to(const TickPrice limit) {
    // Return Range from the best ask travelling upward towards the Limit / Max Price
    const auto ba = best_ask();
    if (!ba || *ba > limit)
        return std::ranges::subrange(asks_.cbegin(), asks_.cbegin());

    const auto clamped_limit = std::min(limit, MaxTick - 1);
    const auto begin         = asks_.cbegin() + to_index(*ba);
    const auto end           = asks_.cbegin() + to_index(clamped_limit) + 1;
    return std::ranges::subrange(begin, end);
}
} // namespace trading::orderbook

#endif // TRADING_ORDER_BOOK_H
