//
// Created by Dominic Kloecker on 06/04/2026.
//
#ifndef TRADING_MATCHING_ENGINE_H
#define TRADING_MATCHING_ENGINE_H

#include <functional>
#include <thread>
#include <variant>

#include "core/queue/spsc_queue.h"
#include "core/util/overloaded_visitor.h"
#include "orderbook/order.h"
#include "orderbook/order_book.h"
#include "orderbook/order_book_listener.h"

namespace trading::orderbook {
// TODO: Update matching engine with DEBUG ASSERTIONS for CONTACTS
// TODO: Temporary Types, for now. will likely need to be replaced later on.
struct AddMessage {
    Order order;
};

struct CancelMessage {
    OrderId  order_id;
    Quantity qty; // qty=0 means full cancel
};

struct ReplaceMessage {
    OrderId old_id{};
    Order   new_order;
};

using OrderMsg = std::variant<std::monostate, AddMessage, CancelMessage, ReplaceMessage>;


enum class ExecType : uint8_t {
    e_NEW,          // Order accepted, resting in book (no match)
    e_PARTIAL_FILL, // Partial fill against a resting order
    e_FILL,         // Order fully filled and removed
    e_CANCEL,       // Order cancelled
    e_REJECT,       // Order rejected (e.g. FOK that can't fully fill)
};

struct ExecutionReport {
    OrderId   order_id;        // The incoming order
    OrderId   match_order_id;  // The resting order it matched against (0 if N/A)
    TickPrice price;           // Execution price
    Quantity  fill_quantity;   // How much was filled in this report
    Quantity  leaves_quantity; // How much remains on the order
    ExecType  exec_type;       // Type of Execution Event
    Side      side;            // Side of the incoming order
};

/**
 * Listener  that generates Execution Reports from Order Book Callbacks
 * Keeps temporary track or aggressor order when order is being added
 */
class ReportGenerator : public BaseOrderBookCallBackHandler<ReportGenerator> {
public:
    using ReportQueue = dsl::spsc_queue<ExecutionReport, 1024>;

    explicit ReportGenerator(ReportQueue &queue) : queue_(queue) {}

    void set_aggressor_(Order &o) {
        aggressor_     = &o;
        filled_so_far_ = 0;
    }

    void clear_aggressor() {
        aggressor_     = nullptr;
        filled_so_far_ = 0;
    }

    void emit_reject(const Order &o) const {
        push({
            .order_id        = o.order_id,
            .match_order_id  = 0,
            .price           = o.price,
            .fill_quantity   = 0,
            .leaves_quantity = o.quantity,
            .exec_type       = ExecType::e_REJECT,
            .side            = o.side,
        });
    }

    void emit_leftover_cancel(const Order &o, const Quantity remaining) const {
        push({
            .order_id        = o.order_id,
            .match_order_id  = 0,
            .price           = o.price,
            .fill_quantity   = o.quantity - remaining,
            .leaves_quantity = remaining,
            .exec_type       = ExecType::e_CANCEL,
            .side            = o.side,
        });
    }

    /** Report the addition of a new resting order into the order book */
    void on_add_impl(const Order &resting) const {
        const Quantity fill = (aggressor_ && aggressor_->order_id == resting.order_id) ? filled_so_far_ : 0;
        push({
            .order_id        = resting.order_id,
            .match_order_id  = 0,
            .price           = resting.price,
            .fill_quantity   = fill,
            .leaves_quantity = resting.quantity,
            .exec_type       = ExecType::e_NEW,
            .side            = resting.side,
        });
    }

    void on_execute_impl(const OrderId matched_id,
                         const Side,
                         const TickPrice matched_price,
                         const Quantity  filled,
                         const Quantity) {
        filled_so_far_             += filled;
        const Quantity aggr_leaves =
                (aggressor_->quantity > filled_so_far_)
                    ? aggressor_->quantity - filled_so_far_
                    : 0;
        push({
            .order_id        = aggressor_->order_id,
            .match_order_id  = matched_id,
            .price           = matched_price,
            .fill_quantity   = filled,
            .leaves_quantity = aggr_leaves,
            .exec_type       = (aggr_leaves == 0) ? ExecType::e_FILL : ExecType::e_PARTIAL_FILL,
            .side            = aggressor_->side,
        });
    }

    void on_cancel_impl(const Order &resting, const Quantity cancelled_qty) const {
        const Quantity remaining = resting.quantity - cancelled_qty;
        push({
            .order_id        = resting.order_id,
            .match_order_id  = 0,
            .price           = resting.price,
            .fill_quantity   = 0,
            .leaves_quantity = remaining,
            .exec_type       = ExecType::e_CANCEL,
            .side            = resting.side,
        });
    }

    void on_replace_impl(const Order &old_order, const Order &new_order) const {
        push({
            .order_id        = old_order.order_id,
            .match_order_id  = 0,
            .price           = old_order.price,
            .fill_quantity   = 0,
            .leaves_quantity = old_order.quantity,
            .exec_type       = ExecType::e_CANCEL,
            .side            = old_order.side,
        });
        push({
            .order_id        = new_order.order_id,
            .match_order_id  = 0,
            .price           = new_order.price,
            .fill_quantity   = 0,
            .leaves_quantity = new_order.quantity,
            .exec_type       = ExecType::e_NEW,
            .side            = new_order.side,
        });
    }

