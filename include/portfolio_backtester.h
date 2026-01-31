#ifndef PORTFOLIO_BACKTESTER_H
#define PORTFOLIO_BACKTESTER_H

#include <map>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <thread>
#include <mutex>
#include <memory>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>

namespace backtest {

/**
 * Simple tick structure for portfolio backtester
 */
struct PortfolioTick {
    std::string timestamp;
    int64_t time_ms;
    double bid;
    double ask;
    long volume;

    double mid() const { return (bid + ask) / 2.0; }
    double spread() const { return ask - bid; }
};

/**
 * Portfolio trade record
 */
struct PortfolioTrade {
    std::string symbol;
    std::string direction;  // "BUY" or "SELL"
    double entry_price;
    double exit_price;
    double lot_size;
    double profit_loss;
    int64_t entry_time_ms;
    int64_t exit_time_ms;
    std::string exit_reason;
};

/**
 * Portfolio-level position tracking
 */
struct PortfolioPosition {
    std::string symbol;
    uint64_t ticket;
    bool is_buy;
    double entry_price;
    double volume;
    double stop_loss;
    double take_profit;
    int64_t entry_time_ms;
    double unrealized_pnl;
};

/**
 * Symbol-specific configuration
 */
struct SymbolConfig {
    std::string symbol;
    std::string tick_file;
    double contract_size = 100.0;
    double pip_size = 0.01;
    double swap_long = 0.0;
    double swap_short = 0.0;
    double max_allocation_pct = 100.0;  // Max % of equity for this symbol
    double correlation_weight = 1.0;     // Weight for correlation adjustments
};

/**
 * Portfolio backtest configuration
 */
struct PortfolioConfig {
    double initial_balance = 10000.0;
    double leverage = 500.0;
    std::string start_date;
    std::string end_date;

    // Risk management
    double max_total_exposure_pct = 100.0;  // Max total exposure as % of equity
    double max_correlation_exposure = 0.8;   // Max exposure to correlated positions
    double max_drawdown_pct = 50.0;          // Stop trading if exceeded

    // Position limits
    int max_positions_per_symbol = 50;
    int max_total_positions = 200;

    // Symbols to trade
    std::vector<SymbolConfig> symbols;
};

/**
 * Portfolio-level results
 */
struct PortfolioResults {
    double initial_balance;
    double final_balance;
    double total_profit;
    double max_drawdown_pct;
    double sharpe_ratio;
    double sortino_ratio;
    double calmar_ratio;
    int total_trades;
    int winning_trades;
    double win_rate;
    double profit_factor;

    // Per-symbol breakdown
    std::map<std::string, double> symbol_profits;
    std::map<std::string, int> symbol_trades;
    std::map<std::string, double> symbol_contribution_pct;

    // Correlation metrics
    double avg_correlation;
    double max_correlation;
    double diversification_ratio;

    // Equity curve
    std::vector<std::pair<int64_t, double>> equity_curve;

    void Print() const {
        std::cout << "\n================================================================================\n";
        std::cout << "PORTFOLIO BACKTEST RESULTS\n";
        std::cout << "================================================================================\n";
        std::cout << "Initial Balance: $" << initial_balance << std::endl;
        std::cout << "Final Balance:   $" << final_balance << std::endl;
        std::cout << "Total Profit:    $" << total_profit << std::endl;
        std::cout << "Max Drawdown:    " << max_drawdown_pct << "%" << std::endl;
        std::cout << "Sharpe Ratio:    " << sharpe_ratio << std::endl;
        std::cout << "Sortino Ratio:   " << sortino_ratio << std::endl;
        std::cout << "Total Trades:    " << total_trades << std::endl;
        std::cout << "Win Rate:        " << win_rate << "%" << std::endl;
        std::cout << "Profit Factor:   " << profit_factor << std::endl;

        std::cout << "\n--- Per-Symbol Breakdown ---\n";
        for (const auto& [sym, profit] : symbol_profits) {
            std::cout << sym << ": $" << profit
                     << " (" << symbol_trades.at(sym) << " trades, "
                     << symbol_contribution_pct.at(sym) << "% contribution)\n";
        }
    }
};

/**
 * Multi-tick event for synchronized processing
 */
struct MultiSymbolTick {
    int64_t timestamp_ms;
    std::map<std::string, PortfolioTick> ticks;  // Symbol -> Tick

    bool HasSymbol(const std::string& symbol) const {
        return ticks.find(symbol) != ticks.end();
    }

