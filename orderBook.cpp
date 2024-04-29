#include <iostream>
#include <map>
#include <list>
#include <algorithm>
#include <string>
#include <sstream>
#include <format>
#include <cmath>
#include <limits>
#include <functional>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <numeric>
#include <stack>
#include <deque>
#include <ctime>
#include <memory>
#include <optional>
#include <variant>

enum class OrderType {
    GoodTillCancel,
    FillOrKill,
};

enum class Side {
    Buy,
    Sell,
};

using Price = std::int32_t;
using Quantity = std::int32_t;
using OrderId = std::uint64_t;

struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos {
    public:
        OrderbookLevelInfos(const LevelInfos& bids, LevelInfos& asks) : 
            bids_ {bids},
            asks_ {asks} {}

        const LevelInfos& GetBids() const {return bids_;}
        const LevelInfos& GetAsks() const {return asks_;}
    
    private:
        LevelInfos bids_;
        LevelInfos asks_;
};

class Order {
    public:
        Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity) :
            orderId_ {orderId},
            side_ {side},
            price_ {price},
            orderType_ {orderType},
            initialQuantity_ {quantity},
            remainingQuantity_ {quantity} {}

        OrderId GetOrderId() const {return orderId_;}
        Side GetSide() const {return side_;}
        Price GetPrice() const {return price_;}
        OrderType GetOrderType() const {return orderType_;}
        Quantity GetInitialQuantity() const {return initialQuantity_;}
        Quantity GetRemainingQuantity() const {return remainingQuantity_;}
        Quantity getFilledQuantity() const {return GetInitialQuantity() - GetRemainingQuantity();}
        
        bool isFilled() const {return GetRemainingQuantity() == 0;}

        void Fill(Quantity quantity) {
            if (quantity > GetRemainingQuantity()) {
                throw std::logic_error("Cannot fill more than remaining quantity");
            }
            remainingQuantity_ -= quantity;
        }


    private:
        OrderType orderType_;
        OrderId orderId_;
        Side side_;
        Price price_;
        Quantity initialQuantity_;
        Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify {
    public:
        OrderModify(OrderId orderId, Side side, Price price, Quantity quantity) :
            orderId_ {orderId},
            price_ {price},
            side_ {side},
            quantity_ {quantity} {}

        OrderId GetOrderId() const {return orderId_;}
        Quantity GetQuantity() const {return quantity_;}
        Price GetPrice() const {return price_;}
        Side GetSide() const {return side_;}

        OrderPointer toOrderPointer(OrderType orderType) const {
            return std::make_shared<Order>(orderType, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
        }

    private:
        OrderId orderId_;
        Price price_;
        Side side_;
        Quantity quantity_;
};

struct TradeInfo {
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade {
    public:
        Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade) :
            bidTrade_ {bidTrade},
            askTrade_ {askTrade} {}

        const TradeInfo& GetBidTrade() const {return bidTrade_;}
        const TradeInfo& GetAskTrade() const {return askTrade_;}

    private:
        TradeInfo bidTrade_;
        TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;

class Orderbook {
    private:
        struct OrderEntry {
            OrderPointer order_ { nullptr };
            OrderPointers::iterator location_;
        };

        std::map<Price, OrderPointers, std::greater<Price>> bids_;
        std::map<Price, OrderPointers, std::less<Price>> asks_;
        std::unordered_map<OrderId, OrderEntry> orders_;

        bool canMatch(Side side, Price price) const {
            if (side == Side::Buy) {
                if (asks_.empty()) {
                    return false;
                }

                const auto& [bestAsk, _] = *asks_.begin();
                return bestAsk <= price;
            } else {
                if (bids_.empty()) {
                    return false;
                }

                const auto& [bestBid, _] = *bids_.begin();
                return bestBid >= price;
            }
        }

        Trades MatchOrders() {
            Trades trades;
            trades.reserve(orders_.size());

            while (true) {
                if (bids_.empty() || asks_.empty()) {
                    break;
                }
                auto& [bidPrice, bids] = *bids_.begin();
                auto& [askPrice, asks] = *asks_.begin();

                if (bidPrice < askPrice) {
                    break;
                }

                while (bids.size() && asks.size()) {
                    auto bidOrder = bids.front();
                    auto askOrder = asks.front();

                    auto tradeQuantity = std::min(bidOrder->GetRemainingQuantity(), askOrder->GetRemainingQuantity());
                    auto tradePrice = bidPrice;

                    bidOrder->Fill(tradeQuantity);
                    askOrder->Fill(tradeQuantity);

                    
                    if (bidOrder->isFilled()) {
                        bids.pop_front();
                        orders_.erase(bidOrder->GetOrderId());
                    }

                    if (askOrder->isFilled()) {
                        asks.pop_front();
                        orders_.erase(askOrder->GetOrderId());
                    }

                    if (bids.empty()) {
                        bids_.erase(bidPrice);
                    }

                    if (asks.empty()) {
                        asks_.erase(askPrice);
                    }

                    trades.push_back(Trade {
                        TradeInfo {bidOrder->GetOrderId(), bidOrder->GetPrice(), tradeQuantity}, 
                        TradeInfo {askOrder->GetOrderId(), askOrder->GetPrice(), tradeQuantity}
                    });
                }
            }

            if (!bids_.empty()) {
                auto& [_, bids] = *bids_.begin();
                auto& order = bids.front();
                if (order->GetOrderType() == OrderType::FillOrKill) {
                    CancelOrder(order->GetOrderId());
                }
            }

            if (!asks_.empty()) {
                auto& [_, asks] = *asks_.begin();
                auto& order = asks.front();
                if (order->GetOrderType() == OrderType::FillOrKill) {
                    CancelOrder(order->GetOrderId());
                }
            }

            return trades;
        }

    public:

        Trades AddOrder(OrderPointer order) {
            /*if (!orders_.contains(orderId)) {
                throw std::logic_error("Order does not exist");
            }
            */

            if (order->GetOrderType() == OrderType::FillOrKill) {
                if (!canMatch(order->GetSide(), order->GetPrice())) {
                    return {};
                }
            }

            OrderPointers::iterator iterator;

            if (order->GetSide() == Side::Buy) {
                auto& orders = bids_[order->GetPrice()];
                orders.push_back(order);
                iterator = std::next(orders.begin(), orders.size() - 1);
            } else {
                auto& orders = asks_[order->GetPrice()];
                orders.push_back(order);
                iterator = std::next(orders.begin(), orders.size() - 1);    
            }

            orders_.insert({order->GetOrderId(), OrderEntry {order, iterator}});

            return MatchOrders();
        }

        void CancelOrder(OrderId orderId) {
            /*if (!orders_.contains(orderId)) {
                throw std::logic_error("Order does not exist");
            }
            */
            const auto& [order, iterator] = orders_.at(orderId);
            
            if (order->GetSide() == Side::Sell) {
                auto price = order->GetPrice();
                auto& orders = asks_.at(price);
                orders.erase(iterator);
                if (orders.empty()) {
                    asks_.erase(price);
                }
            } else {
                auto price = order->GetPrice();
                auto& orders = bids_.at(price);
                orders.erase(iterator);
                if (orders.empty()) {
                    bids_.erase(price);
                }
            }

            orders_.erase(orderId);
        }

        Trades MatchOrder(OrderModify order) {
            /*if (!orders_.contains(orderId)) {
                throw std::logic_error("Order does not exist");
            }
            */

            const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
            CancelOrder(order.GetOrderId());
            return AddOrder(order.toOrderPointer(existingOrder->GetOrderType()));
        }

        std::size_t Size() const {
            return orders_.size();
        }

        OrderbookLevelInfos GetOrderInfos() const {
            LevelInfos bidInfos, askInfos;
            bidInfos.reserve(bids_.size());
            askInfos.reserve(asks_.size());

            auto CreateLevelInfos = [](Price price, const OrderPointers& orders) {
                return LevelInfo{price, std::accumulate(orders.begin(), orders.end(), (Quantity)0, [](Quantity sum, const OrderPointer& order) {
                    return sum + order->GetRemainingQuantity();
                })};
            };

            for (const auto& [price, orders] : bids_) {
                bidInfos.push_back(CreateLevelInfos(price, orders));
            }

            for (const auto& [price, orders] : asks_) {
                askInfos.push_back(CreateLevelInfos(price, orders));
            }
            return OrderbookLevelInfos{bidInfos, askInfos};
        }

};

int main() {
    Orderbook orderbook;
    const OrderId orderId = 1;
    const OrderId orderId2 = 2;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId2, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << std::endl;
    orderbook.CancelOrder(orderId2);
    std::cout << orderbook.Size() << std::endl;
    return 0;
};