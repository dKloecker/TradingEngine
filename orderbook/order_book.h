#ifndef TRADING_ORDER_BOOK_H
#define TRADING_ORDER_BOOK_H

#include <array>
#include <type_traits>
#include <unordered_map>

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

/**
 * @brief Basic Limit Order Book
 *
 * @tparam MinTick    Minimum representable price in ticks
 * @tparam MaxTick    Maximum representable price in ticks
 * @tparam OBListener Order Book Listener to handle updates in OB State
 */
template<TickPrice MinTick, TickPrice MaxTick, class OBListener = VoidListener>
class OrderBook {
    static_assert(MinTick < MaxTick, "Maximum Tick Amount must be Larger than Minimum Tick");

    static_assert(std::is_base_of_v<OrderBookListener<OBListener>, OBListener>,
                  "OBListener must derive from OrderBookListener<OBListener> (CRTP)");

    static constexpr size_t NUM_CHUNKS  = 4096;
    static constexpr size_t NUM_LEVELS  = MaxTick - MinTick;
    static constexpr size_t BUCKET_SIZE = NUM_LEVELS * 4;

    OBListener listener_;

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
     * @param bo Pointer of Order to be de-allocated
     */
    void destroy_order(OrderNode *&bo) {
        bo->~OrderNode();
        pool_.deallocate(bo, sizeof(OrderNode), alignof(OrderNode));
        bo = nullptr;
    }

    /** Unlinks a node from its price level. Does not erase from map or free memory. */
    void unlink_from_level(OrderNode *bo);

    /** Appends a node to the tail of its price level. */
    void append_to_level(OrderNode *bo);

    /**
     *
     * Reduces order quantity by a given amount, or removes Order entirely if amount >= remaining
     * @param bo        Reference to @c OrderNode pointer. Will be set to @c nullptr when order removed
     * @param amount    Amount by which order will be reduced
     */
    void reduce(OrderNode *&bo, Quantity amount);

public:
    /** Adds order to book. No-op if ID already exists. */
    bool add(const Order &order);

    /** Partial cancel. Removes if quantity reaches zero. */
    bool cancel(OrderId order_id, Quantity amount);

    /** Removes order entirely. */
    bool remove(OrderId order_id);

    /** Fills order. Removes if fully filled. */
    bool execute(OrderId order_id, Quantity fill);

    /**
     * Removes an old order and adds a new one, re-using the old OrderNode's resources.
     * @warning no-op if old order not found
     * @param old_order_id old order to remove
     * @param new_order new order to add instead
     */
    bool replace(OrderId old_order_id, const Order &new_order);

    /** @return Reference to the listener instance. */
    [[nodiscard]] const OBListener &listener() const { return listener_; }

    /** @return Best bid price, or MinTick if no bids */
    TickPrice best_bid() const;

    /** @return Best ask price, or MaxTick if no asks. */
    TickPrice best_ask() const;

    [[nodiscard]] const PriceLevel &level_at(const Side side, const TickPrice tick) const {
        return (side == Side::e_BUY) ? bids_[to_index(tick)] : asks_[to_index(tick)];
    }

