#ifndef TRADING_ORDER_H
#define TRADING_ORDER_H

#include <cstdint>

namespace trading::orderbook {
// Prices are expressed in Tick amounts (0.001) for now.
constexpr float TickSize = 0.001;

using OrderId = uint64_t;

// Price in ticks (fixed-point integer). I.e. Price 2.34$ -> TickPrice 2340
using TickPrice = int64_t;
using Quantity  = uint32_t;

enum class Side : char {
   e_BUY  = 'B',
   e_SELL = 'S'
};

std::string_view to_string(Side side) {
   switch (side) {
      case Side::e_BUY: return "BUY";
      case Side::e_SELL: return "SELL";
   }
   return "UNKNOWN";
}

/**
 * Order Types
 */
enum class OrderType : uint8_t {
   e_UNKNOWN, //
   e_LIMIT,   // Fill what we can and place the rest in order book
   e_MARKET,  // Submit order at best Market Price (Partial Allowed)
   e_IOC,     // Immediate or kill (Fill What we can and cancel the rest)
   e_FOK      // Fill or Kill (If you cannot fill the whole thing then cancel)
};

struct Order {
   OrderId   order_id   = 0;
   TickPrice price      = 0;
   Quantity  quantity   = 0;
   Side      side       = Side::e_BUY;
   OrderType order_type = OrderType::e_UNKNOWN;
};
} // namespace trading::orderbook

#endif // TRADING_ORDER_H
