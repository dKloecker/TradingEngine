//
// Created by Dominic Kloecker on 06/04/2026.
//

#ifndef TRADING_ITCH_MESSAGES_H
#define TRADING_ITCH_MESSAGES_H

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace networking::itch {
// TODO: Update this to be handled more cleanly. For now used just for testing so it can do the job.
// Implement proper parser (maybe based on https://kevingivens.github.io/parsing-itch-messages-in-c)
// I.e. Split the different message types etc

enum class MessageType : char {
    e_UNKNOWN              = ' ', // Place Holder for any Unknown message
    e_ADD                  = 'A', // New Order accepted and added to book
    e_ADD_WITH_ATTRIBUTION = 'F', // New Order with Market Participant Identifier
    e_EXECUTED_WITH_PRICE  = 'C', // Order Executed with Price Message
    e_EXECUTED             = 'E', // Order fully or partially executed
    e_CANCELED             = 'X', // Order was partially or fully cancelled
    e_DELETED              = 'D', // Order was cancelled in its entirety and removed from book
    e_REPLACED             = 'U', // Existing Order replaced by new Order
    e_TRADE                = 'P', // Non-cross trade e.g. match against non-displayed order
    e_CROSS_TRADE          = 'Q', // Cross event
    e_BROKEN_TRADE         = 'B', // Previously executed trade has been cancelled
    e_SECONDS_MESSAGE      = 'T', // Nano Seconds since midnight
};

enum class Side : char {
    e_BUY  = 'B',
    e_SELL = 'S'
};

using StockLocate          = uint16_t;
using TrackingNumber       = uint16_t;
using TimeStamp            = uint64_t; // 6 bytes on wire (48-bit), nanoseconds since midnight
using OrderReferenceNumber = uint64_t;
using Quantity             = uint32_t;
using Price                = int32_t; // Fixed-point, 4 implied decimal places (e.g. 100000 = $10.0000)

static constexpr size_t STOCK_SYMBOL_LEN = 8;
using Stock                              = std::array<char, STOCK_SYMBOL_LEN>; // Right-padded with spaces

static constexpr size_t ITCH_ADD_ORDER_SIZE = 36;
using ItchMessage                           = std::array<std::byte, ITCH_ADD_ORDER_SIZE>;


/**
 * @brief NASDAQ ITCH 5.0 Add Order Message (Type 'A')
 * https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf
 * @code
 * Offset  Length  Field                    Type
 * 0       1       Message Type             (Alphanumeric)
 * 1       2       Stock Locate             (Integer, big-endian)
 * 3       2       Tracking Number          (Integer, big-endian)
 * 5       6       Timestamp                (Integer, big-endian, nanoseconds since midnight)
 * 11      8       Order Reference Number   (Integer, big-endian)
 * 19      1       Buy/Sell Indicator       (Alphanumeric, 'B'/'S')
 * 20      4       Shares                   (Integer, big-endian)
 * 24      8       Stock                    (Alphanumeric, right-padded spaces)
 * 32      4       Price                    (Price(4), big-endian, 4 implied decimals)
 * @endcode
 */
struct OrderMessage {
    MessageType          message_type           = MessageType::e_UNKNOWN;
    StockLocate          stock_locate           = 0;
    TrackingNumber       tracking_number        = 0;
    TimeStamp            time_stamp             = 0;
    OrderReferenceNumber order_reference_number = 0;
    Side                 side                   = Side::e_BUY;
    Quantity             quantity               = 0;
    Stock                stock                  = {};
    Price                price                  = 0;
};

namespace details {
// ITCH Messages are encoded in BIG Endian Byte Format so some utilities for reading / writing
template<std::integral T, size_t N = sizeof(T)>
    requires (N <= sizeof(T))
constexpr T read_big_endian(const std::byte *src) {
    using U  = std::make_unsigned_t<T>;
    U result = 0;
    for (size_t i = 0; i < N; ++i) {
        result = static_cast<U>(
            (result << 8) | static_cast<U>(std::to_integer<uint8_t>(src[i])));
    }
    return static_cast<T>(result);
}

template<std::integral T, size_t N = sizeof(T)>
    requires (N <= sizeof(T))
constexpr void write_big_endian(std::byte *dst, T value) {
    using U   = std::make_unsigned_t<T>;
    auto uval = static_cast<U>(value);
    for (size_t i = N; i > 0; --i) {
        dst[i - 1] = static_cast<std::byte>(uval & 0xFF);
        uval       >>= 8;
    }
}
}

/**
 * @brief Parses a 36-byte ITCH 5.0 Add Order message into an OrderMessage.
 *
 * All multibyte numeric fields are big-endian as per the ITCH protocol.
 * Timestamp is 6 bytes on the wire, stored in a uint64_t.
 * Can be evaluated at compile time for static test data.
 */
constexpr OrderMessage parse_itch(const ItchMessage &raw) {
    const std::byte *p = raw.data();

    Stock stock{};
    for (size_t i = 0; i < STOCK_SYMBOL_LEN; ++i)
        stock[i] = static_cast<char>(std::to_integer<uint8_t>(p[24 + i]));

    return OrderMessage{
        .message_type           = static_cast<MessageType>(std::to_integer<char>(p[0])),
        .stock_locate           = details::read_big_endian<StockLocate>(p + 1),
        .tracking_number        = details::read_big_endian<TrackingNumber>(p + 3),
        .time_stamp             = details::read_big_endian<TimeStamp, 6>(p + 5),
        .order_reference_number = details::read_big_endian<OrderReferenceNumber>(p + 11),
        .side                   = static_cast<Side>(std::to_integer<char>(p[19])),
        .quantity               = details::read_big_endian<Quantity>(p + 20),
        .stock                  = stock,
        .price                  = details::read_big_endian<Price>(p + 32),
    };
}

/**
 * @brief Serializes an OrderMessage into a 36-byte ITCH 5.0 Add Order message.
 *
 * Inverse of @c parse_itch. Intended for constructing test data.
 * Can be evaluated at compile time.
 */
constexpr ItchMessage serialize_itch(const OrderMessage &msg) {
    ItchMessage raw{};
    std::byte * p = raw.data();

    p[0] = static_cast<std::byte>(static_cast<char>(msg.message_type));
    details::write_big_endian(p + 1, msg.stock_locate);
    details::write_big_endian(p + 3, msg.tracking_number);
    details::write_big_endian<TimeStamp, 6>(p + 5, msg.time_stamp);
    details::write_big_endian(p + 11, msg.order_reference_number);
    p[19] = static_cast<std::byte>(static_cast<char>(msg.side));
    details::write_big_endian(p + 20, msg.quantity);
    for (size_t i = 0; i < STOCK_SYMBOL_LEN; ++i)
        p[24 + i] = static_cast<std::byte>(msg.stock[i]);
    details::write_big_endian(p + 32, msg.price);

    return raw;
}
}

#endif //TRADING_ITCH_MESSAGES_H