    [[nodiscard]] const OrderNode *find_order(const OrderId order_id) const {
        const auto it = order_map_.find(order_id);
        return (it != order_map_.end()) ? it->second : nullptr;
    }
};

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
void OrderBook<MinTick, MaxTick, OBListener>::unlink_from_level(OrderNode *bo) {
    PriceLevel &level = level_at(bo->order.side, bo->order.price);

    if (bo->prev) bo->prev->next = bo->next;
    if (bo->next) bo->next->prev = bo->prev;
    if (bo == level.head) level.head = bo->next;
    if (bo == level.tail) level.tail = bo->prev;

    --level.count;
    level.total_quantity -= bo->order.quantity;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
void OrderBook<MinTick, MaxTick, OBListener>::append_to_level(OrderNode *bo) {
    PriceLevel &level = level_at(bo->order.side, bo->order.price);

    bo->prev = level.tail;
    bo->next = nullptr;

    if (level.tail) {
        level.tail->next = bo;
    } else {
        level.head = bo;
    }
    level.tail = bo;

    ++level.count;
    level.total_quantity += bo->order.quantity;
}


template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
void OrderBook<MinTick, MaxTick, OBListener>::reduce(OrderNode *&bo, const Quantity amount) {
    if (!bo) return;
    // If order will be filled / we can remove it
    if (amount >= bo->order.quantity) {
        const OrderId id = bo->order.order_id;
        // Unlink will decrement relevant level
        unlink_from_level(bo);
        order_map_.erase(id);
        destroy_order(bo);
        return;
    }
    // If not filled, update amount and level aggregate
    PriceLevel &level    = level_at(bo->order.side, bo->order.price);
    level.total_quantity -= amount;
    bo->order.quantity   -= amount;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::add(const Order &order) {
    if (order_map_.contains(order.order_id)) return false;
    // Create the order and append it to it's level
    OrderNode *bo = make_order(OrderNode{.order = order});
    append_to_level(bo);
    order_map_[order.order_id] = bo;
    listener_.on_book_update(bo->order.side);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::cancel(const OrderId order_id, const Quantity amount) {
    OrderNode *bo = find_order(order_id);
    if (!bo) return false;
    const Side side = bo->order.side;
    reduce(bo, amount);
    listener_.on_book_update(side);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::remove(const OrderId order_id) {
    OrderNode *bo = find_order(order_id);
    if (!bo) return false;
    const Side side = bo->order.side;
    reduce(bo, bo->order.quantity); // Reduce by entire amount to remove
    listener_.on_book_update(side);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::execute(const OrderId order_id, const Quantity fill) {
    OrderNode *bo = find_order(order_id);
    if (!bo) return false;
    // Copy old order since it may be deleted
    const TickPrice price          = bo->order.price;
    const Side      aggressor_side = (bo->order.side == Side::e_BUY) ? Side::e_SELL : Side::e_BUY;
    reduce(bo, fill);
    listener_.on_trade(price, fill, aggressor_side);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
bool OrderBook<MinTick, MaxTick, OBListener>::replace(const OrderId old_order_id, const Order &new_order) {
    OrderNode *bo = find_order(old_order_id);
    if (!bo) return false;
    const bool bids_updated = bo->order.side == Side::e_BUY || new_order.side == Side::e_BUY;
    const bool asks_updated = bo->order.side == Side::e_SELL || new_order.side == Side::e_SELL;
    // Unlink Order from old Price Level
    unlink_from_level(bo);
    // If order has a different ID, delete old record from map
    if (old_order_id != new_order.order_id) order_map_.erase(old_order_id);
    // Re-use pointer for new order
    bo->order = new_order;
    append_to_level(bo);
    order_map_[new_order.order_id] = bo;

    if (bids_updated) listener_.on_book_update(Side::e_BUY);
    if (asks_updated) listener_.on_book_update(Side::e_SELL);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
TickPrice OrderBook<MinTick, MaxTick, OBListener>::best_bid() const {
    for (TickPrice tp = MaxTick - 1; tp >= MinTick; --tp) {
        if (bids_[to_index(tp)].count > 0) return tp;
        // prevent underflow if MinTick is 0 and we switch to a unsigned TickPrice
        if (tp == MinTick) break;
    }
    return MinTick;
}

template<TickPrice MinTick, TickPrice MaxTick, class OBListener>
TickPrice OrderBook<MinTick, MaxTick, OBListener>::best_ask() const {
    for (TickPrice tp = MinTick; tp < MaxTick; ++tp) {
        if (asks_[to_index(tp)].count > 0) return tp;
    }
    return MaxTick;
}
} // namespace trading::orderbook

#endif // TRADING_ORDER_BOOK_H
