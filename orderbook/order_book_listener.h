//
// Created by Dominic Kloecker on 06/04/2026.
//
#ifndef TRADING_ORDER_BOOK_LISTENER_H
#define TRADING_ORDER_BOOK_LISTENER_H

#include <type_traits>

#include "orderbook/order.h"

namespace trading::orderbook {
template<typename DerivedT>
class BaseOrderBookCallBackHandler {
public:
    /** a new order has been rested in the book. */
    void on_add(const Order &resting) {
        static_cast<DerivedT *>(this)->on_add_impl(resting);
    }

    /**
     *
     * @param resting resting order being cancelled
     * @param cancelled_qty quantity being cancelled
     */
    void on_cancel(const Order &resting, Quantity cancelled_qty) {
        static_cast<DerivedT *>(this)->on_cancel_impl(resting, cancelled_qty);
    }

    /**
     * @param matched_id
     * @param matched_side
     * @param matched_price
     * @param filled
     * @param matched_remaining
     */
    void on_execute(OrderId   matched_id,
                    Side      matched_side,
                    TickPrice matched_price,
                    Quantity  filled,
                    Quantity  matched_remaining) {
        static_cast<DerivedT *>(this)->on_execute_impl(matched_id,
                                                       matched_side,
                                                       matched_price,
                                                       filled,
                                                       matched_remaining);
    }

    /**
     * @param old_order old order to remove (i.e. cancel)
     * @param replacement new order to replace the old order
     */
    void on_replace(const Order &old_order, const Order &replacement) {
        static_cast<DerivedT *>(this)->on_replace_impl(old_order, replacement);
    }

    /** depth changed on this side signal */
    void on_book_update(Side side) {
        static_cast<DerivedT *>(this)->on_book_update_impl(side);
    }
};

/** No-op handler */
class VoidHandler : public BaseOrderBookCallBackHandler<VoidHandler> {
public:
    static void on_add_impl(const Order &) {}
    static void on_cancel_impl(const Order &, Quantity) {}
    static void on_execute_impl(OrderId, Side, TickPrice, Quantity, Quantity) {}
    static void on_replace_impl(const Order &, const Order &) {}
    static void on_book_update_impl(Side) {}
};

template<typename T>
concept CallBackHandler = std::is_base_of_v<BaseOrderBookCallBackHandler<T>, T>;
} // namespace trading::orderbook

#endif //TRADING_ORDER_BOOK_LISTENER_H
