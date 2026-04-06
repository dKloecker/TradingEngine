//
// Created by Dominic Kloecker on 06/04/2026.
//
#ifndef TRADING_ORDER_BOOK_LISTENER_H
#define TRADING_ORDER_BOOK_LISTENER_H

#include "order.h"

namespace dsl::order {
template<typename DerivedT>
class OrderBookListener {
public:
    // Trade Occurred. Price / Quantity and Aggressor
    void on_trade(TickPrice price, Quantity qty, Side aggressor) {
        static_cast<DerivedT *>(this)->on_trade_impl(price, qty, aggressor);
    }

    // Book depth changed on this side
    void on_book_update(Side side) {
        static_cast<DerivedT *>(this)->on_book_update_impl(side);
    }
};

/**
 * Void Listener - Ignoring All Events
 */
class VoidListener : public OrderBookListener<VoidListener> {
public:
    static void on_trade_impl([[maybe_unused]] TickPrice price,
                              [[maybe_unused]] Quantity  qty,
                              [[maybe_unused]] Side      aggressor) {
        return;
    }

    static void on_book_update_impl([[maybe_unused]] Side side) {
        return;
    }
};
}


#endif //TRADING_ORDER_BOOK_LISTENER_H