    const PortfolioTick& Get(const std::string& symbol) const {
        return ticks.at(symbol);
    }
};

/**
 * Portfolio backtester - runs strategies across multiple symbols
 */
class PortfolioBacktester {
public:
    using StrategyCallback = std::function<void(const MultiSymbolTick&, PortfolioBacktester&)>;

private:
    PortfolioConfig config_;
    double balance_;
    double equity_;
    double peak_equity_;
    double max_drawdown_pct_;

    std::vector<PortfolioPosition> positions_;
    std::vector<PortfolioTrade> all_trades_;
    std::vector<std::pair<int64_t, double>> equity_curve_;

    // Symbol data
    std::map<std::string, std::vector<PortfolioTick>> symbol_ticks_;
    std::map<std::string, SymbolConfig> symbol_configs_;
    std::map<std::string, PortfolioTick> current_ticks_;

    // State
    uint64_t next_ticket_ = 1;
    bool stopped_ = false;
    std::string stop_reason_;

    // Thread safety
    mutable std::mutex mutex_;

public:
    PortfolioBacktester(const PortfolioConfig& config)
        : config_(config), balance_(config.initial_balance),
          equity_(config.initial_balance), peak_equity_(config.initial_balance),
          max_drawdown_pct_(0) {

        for (const auto& sym_cfg : config.symbols) {
            symbol_configs_[sym_cfg.symbol] = sym_cfg;
        }
    }

    /**
     * Load tick data for all symbols (simplified CSV loader)
     */
    bool LoadTickData() {
        for (const auto& sym_cfg : config_.symbols) {
            std::cout << "Loading ticks for " << sym_cfg.symbol << "..." << std::endl;

            std::ifstream file(sym_cfg.tick_file);
            if (!file.is_open()) {
                std::cerr << "Failed to open " << sym_cfg.tick_file << std::endl;
                return false;
            }

            std::vector<PortfolioTick> ticks;
            std::string line;
            bool is_header = true;

            while (std::getline(file, line)) {
                if (is_header) {
                    is_header = false;
                    continue;
                }

                // Parse MT5 CSV format: <DATE>\t<TIME>\t<BID>\t<ASK>\t<LAST>\t<VOLUME>
                std::stringstream ss(line);
                std::string date, time, bid_str, ask_str, last_str, vol_str;

                if (std::getline(ss, date, '\t') &&
                    std::getline(ss, time, '\t') &&
                    std::getline(ss, bid_str, '\t') &&
                    std::getline(ss, ask_str, '\t')) {

                    PortfolioTick tick;
                    tick.timestamp = date + " " + time;
                    tick.bid = std::stod(bid_str);
                    tick.ask = std::stod(ask_str);
                    tick.time_ms = ticks.size();  // Simple incrementing ID
                    tick.volume = 0;

                    ticks.push_back(tick);
                }
            }

            symbol_ticks_[sym_cfg.symbol] = std::move(ticks);
            std::cout << "  Loaded " << symbol_ticks_[sym_cfg.symbol].size() << " ticks" << std::endl;
        }
        return true;
    }

    /**
     * Run portfolio backtest with synchronized ticks
     */
    void Run(StrategyCallback strategy) {
        if (symbol_ticks_.empty()) {
            std::cerr << "No tick data loaded" << std::endl;
            return;
        }

        // Create merged tick stream
        std::vector<MultiSymbolTick> merged_ticks = MergeTickStreams();
        std::cout << "Merged " << merged_ticks.size() << " multi-symbol ticks" << std::endl;

        // Process each merged tick
        for (const auto& multi_tick : merged_ticks) {
            if (stopped_) break;

            // Update current ticks
            for (const auto& [symbol, tick] : multi_tick.ticks) {
                current_ticks_[symbol] = tick;
            }

            // Update positions P/L
            UpdatePositionsPnL(multi_tick);

            // Check drawdown limit
            CheckDrawdownLimit();

            // Call strategy
            strategy(multi_tick, *this);

            // Record equity
            equity_curve_.push_back({multi_tick.timestamp_ms, equity_});
        }

        // Close all remaining positions at last tick
        if (!positions_.empty() && !merged_ticks.empty()) {
            CloseAllPositions(merged_ticks.back());
        }
    }

    // ==================== Trading Operations ====================

    /**
     * Open a position on a symbol
     */
    uint64_t OpenPosition(const std::string& symbol, bool is_buy, double volume,
                          double sl = 0, double tp = 0) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopped_) return 0;

        auto it = symbol_configs_.find(symbol);
        if (it == symbol_configs_.end()) return 0;

