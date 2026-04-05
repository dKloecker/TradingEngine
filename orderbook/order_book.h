#ifndef TRADING_ORDER_BOOK_H
#define TRADING_ORDER_BOOK_H

#include <array>
#include <cstddef>
#include <unordered_map>

#include "fixed_size_pool_resource.h"
#include "order.h"

namespace dsl::order {
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

/**
 * @brief Basic Limit Order Book
 *
 * @tparam MinTick Minimum representable price in ticks
 * @tparam MaxTick Maximum representable price in ticks
 */
template<TickPrice MinTick, TickPrice MaxTick>
class OrderBook {
    static_assert(MinTick < MaxTick);
    static constexpr size_t NUM_CHUNKS  = 4096;
    static constexpr size_t NUM_LEVELS  = MaxTick - MinTick;
    static constexpr size_t BUCKET_SIZE = NUM_LEVELS * 10;

    /** @brief Pool allocator for @c OrderNode instances. */
    fixed_size_pool_resource<sizeof(OrderNode), NUM_CHUNKS> pool_;

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
    void destroy_order(OrderNode *bo) {
        bo->~OrderNode();
        pool_.deallocate(bo, sizeof(OrderNode), alignof(OrderNode));
    }

    /** Unlinks node from its price level. Does not erase from map or free memory. */
    void unlink_from_level(OrderNode *bo);

    /** Appends node to the tail of its price level. */
    void append_to_level(OrderNode *bo);

    /**
     *
     * Reduces order quantity by a given amount, or removes Order entirely if amount >= remaining
     * @return @c True if order was found, @c false otherwise
     */
    bool reduce(OrderId order_id, Quantity amount);

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

template<TickPrice MinTick, TickPrice MaxTick>
void OrderBook<MinTick, MaxTick>::unlink_from_level(OrderNode *bo) {
    PriceLevel &level = level_at(bo->order.side, bo->order.price);

    if (bo->prev) bo->prev->next = bo->next;
    if (bo->next) bo->next->prev = bo->prev;
    if (bo == level.head) level.head = bo->next;
    if (bo == level.tail) level.tail = bo->prev;

    --level.count;
    level.total_quantity -= bo->order.quantity;
}

template<TickPrice MinTick, TickPrice MaxTick>
void OrderBook<MinTick, MaxTick>::append_to_level(OrderNode *bo) {
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


template<TickPrice MinTick, TickPrice MaxTick>
bool OrderBook<MinTick, MaxTick>::reduce(const OrderId order_id, const Quantity amount) {
    OrderNode *bo = find_order(order_id);
    if (!bo) return false;

    // If order will be filled / we can remove it
    if (amount >= bo->order.quantity) {
        unlink_from_level(bo);
        order_map_.erase(order_id);
        destroy_order(bo);
        return true;
    }
    // If not filled, update amount and level aggregate
    PriceLevel &level    = level_at(bo->order.side, bo->order.price);
    level.total_quantity -= amount;
    bo->order.quantity   -= amount;
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick>
bool OrderBook<MinTick, MaxTick>::add(const Order &order) {
    if (order_map_.contains(order.order_id)) return false;
    // Create the order and append it to it's level
    OrderNode *bo = make_order(OrderNode{.order = order});
    append_to_level(bo);
    order_map_[order.order_id] = bo;
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick>
bool OrderBook<MinTick, MaxTick>::cancel(const OrderId order_id, const Quantity amount) {
    return reduce(order_id, amount);
}

template<TickPrice MinTick, TickPrice MaxTick>
bool OrderBook<MinTick, MaxTick>::remove(const OrderId order_id) {
    OrderNode *bo = find_order(order_id);
    if (!bo) return false;

    unlink_from_level(bo);
    order_map_.erase(order_id);
    destroy_order(bo);
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick>
bool OrderBook<MinTick, MaxTick>::execute(const OrderId order_id, const Quantity fill) {
    return reduce(order_id, fill);
}

template<TickPrice MinTick, TickPrice MaxTick>
bool OrderBook<MinTick, MaxTick>::replace(const OrderId old_order_id, const Order &new_order) {
    OrderNode *bo = find_order(old_order_id);
    if (!bo) return false;
    // Unlink Order from old Price Level
    unlink_from_level(bo);
    // If order has a different ID, delete old record from map
    if (old_order_id != new_order.order_id) order_map_.erase(old_order_id);
    // Re-use pointer for new order
    bo->order = new_order;
    append_to_level(bo);
    order_map_[new_order.order_id] = bo;
    return true;
}

template<TickPrice MinTick, TickPrice MaxTick>
TickPrice OrderBook<MinTick, MaxTick>::best_bid() const {
    for (TickPrice tp = MaxTick - 1; tp >= MinTick; --tp) {
        if (bids_[to_index(tp)].count > 0) return tp;
        if (tp == MinTick) break; // prevent underflow
    }
    return MinTick;
}

template<TickPrice MinTick, TickPrice MaxTick>
TickPrice OrderBook<MinTick, MaxTick>::best_ask() const {
    for (TickPrice tp = MinTick; tp < MaxTick; ++tp) {
        if (asks_[to_index(tp)].count > 0) return tp;
    }
    return MaxTick;
}
} // namespace dsl::order

#endif // TRADING_ORDER_BOOK_H
