#include <iostream>

#include <map>
#include <set>
#include <list>
#include <cmath>
#include <ctime>
#include <deque>
#include <queue>
#include <stack>
#include <limits>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <variant>
#include <optional>
#include <tuple>
#include <format>

enum class OrderType
{
	GoodTillCancel,
	FillandKill
};

enum class Side
{
	Buy,
	Sell
};

// aliases
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

struct LevelInfo
{
	Price price_;
	Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos
{
public:
	OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
		: bids_{ bids }
		, asks_{ asks }
	{ }

	// apis
	const LevelInfos& GetBids() const { return bids_; }
	const LevelInfos& GetAsks() const { return asks_; }

private:
	LevelInfos bids_;
	LevelInfos asks_;
};

class Order
{
public:
	Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
		: orderType_{ orderType }
		, orderId_{ orderId }
		, side_{ side }
		, price_{ price }
		, initialQuantity_{ quantity }
		, remainingQuantity_{ quantity }
	{ }

	// apis
	OrderId GetOrderId() const { return orderId_; }
	Side GetSide() const { return side_; }
	Price GetPrice() const { return price_; }
	OrderType GetOrderType() const { return orderType_; }
	Quantity GetInitialQuantity() const { return initialQuantity_; }
	Quantity GetRemainingQuantity() const { return remainingQuantity_; }
	Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
	bool IsFilled() const { return GetRemainingQuantity() == 0; }
	void Fill(Quantity quantity)
	{
		if (quantity > GetRemainingQuantity())
			throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));

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

class OrderModify
{
public:
	OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
		: orderId_{ orderId }
		, price_{ price }
		, side_{ side }
		, quantity_{ quantity }
	{ }

	// blah blah
	OrderId GetOrderId() const { return orderId_; }
	Side GetSide() const { return side_; }
	Price GetPrice() const { return price_; }
	Quantity GetQuantity() const { return quantity_; }

	OrderPointer ToOrderPointer(OrderType type) const
	{
		return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
	}

private:
	OrderId orderId_;
	Price price_;
	Side side_;
	Quantity quantity_;

};

struct TradeInfo
{
	OrderId orderId_;
	Price price_;
	Quantity quantity_;
};

class Trade
{
public:
	Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
		: bidTrade_{ bidTrade }
		, askTrade_{ askTrade }
	{ }

	const TradeInfo& GetBidTrade() const { return bidTrade_; }
	const TradeInfo& GetAskTrade() const { return askTrade_; }

private:
	TradeInfo bidTrade_;
	TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;

// now the interesting part!

// bids are in descending order from best bid
// asks are in ascending order from best ask
class Orderbook
{
private:
	struct OrderEntry
	{
		OrderPointer order_{ nullptr };
		OrderPointers::iterator location_;
	};

	// each map is keyed by price
	// value is a list of order pointers
	std::map<Price, OrderPointers, std::greater<Price>> bids_;
	std::map<Price, OrderPointers, std::less<Price>> asks_;

	std::unordered_map<OrderId, OrderEntry> orders_;

	bool CanMatch(Side side, Price price) const
	{
		// if on the buy side (check asks)
		if (side == Side::Buy)
		{
			if (asks_.empty())
				return false;
			
			// get best ask price (key)
			const auto& [bestAsk, _] = *asks_.begin();
			return price >= bestAsk;
		}
		// if on the sell side (check bids)
		else
		{
			if (bids_.empty())
				return false;

			// get best bid price (key)
			const auto& [bestBid, _] = *bids_.begin();
			return price <= bestBid;
		}
	}

	Trades MatchOrders()
	{
		Trades trades;
		trades.reserve(orders_.size());

		while (true)
		{
			// either empty, nothing can be matched
			if (bids_.empty() || asks_.empty())
				break;

			// get best bid and best ask
			auto& [bidPrice, bids] = *bids_.begin();
			auto& [askPrice, asks] = *asks_.begin();

			// no matches can be made if best bid < best ask
			if (bidPrice < askPrice)
				break;
			
			// until either is empty:
			while (bids.size() && asks.size())
			{
				// get refs to first bid and first ask
				auto& bid = bids.front();
				auto& ask = asks.front();

				// limiting quantity will be minimum between the two
				Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

				// fill appropriate quantity
				bid->Fill(quantity);
				ask->Fill(quantity);

				// check if either bid or ask is fully satisfied
				if (bid->IsFilled())
				{
					bids.pop_front();
					orders_.erase(bid->GetOrderId());
				}

				if (ask->IsFilled())
				{
					asks.pop_front();
					orders_.erase(ask->GetOrderId());
				}

				if (bids.empty())
					bids_.erase(bidPrice);

				if (asks.empty())
					asks_.erase(askPrice);

				// keep track of orders involved with the trade
				trades.push_back(Trade{
					TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity},
					TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity}
					});
			}
		}