        const auto& sym_cfg = it->second;

        // Check position limits
        if (positions_.size() >= static_cast<size_t>(config_.max_total_positions)) {
            return 0;
        }

        int symbol_positions = std::count_if(positions_.begin(), positions_.end(),
            [&symbol](const PortfolioPosition& p) { return p.symbol == symbol; });

        if (symbol_positions >= config_.max_positions_per_symbol) {
            return 0;
        }

        // Check allocation limit
        double symbol_exposure = CalculateSymbolExposure(symbol);
        double new_exposure = volume * sym_cfg.contract_size * GetCurrentPrice(symbol, is_buy);
        if ((symbol_exposure + new_exposure) / equity_ * 100 > sym_cfg.max_allocation_pct) {
            return 0;
        }

        // Check total exposure
        double total_exposure = CalculateTotalExposure();
        if ((total_exposure + new_exposure) / equity_ * 100 > config_.max_total_exposure_pct) {
            return 0;
        }

        // Create position
        PortfolioPosition pos;
        pos.symbol = symbol;
        pos.ticket = next_ticket_++;
        pos.is_buy = is_buy;
        pos.entry_price = GetCurrentPrice(symbol, is_buy);
        pos.volume = volume;
        pos.stop_loss = sl;
        pos.take_profit = tp;
        pos.entry_time_ms = GetCurrentTimestamp(symbol);
        pos.unrealized_pnl = 0;

        positions_.push_back(pos);

        return pos.ticket;
    }

    /**
     * Close a specific position
     */
    bool ClosePosition(uint64_t ticket) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = std::find_if(positions_.begin(), positions_.end(),
            [ticket](const PortfolioPosition& p) { return p.ticket == ticket; });

        if (it == positions_.end()) return false;

        double exit_price = GetCurrentPrice(it->symbol, !it->is_buy);
        double pnl = CalculatePositionPnL(*it, exit_price);

        // Record trade
        PortfolioTrade trade;
        trade.symbol = it->symbol;
        trade.direction = it->is_buy ? "BUY" : "SELL";
        trade.entry_price = it->entry_price;
        trade.exit_price = exit_price;
        trade.lot_size = it->volume;
        trade.profit_loss = pnl;
        trade.entry_time_ms = it->entry_time_ms;
        trade.exit_time_ms = GetCurrentTimestamp(it->symbol);
        trade.exit_reason = "close";
        all_trades_.push_back(trade);

        // Update balance
        balance_ += pnl;
        equity_ = balance_;
        for (const auto& pos : positions_) {
            if (pos.ticket != ticket) {
                equity_ += pos.unrealized_pnl;
            }
        }

        // Update peak/drawdown
        if (equity_ > peak_equity_) {
            peak_equity_ = equity_;
        }
        double dd = (peak_equity_ - equity_) / peak_equity_ * 100;
        if (dd > max_drawdown_pct_) {
            max_drawdown_pct_ = dd;
        }

        positions_.erase(it);
        return true;
    }

    /**
     * Close all positions for a symbol
     */
    void CloseSymbolPositions(const std::string& symbol) {
        std::vector<uint64_t> tickets;
        for (const auto& pos : positions_) {
            if (pos.symbol == symbol) {
                tickets.push_back(pos.ticket);
            }
        }
        for (uint64_t ticket : tickets) {
            ClosePosition(ticket);
        }
    }

    // ==================== Getters ====================

    double Balance() const { return balance_; }
    double Equity() const { return equity_; }
    double MaxDrawdownPct() const { return max_drawdown_pct_; }

    const std::vector<PortfolioPosition>& Positions() const { return positions_; }

    std::vector<PortfolioPosition> GetPositions(const std::string& symbol) const {
        std::vector<PortfolioPosition> result;
        for (const auto& pos : positions_) {
            if (symbol.empty() || pos.symbol == symbol) {
                result.push_back(pos);
            }
        }
        return result;
    }

    int PositionCount(const std::string& symbol = "") const {
        if (symbol.empty()) return positions_.size();
        return std::count_if(positions_.begin(), positions_.end(),
            [&symbol](const PortfolioPosition& p) { return p.symbol == symbol; });
    }

    double TotalLots(const std::string& symbol = "") const {
        double total = 0;
        for (const auto& pos : positions_) {
            if (symbol.empty() || pos.symbol == symbol) {
                total += pos.volume;
            }
        }
        return total;
    }

    bool IsStopped() const { return stopped_; }
    const std::string& StopReason() const { return stop_reason_; }

    /**
     * Get results summary
     */
    PortfolioResults GetResults() const {
        PortfolioResults results;
        results.initial_balance = config_.initial_balance;
        results.final_balance = balance_;
        results.total_profit = balance_ - config_.initial_balance;
        results.max_drawdown_pct = max_drawdown_pct_;
        results.total_trades = all_trades_.size();

        // Win rate
        results.winning_trades = std::count_if(all_trades_.begin(), all_trades_.end(),
            [](const PortfolioTrade& t) { return t.profit_loss > 0; });
        results.win_rate = results.total_trades > 0
            ? 100.0 * results.winning_trades / results.total_trades : 0;

        // Profit factor
        double gross_profit = 0, gross_loss = 0;
        for (const auto& t : all_trades_) {
            if (t.profit_loss > 0) gross_profit += t.profit_loss;
            else gross_loss -= t.profit_loss;
        }
        results.profit_factor = gross_loss > 0 ? gross_profit / gross_loss : 0;

        // Per-symbol breakdown
        for (const auto& t : all_trades_) {
            results.symbol_profits[t.symbol] += t.profit_loss;
            results.symbol_trades[t.symbol]++;
        }

        // Symbol contribution
        for (const auto& [sym, profit] : results.symbol_profits) {
            results.symbol_contribution_pct[sym] =
                results.total_profit != 0 ? 100.0 * profit / results.total_profit : 0;
        }

        // Sharpe ratio (annualized, assuming daily returns)
        results.sharpe_ratio = CalculateSharpeRatio();
        results.sortino_ratio = CalculateSortinoRatio();
        results.calmar_ratio = max_drawdown_pct_ > 0
            ? (results.total_profit / config_.initial_balance * 100) / max_drawdown_pct_ : 0;

        results.equity_curve = equity_curve_;

        return results;
    }

