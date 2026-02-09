/**
 * @file multi_symbol_engine.h
 * @brief Multi-symbol backtest engine with shared margin pool.
 *
 * Handles multiple symbols (e.g., XAUUSD and XAGUSD) trading on a single
 * account with shared equity and margin.
 */

#ifndef MULTI_SYMBOL_ENGINE_H
#define MULTI_SYMBOL_ENGINE_H

#include "tick_based_engine.h"
#include <map>
#include <string>
#include <vector>
#include <functional>

namespace backtest {

struct SymbolConfig {
    std::string symbol;
    double contract_size;
    double pip_size;
    double swap_long;
    double swap_short;
};

struct MultiTrade {
    std::string symbol;
    TradeDirection direction;
    double lot_size;
    double entry_price;
    double stop_loss;
    double take_profit;
    std::string timestamp;
    int id;

    MultiTrade() : direction(TradeDirection::BUY), lot_size(0), entry_price(0),
                   stop_loss(0), take_profit(0), id(0) {}
};

class MultiSymbolEngine {
public:
    struct Config {
        double initial_balance = 10000.0;
        double leverage = 500.0;
        double margin_stop_out = 20.0;
        bool verbose = false;
    };

    struct Results {
        double initial_balance;
        double final_balance;
        double final_equity;
        double max_drawdown_pct;
        double peak_equity;
        int total_trades;
        std::map<std::string, int> trades_per_symbol;
    };

    explicit MultiSymbolEngine(const Config& config)
        : config_(config),
          balance_(config.initial_balance),
          equity_(config.initial_balance),
          peak_equity_(config.initial_balance),
          max_drawdown_pct_(0.0),
          next_trade_id_(1),
          total_trades_(0)
    {}

    void AddSymbol(const SymbolConfig& symbol_config) {
        symbols_[symbol_config.symbol] = symbol_config;
        current_prices_[symbol_config.symbol] = {0.0, 0.0};  // bid, ask
    }

    void UpdatePrice(const std::string& symbol, double bid, double ask) {
        current_prices_[symbol] = {bid, ask};

        // Update equity based on all open positions
        UpdateEquity();

        // Check take profits and stop losses for this symbol
        CheckExits(symbol);

        // Check margin stop-out
        CheckMarginStopOut();
    }

    MultiTrade* OpenPosition(const std::string& symbol, TradeDirection direction,
                             double lot_size, double stop_loss = 0.0, double take_profit = 0.0,
                             const std::string& timestamp = "") {
        if (symbols_.find(symbol) == symbols_.end()) {
            return nullptr;
        }

        auto& prices = current_prices_[symbol];
        double entry_price = (direction == TradeDirection::BUY) ? prices.second : prices.first;  // ask for buy, bid for sell

        // Check margin
        double required_margin = CalculateMargin(symbol, lot_size, entry_price);
        double free_margin = equity_ - GetUsedMargin();

        if (required_margin > free_margin * 0.5) {  // Don't use more than 50% of free margin
            return nullptr;
        }

        MultiTrade trade;
        trade.symbol = symbol;
        trade.direction = direction;
        trade.lot_size = lot_size;
        trade.entry_price = entry_price;
        trade.stop_loss = stop_loss;
        trade.take_profit = take_profit;
        trade.timestamp = timestamp;
        trade.id = next_trade_id_++;

        positions_.push_back(trade);
        total_trades_++;
        trades_per_symbol_[symbol]++;

        return &positions_.back();
    }

    void ClosePosition(int trade_id) {
        for (auto it = positions_.begin(); it != positions_.end(); ++it) {
            if (it->id == trade_id) {
                // Calculate P&L
                auto& prices = current_prices_[it->symbol];
                double exit_price = (it->direction == TradeDirection::BUY) ? prices.first : prices.second;
                double pnl = CalculatePnL(*it, exit_price);

                balance_ += pnl;
                positions_.erase(it);
                UpdateEquity();
                return;
            }
        }
    }

    // Accessors
    double GetBalance() const { return balance_; }
    double GetEquity() const { return equity_; }
    double GetFreeMargin() const { return equity_ - GetUsedMargin(); }
    double GetMarginLevel() const {
        double used = GetUsedMargin();
        return (used > 0) ? (equity_ / used * 100.0) : 10000.0;
    }

    const std::vector<MultiTrade>& GetOpenPositions() const { return positions_; }

