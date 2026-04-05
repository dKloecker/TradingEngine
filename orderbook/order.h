#ifndef TRADING_ORDER_H
#define TRADING_ORDER_H

#include <cstdint>

namespace dsl::order {
// Prices are expressed in Tick amounts (0.001) for now.
constexpr float TickSize = 0.001;

using OrderId   = uint64_t;
using TickPrice = int64_t; // Price in ticks (fixed-point integer)
using Quantity  = uint32_t;

enum class Side : char {
   e_BUY  = 'B',
   e_SELL = 'S'
};

struct Order {
   OrderId   order_id = 0;
   TickPrice price    = 0;
   Quantity  quantity = 0;
   Side      side     = Side::e_BUY;
};
} // namespace dsl::order

#endif // TRADING_ORDER_H