private:
    /**
     * Merge tick streams from all symbols into time-synchronized events
     */
    std::vector<MultiSymbolTick> MergeTickStreams() {
        std::vector<MultiSymbolTick> merged;

        // Track current index for each symbol
        std::map<std::string, size_t> indices;
        for (const auto& [symbol, ticks] : symbol_ticks_) {
            indices[symbol] = 0;
        }

        int64_t tick_id = 0;

        while (true) {
            // Find minimum index across all symbols (simplified - just round-robin)
            std::string next_symbol;
            size_t min_remaining = SIZE_MAX;

            for (const auto& [symbol, ticks] : symbol_ticks_) {
                size_t remaining = ticks.size() - indices[symbol];
                if (remaining > 0 && remaining < min_remaining) {
                    min_remaining = remaining;
                    next_symbol = symbol;
                }
            }

            if (next_symbol.empty()) break;  // All streams exhausted

            // Create multi-symbol tick with all available symbols
            MultiSymbolTick multi_tick;
            multi_tick.timestamp_ms = tick_id++;

            for (const auto& [symbol, ticks] : symbol_ticks_) {
                size_t idx = indices[symbol];
                if (idx < ticks.size()) {
                    multi_tick.ticks[symbol] = ticks[idx];
                }
            }

            // Advance one symbol
            indices[next_symbol]++;

            merged.push_back(multi_tick);

            // Limit for testing
            if (merged.size() >= 1000000) break;
        }

        return merged;
    }

    void UpdatePositionsPnL(const MultiSymbolTick& multi_tick) {
        equity_ = balance_;

        for (auto& pos : positions_) {
            if (!multi_tick.HasSymbol(pos.symbol)) continue;

            double exit_price = pos.is_buy
                ? multi_tick.Get(pos.symbol).bid
                : multi_tick.Get(pos.symbol).ask;

            pos.unrealized_pnl = CalculatePositionPnL(pos, exit_price);
            equity_ += pos.unrealized_pnl;

            // Check SL/TP
            const auto& tick = multi_tick.Get(pos.symbol);
            if (pos.is_buy) {
                if (pos.stop_loss > 0 && tick.bid <= pos.stop_loss) {
                    ClosePosition(pos.ticket);
                } else if (pos.take_profit > 0 && tick.bid >= pos.take_profit) {
                    ClosePosition(pos.ticket);
                }
            } else {
                if (pos.stop_loss > 0 && tick.ask >= pos.stop_loss) {
                    ClosePosition(pos.ticket);
                } else if (pos.take_profit > 0 && tick.ask <= pos.take_profit) {
                    ClosePosition(pos.ticket);
                }
            }
        }

        // Update peak/drawdown
        if (equity_ > peak_equity_) {
            peak_equity_ = equity_;
        }
        double dd = (peak_equity_ - equity_) / peak_equity_ * 100;
        if (dd > max_drawdown_pct_) {
            max_drawdown_pct_ = dd;
        }
    }

    void CheckDrawdownLimit() {
        if (max_drawdown_pct_ >= config_.max_drawdown_pct) {
            stopped_ = true;
            stop_reason_ = "Max drawdown exceeded: " + std::to_string(max_drawdown_pct_) + "%";
        }
    }

    void CloseAllPositions(const MultiSymbolTick& multi_tick) {
        std::vector<uint64_t> tickets;
        for (const auto& pos : positions_) {
            tickets.push_back(pos.ticket);
        }
        for (uint64_t ticket : tickets) {
            ClosePosition(ticket);
        }
    }

    double CalculatePositionPnL(const PortfolioPosition& pos, double exit_price) const {
        auto it = symbol_configs_.find(pos.symbol);
        if (it == symbol_configs_.end()) return 0;

        double price_diff = pos.is_buy
            ? (exit_price - pos.entry_price)
            : (pos.entry_price - exit_price);

        return price_diff * pos.volume * it->second.contract_size;
    }

    double GetCurrentPrice(const std::string& symbol, bool is_buy) const {
        auto it = current_ticks_.find(symbol);
        if (it == current_ticks_.end()) {
            // Fall back to last loaded tick
            auto ticks_it = symbol_ticks_.find(symbol);
            if (ticks_it == symbol_ticks_.end() || ticks_it->second.empty()) return 0;
            const auto& tick = ticks_it->second.back();
            return is_buy ? tick.ask : tick.bid;
        }
        return is_buy ? it->second.ask : it->second.bid;
    }

    int64_t GetCurrentTimestamp(const std::string& symbol) const {
        auto it = current_ticks_.find(symbol);
        if (it == current_ticks_.end()) return 0;
        return it->second.time_ms;
    }

    double CalculateSymbolExposure(const std::string& symbol) const {
        auto cfg_it = symbol_configs_.find(symbol);
        if (cfg_it == symbol_configs_.end()) return 0;

        double exposure = 0;
        for (const auto& pos : positions_) {
            if (pos.symbol == symbol) {
                exposure += pos.volume * cfg_it->second.contract_size * pos.entry_price;
            }
        }
        return exposure;
    }

    double CalculateTotalExposure() const {
        double exposure = 0;
        for (const auto& pos : positions_) {
            auto cfg_it = symbol_configs_.find(pos.symbol);
            if (cfg_it != symbol_configs_.end()) {
                exposure += pos.volume * cfg_it->second.contract_size * pos.entry_price;
            }
        }
        return exposure;
    }

    double CalculateSharpeRatio() const {
        if (equity_curve_.size() < 2) return 0;

        // Calculate daily returns
        std::vector<double> returns;
        for (size_t i = 1; i < equity_curve_.size(); i += 1000) {  // Sample every 1000 ticks
            double ret = (equity_curve_[i].second - equity_curve_[i-1].second)
                        / equity_curve_[i-1].second;
            returns.push_back(ret);
        }

        if (returns.empty()) return 0;

        double mean = 0;
        for (double r : returns) mean += r;
        mean /= returns.size();

        double variance = 0;
        for (double r : returns) variance += (r - mean) * (r - mean);
        variance /= returns.size();

        double std_dev = sqrt(variance);
        if (std_dev == 0) return 0;

        return (mean / std_dev) * sqrt(252.0);  // Annualized
    }

    double CalculateSortinoRatio() const {
        if (equity_curve_.size() < 2) return 0;

        std::vector<double> returns;
        for (size_t i = 1; i < equity_curve_.size(); i += 1000) {
            double ret = (equity_curve_[i].second - equity_curve_[i-1].second)
                        / equity_curve_[i-1].second;
            returns.push_back(ret);
        }

        if (returns.empty()) return 0;

        double mean = 0;
        for (double r : returns) mean += r;
        mean /= returns.size();

        // Downside deviation (only negative returns)
        double downside_variance = 0;
        int count = 0;
        for (double r : returns) {
            if (r < 0) {
                downside_variance += r * r;
                count++;
            }
        }

        if (count == 0) return mean > 0 ? 999 : 0;
        downside_variance /= count;
        double downside_dev = sqrt(downside_variance);

        if (downside_dev == 0) return 0;

        return (mean / downside_dev) * sqrt(252.0);
    }
};

}  // namespace backtest

#endif  // PORTFOLIO_BACKTESTER_H