		// unmatched FOK orders should be deleted
		if (!bids_.empty())
		{
			auto& [_, bids] = *bids_.begin();
			auto& order = bids.front();
			if (order->GetOrderType() == OrderType::FillandKill)
				CancelOrder(order->GetOrderId());
		}
		if (!asks_.empty())
		{
			auto& [_, asks] = *asks_.begin();
			auto& order = asks.front();
			if (order->GetOrderType() == OrderType::FillandKill)
				CancelOrder(order->GetOrderId());
		}

		return trades;
	}

public:
	Trades AddOrder(OrderPointer order)
	{
		// no duplicate order ID's
		if (orders_.contains(order->GetOrderId()))
			return { };

		// unmatchable FOK orders must be deleted
		if (order->GetOrderType() == OrderType::FillandKill && !CanMatch(order->GetSide(), order->GetPrice()))
			return { };

		OrderPointers::iterator iterator;

		// otherwise, add the order where it belongs
		if (order->GetSide() == Side::Buy)
		{
			// get ref to list of bid orders at price level specified by the order
			auto& orders = bids_[order->GetPrice()];

			orders.push_back(order);

			// get iterator pointing to newly added order (get final index)
			iterator = std::next(orders.begin(), orders.size() - 1);
		}
		else
		{
			// same as above
			auto& orders = asks_[order->GetPrice()];
			orders.push_back(order);
			iterator = std::next(orders.begin(), orders.size() - 1);
		}

		// insert to orders map
		orders_.insert({ order->GetOrderId(), OrderEntry{ order, iterator } });
		//                    key                       val
		
		return MatchOrders();
	}

	void CancelOrder(OrderId orderId)
	{
		if (!orders_.contains(orderId))
			return;
		
		// get refs to order-to-remove
		const auto& [order, iterator] = orders_.at(orderId);
		orders_.erase(orderId);

		if (order->GetSide() == Side::Sell)
		{
			auto price = order->GetPrice();

			// get ref to list of ask orders at the price level of the canceled order
			auto& orders = asks_.at(price);

			// remove canceled order from list
			orders.erase(iterator);

			// are there any orders left at this price level?
			if (orders.empty())
				asks_.erase(price);
		}
		else
		{
			// same as abbove for buy side
			auto price = order->GetPrice();
			auto& orders = bids_.at(price);
			orders.erase(iterator);
			if (orders.empty())
				bids_.erase(price);
		}
	}

	// match a modified order 
	Trades MatchOrder(OrderModify order)
	{
		if (!orders_.contains(order.GetOrderId()))
			return { };

		// retrieve existing order
		const auto& [existingOrder, _] = orders_.at(order.GetOrderId());

		// cancel existing order
		CancelOrder(order.GetOrderId());

		// add modified order to order book, will attempt to match on return 
		return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
	}

	std::size_t Size() const { return orders_.size(); }
	
	OrderbookLevelInfos GetOrderInfos() const
	{
		LevelInfos bidInfos, askInfos;
		bidInfos.reserve(orders_.size());
		askInfos.reserve(orders_.size());

		auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
			{
				// count up total remaining quantity of all orders at a given price level
				return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
					[](Quantity runningSum, const OrderPointer& order)
					{ return runningSum + order->GetRemainingQuantity(); }) };
			};

		// iterate over each entry in bids_
		for (const auto& [price, orders] : bids_)

			// for each price level and corresponding bid orders,get level infos
			// push into bidInfos
			bidInfos.push_back(CreateLevelInfos(price, orders));

		for (const auto& [price, orders] : asks_)
			askInfos.push_back(CreateLevelInfos(price, orders));

		return OrderbookLevelInfos{ bidInfos, askInfos };
	}
	
};

int main()
{
	Orderbook orderbook;
	const OrderId orderId = 1;
	orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
	std::cout << orderbook.Size() << std::endl; // 1
	orderbook.CancelOrder(orderId);
	std::cout << orderbook.Size() << std::endl; // 0
	return 0;
}