    static void on_book_update_impl(Side) {}

private:
    ReportQueue &queue_;
    Order *      aggressor_     = nullptr;
    Quantity     filled_so_far_ = 0;

    void push(const ExecutionReport &r) const { while (!queue_.push(r)) {} }
};

template<TickPrice MinTick, TickPrice MaxTick>
class MatchingEngine {
public:
    using ReportSink = std::function<void(ExecutionReport &&)>;

    explicit MatchingEngine(ReportSink sink)
        : report_sink_(std::move(sink)) {
        message_processor_ = std::thread([this] { process_messages(); });
        report_processor_  = std::thread([this] { process_reports(); });
    }

    ~MatchingEngine() {
        if (sc_.stop_possible()) sc_.request_stop();
        if (message_processor_.joinable()) message_processor_.join();
        if (report_processor_.joinable()) report_processor_.join();
    }

    MatchingEngine(const MatchingEngine &) = delete;

    MatchingEngine &operator=(const MatchingEngine &) = delete;

    bool submit(const OrderMsg &msg) {
        if (sc_.stop_requested()) [[unlikely]] return false;
        return input_.push(msg);
    }

private:
    ReportSink                                   report_sink_;
    ReportGenerator::ReportQueue                 report_queue{};
    ReportGenerator                              report_cb_{report_queue};
    OrderBook<MinTick, MaxTick, ReportGenerator> order_book_{&report_cb_};

    dsl::spsc_queue<OrderMsg, 1024> input_{};

    std::stop_source sc_{};
    std::thread      message_processor_{};
    std::thread      report_processor_{};

    const auto &cbook() const { return order_book_; }

    void process_messages() {
        while (!sc_.stop_requested() || !input_.empty()) {
            OrderMsg order_msg = std::monostate{};
            if (!input_.pop(order_msg)) {
                std::this_thread::yield();
                continue;
            }
            std::visit(
                dsl::overload{
                    [](std::monostate) { /* Should Not Happen */ },
                    [this](AddMessage &&msg) { process_add(std::move(msg)); },
                    [this](CancelMessage &&msg) { process_cancel(std::move(msg)); },
                    [this](ReplaceMessage &&msg) { process_replace(std::move(msg)); },
                },
                std::move(order_msg));
        }
    }

    void process_reports() {
        ExecutionReport r;
        while (!sc_.stop_requested() || !report_queue.empty()) {
            if (!report_queue.pop(r)) {
                std::this_thread::yield();
                continue;
            }
            report_sink_(std::move(r));
        }
    }

    /**
     * Absorb resting orders at a single price level until the level is
     * exhausted or the aggressor's remaining quantity reaches zero.
     */
    void absorb_up_to(const PriceLevel &level, Quantity &remaining) {
        const OrderNode *tail = level.head;
        while (tail && remaining > 0) {
            const OrderId    matched_id = tail->order.order_id;
            const OrderNode *next       = tail->next;
            const Quantity   filled     = order_book_.execute(matched_id, remaining);
            remaining                   -= filled;
            tail                        = next;
        }
    }

    void process_add(AddMessage message) {
        Order &    order    = message.order;
        const Side opposite = (order.side == Side::e_BUY) ? Side::e_SELL : Side::e_BUY;

        switch (order.order_type) {
            case OrderType::e_UNKNOWN: return; /* Should not happen */
            case OrderType::e_FOK: {
                if (order_book_.available_between(opposite, order.price, order.quantity) < order.quantity) {
                    report_cb_.emit_reject(order);
                    return;
                }
                break;
            }
            case OrderType::e_MARKET: {
                order.price = MaxTick;
                break;
            }
            case OrderType::e_LIMIT: [[fallthrough]];
            case OrderType::e_IOC: break;
        }

        report_cb_.set_aggressor_(order);

        Quantity remaining = order.quantity;
        auto     fill      = [&](const auto &range) {
            for (const PriceLevel &lvl: range) {
                if (remaining == 0) break;
                absorb_up_to(lvl, remaining);
            }
        };
        switch (order.side) {
            case Side::e_BUY: fill(order_book_.asks_up_to(order.price));
                break;
            case Side::e_SELL: fill(order_book_.bids_down_to(order.price));
                break;
        }

        if (remaining > 0) {
            switch (order.order_type) {
                case OrderType::e_UNKNOWN: [[unlikely]] break;
                case OrderType::e_FOK: [[unlikely]] break; // pre-checked above
                case OrderType::e_IOC: [[fallthrough]];
                case OrderType::e_MARKET: report_cb_.emit_leftover_cancel(order, remaining);
                    break;
                case OrderType::e_LIMIT: {
                    // Limit Orders can have remaining orders rested in ob
                    order.quantity = remaining;
                    order_book_.add(order);
                    break;
                }
            }
        }

        report_cb_.clear_aggressor();
    }

    void process_cancel(CancelMessage msg) {
        order_book_.cancel(msg.order_id, msg.qty);
    }

    void process_replace(ReplaceMessage msg) {
        order_book_.replace(msg.old_id, msg.new_order);
    }
};
} // namespace trading::orderbook

#endif //TRADING_MATCHING_ENGINE_H