    std::vector<const MultiTrade*> GetPositionsForSymbol(const std::string& symbol) const {
        std::vector<const MultiTrade*> result;
        for (const auto& pos : positions_) {
            if (pos.symbol == symbol) {
                result.push_back(&pos);
            }
        }
        return result;
    }

    double GetUsedMargin() const {
        double total = 0.0;
        for (const auto& pos : positions_) {
            total += CalculateMargin(pos.symbol, pos.lot_size, pos.entry_price);
        }
        return total;
    }

    Results GetResults() const {
        Results r;
        r.initial_balance = config_.initial_balance;
        r.final_balance = balance_;
        r.final_equity = equity_;
        r.max_drawdown_pct = max_drawdown_pct_;
        r.peak_equity = peak_equity_;
        r.total_trades = total_trades_;
        r.trades_per_symbol = trades_per_symbol_;
        return r;
    }

    double GetPeakEquity() const { return peak_equity_; }
    double GetMaxDrawdownPct() const { return max_drawdown_pct_; }

private:
    Config config_;
    std::map<std::string, SymbolConfig> symbols_;
    std::map<std::string, std::pair<double, double>> current_prices_;  // symbol -> (bid, ask)
    std::vector<MultiTrade> positions_;

    double balance_;
    double equity_;
    double peak_equity_;
    double max_drawdown_pct_;
    int next_trade_id_;
    int total_trades_;
    std::map<std::string, int> trades_per_symbol_;

    double CalculateMargin(const std::string& symbol, double lots, double price) const {
        auto it = symbols_.find(symbol);
        if (it == symbols_.end()) return 0.0;
        return lots * it->second.contract_size * price / config_.leverage;
    }

    double CalculatePnL(const MultiTrade& trade, double current_price) const {
        auto it = symbols_.find(trade.symbol);
        if (it == symbols_.end()) return 0.0;

        double price_diff = (trade.direction == TradeDirection::BUY)
            ? (current_price - trade.entry_price)
            : (trade.entry_price - current_price);

        return trade.lot_size * it->second.contract_size * price_diff;
    }

    void UpdateEquity() {
        double unrealized_pnl = 0.0;
        for (const auto& pos : positions_) {
            auto& prices = current_prices_[pos.symbol];
            double current_price = (pos.direction == TradeDirection::BUY) ? prices.first : prices.second;
            unrealized_pnl += CalculatePnL(pos, current_price);
        }

        equity_ = balance_ + unrealized_pnl;

        if (equity_ > peak_equity_) {
            peak_equity_ = equity_;
        }

        double dd = (peak_equity_ > 0) ? (peak_equity_ - equity_) / peak_equity_ * 100.0 : 0.0;
        if (dd > max_drawdown_pct_) {
            max_drawdown_pct_ = dd;
        }
    }

    void CheckExits(const std::string& symbol) {
        auto& prices = current_prices_[symbol];

        std::vector<int> to_close;
        for (const auto& pos : positions_) {
            if (pos.symbol != symbol) continue;

            double current_price = (pos.direction == TradeDirection::BUY) ? prices.first : prices.second;

            // Check take profit
            if (pos.take_profit > 0) {
                if (pos.direction == TradeDirection::BUY && current_price >= pos.take_profit) {
                    to_close.push_back(pos.id);
                } else if (pos.direction == TradeDirection::SELL && current_price <= pos.take_profit) {
                    to_close.push_back(pos.id);
                }
            }

            // Check stop loss
            if (pos.stop_loss > 0) {
                if (pos.direction == TradeDirection::BUY && current_price <= pos.stop_loss) {
                    to_close.push_back(pos.id);
                } else if (pos.direction == TradeDirection::SELL && current_price >= pos.stop_loss) {
                    to_close.push_back(pos.id);
                }
            }
        }

        for (int id : to_close) {
            ClosePosition(id);
        }
    }

    void CheckMarginStopOut() {
        double margin_level = GetMarginLevel();
        if (margin_level < config_.margin_stop_out && !positions_.empty()) {
            if (config_.verbose) {
                std::cout << "MARGIN STOP-OUT at " << margin_level << "%" << std::endl;
            }

            // Close all positions
            while (!positions_.empty()) {
                ClosePosition(positions_.front().id);
            }
        }
    }
};

} // namespace backtest

#endif // MULTI_SYMBOL_ENGINE_H
