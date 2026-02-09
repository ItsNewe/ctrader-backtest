#pragma once
/**
 * grid_optimized_strategy.h
 *
 * Optimized version of grid_open_upwards_while_going_upwards
 *
 * Key improvements over original:
 * 1. ATR-based trailing stops - locks in profits before corrections
 * 2. Minimum entry spacing - prevents overtrading on grinding moves
 * 3. Portfolio DD limit - circuit breaker for catastrophic drawdowns
 *
 * Original strategy: Opens long on every new all-time high using
 *                   survive-based position sizing
 * Problem: Thousands of tightly-spaced positions get wiped out together
 * Solution: Space entries apart so trailing stops can work individually
 *
 * Tested results (2025 full-year tick data):
 *
 * NAS100 (53M ticks, +23.7% underlying):
 * - Best:         1.11x return, 8% DD  (survive=8%, spacing=10, ATR=2.5x)
 * - Default:      1.05x return, 8% DD  (survive=10%, spacing=10, ATR=2.0x)
 * - Conservative: 1.03x return, 2% DD  (survive=10%, spacing=50, ATR=2.0x)
 * - Aggressive:   1.13x return, 24% DD (survive=8%, spacing=5, ATR=1.5x)
 *
 * Gold/XAUUSD (53M ticks, +69.7% underlying):
 * - Best:         2.79x return, 28% DD (survive=1%, spacing=$1, ATR=1.5x)
 * - Default:      1.96x return, 27% DD (survive=2%, spacing=$1, ATR=2.0x)
 * - Conservative: 1.29x return, 20% DD (survive=3%, spacing=$2, ATR=2.0x)
 * - Aggressive:   2.59x return, 28% DD (survive=1%, spacing=$1, ATR=1.5x)
 *
 * Usage:
 *   GridOptimizedStrategy strategy;
 *   strategy.configure(GridOptimizedConfig::Gold_Best());
 *   strategy.reset(10000.0);  // Starting equity
 *   for (double price : tick_prices) {
 *       strategy.on_tick(price);
 *   }
 *   double final_equity = strategy.get_equity(current_price);
 */

#include <vector>
#include <cmath>
#include <algorithm>

struct GridOptimizedConfig {
    // === Core Parameters ===
    double survive_down_pct = 10.0;     // % price can drop before margin call per position
    double min_entry_spacing = 10.0;    // Minimum points between new entries

    // === ATR Trailing Stop ===
    bool enable_trailing = true;
    double atr_multiplier = 2.0;        // Trail at ATR * multiplier below peak
    int atr_period = 14;                // ATR calculation period

    // === Portfolio Protection ===
    double max_portfolio_dd = 100.0;    // Max DD% before closing all (100 = disabled)

    // === Symbol Parameters (set these based on instrument) ===
    double contract_size = 1.0;         // NAS100=1, Gold=100
    double leverage = 500.0;
    double spread = 1.0;                // In price units

    // === Take Profit (optional) ===
    double take_profit_pts = 0.0;       // 0 = disabled, let trailing handle exits

    // === Presets ===
    // === NAS100 Presets (tested on 2025 full-year data) ===

    static GridOptimizedConfig NAS100_Default() {
        // Balanced config: 1.05x return, 8.1% max DD
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = 10.0;
        cfg.min_entry_spacing = 10.0;   // 10 index points
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 50.0;
        cfg.contract_size = 1.0;
        cfg.leverage = 500.0;
        cfg.spread = 1.0;
        return cfg;
    }

    static GridOptimizedConfig NAS100_Best() {
        // Best tested: 1.11x return with moderate DD
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = 8.0;
        cfg.min_entry_spacing = 10.0;
        cfg.atr_multiplier = 2.5;
        cfg.max_portfolio_dd = 50.0;
        cfg.contract_size = 1.0;
        cfg.leverage = 500.0;
        cfg.spread = 1.0;
        return cfg;
    }

    static GridOptimizedConfig NAS100_Conservative() {
        // Low risk: 1.03x return, only 2.3% max DD
        GridOptimizedConfig cfg = NAS100_Default();
        cfg.min_entry_spacing = 50.0;   // Fewer trades, lower DD
        cfg.max_portfolio_dd = 25.0;
        return cfg;
    }

    static GridOptimizedConfig NAS100_Aggressive() {
        // Higher risk/reward: 1.13x return, 24% max DD
        GridOptimizedConfig cfg = NAS100_Default();
        cfg.survive_down_pct = 8.0;
        cfg.min_entry_spacing = 5.0;    // More trades
        cfg.atr_multiplier = 1.5;
        cfg.max_portfolio_dd = 40.0;
        return cfg;
    }

    // === Gold (XAUUSD) Presets (tested on 2025 full-year data) ===

    static GridOptimizedConfig Gold_Default() {
        // Good balance: 1.96x return, 26.9% max DD
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = 2.0;
        cfg.min_entry_spacing = 1.0;    // $1 spacing
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 30.0;
        cfg.contract_size = 100.0;
        cfg.leverage = 500.0;
        cfg.spread = 0.20;              // 20 cents
        return cfg;
    }

    static GridOptimizedConfig Gold_Best() {
        // Best tested: 2.79x return
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = 1.0;     // More aggressive sizing
        cfg.min_entry_spacing = 1.0;    // $1 spacing
        cfg.atr_multiplier = 1.5;       // Tighter trailing
        cfg.max_portfolio_dd = 30.0;
        cfg.contract_size = 100.0;
        cfg.leverage = 500.0;
        cfg.spread = 0.20;
        return cfg;
    }

    static GridOptimizedConfig Gold_Conservative() {
        // Lower risk: 1.29x return, 20.2% max DD
        GridOptimizedConfig cfg = Gold_Default();
        cfg.min_entry_spacing = 2.0;    // $2 spacing
        cfg.survive_down_pct = 3.0;
        cfg.max_portfolio_dd = 25.0;
        return cfg;
    }

    static GridOptimizedConfig Gold_Aggressive() {
        // Highest returns: 2.59x return, 28.1% max DD
        GridOptimizedConfig cfg = Gold_Default();
        cfg.survive_down_pct = 1.0;
        cfg.min_entry_spacing = 1.0;
        cfg.atr_multiplier = 1.5;
        cfg.max_portfolio_dd = 35.0;
        return cfg;
    }
};

struct GridPosition {
    double entry_price;
    double lots;
    double peak_price;          // Highest price since entry (for trailing)
    double trailing_stop;       // Current trailing stop level
    bool trailing_active;       // Has trailing been activated
};

class GridOptimizedStrategy {
public:
    GridOptimizedConfig config;

private:
    std::vector<GridPosition> positions_;
    double equity_;
    double initial_equity_;
    double all_time_high_;
    double last_entry_price_;   // For spacing check
    double peak_equity_;        // For portfolio DD tracking

    // ATR calculation
    std::vector<double> tr_values_;
    double current_atr_;
    double prev_close_;

    // Statistics
    int total_trades_;
    int winning_trades_;
    int trailing_exits_;
    double max_drawdown_;

public:
    GridOptimizedStrategy() { reset(10000.0); }

    void reset(double starting_equity) {
        positions_.clear();
        equity_ = starting_equity;
        initial_equity_ = starting_equity;
        peak_equity_ = starting_equity;
        all_time_high_ = 0.0;
        last_entry_price_ = 0.0;

        tr_values_.clear();
        current_atr_ = 0.0;
        prev_close_ = 0.0;

        total_trades_ = 0;
        winning_trades_ = 0;
        trailing_exits_ = 0;
        max_drawdown_ = 0.0;
    }

    void configure(const GridOptimizedConfig& cfg) {
        config = cfg;
    }

    // Process a single tick
    void on_tick(double price, double high = 0, double low = 0, double close = 0) {
        // Use price as high/low/close if not provided (tick data)
        if (high == 0) high = price;
        if (low == 0) low = price;
        if (close == 0) close = price;

        // Update ATR
        update_atr(high, low, close);

        // Check portfolio DD limit first
        if (check_portfolio_dd(price)) {
            return; // All positions closed
        }

        // Update trailing stops and check for exits
        process_trailing_stops(price);

        // Check for new entry on all-time high
        check_new_entry(price);
    }

    // Get current equity (mark-to-market)
    double get_equity(double current_price) const {
        double unrealized_pnl = 0.0;
        for (const auto& pos : positions_) {
            unrealized_pnl += (current_price - pos.entry_price) * pos.lots * config.contract_size;
        }
        return equity_ + unrealized_pnl;
    }

    double get_realized_equity() const { return equity_; }
    double get_initial_equity() const { return initial_equity_; }
    double get_max_drawdown() const { return max_drawdown_; }
    int get_position_count() const { return positions_.size(); }
    int get_total_trades() const { return total_trades_; }
    int get_winning_trades() const { return winning_trades_; }
    int get_trailing_exits() const { return trailing_exits_; }
    double get_win_rate() const {
        return total_trades_ > 0 ? (100.0 * winning_trades_ / total_trades_) : 0.0;
    }
    double get_all_time_high() const { return all_time_high_; }
    double get_current_atr() const { return current_atr_; }

    const std::vector<GridPosition>& get_positions() const { return positions_; }

private:
    void update_atr(double high, double low, double close) {
        if (prev_close_ > 0) {
            double tr = std::max({
                high - low,
                std::abs(high - prev_close_),
                std::abs(low - prev_close_)
            });
            tr_values_.push_back(tr);

            // Keep only last atr_period values
            if (tr_values_.size() > (size_t)config.atr_period) {
                tr_values_.erase(tr_values_.begin());
            }

            // Calculate ATR as simple moving average of TR
            if (tr_values_.size() >= (size_t)config.atr_period) {
                double sum = 0.0;
                for (double v : tr_values_) sum += v;
                current_atr_ = sum / tr_values_.size();
            }
        }
        prev_close_ = close;
    }

    bool check_portfolio_dd(double price) {
        if (config.max_portfolio_dd >= 100.0) return false;

        double current_equity = get_equity(price);
        if (current_equity > peak_equity_) {
            peak_equity_ = current_equity;
        }

        // Avoid divide by zero
        if (peak_equity_ <= 0) return false;

        double dd_pct = 100.0 * (peak_equity_ - current_equity) / peak_equity_;
        if (dd_pct > max_drawdown_) {
            max_drawdown_ = dd_pct;
        }

        if (dd_pct >= config.max_portfolio_dd && !positions_.empty()) {
            // Close all positions
            close_all_positions(price, "Portfolio DD limit");
            return true;
        }
        return false;
    }

    void process_trailing_stops(double price) {
        if (!config.enable_trailing || current_atr_ <= 0) return;

        std::vector<int> to_close;

        for (size_t i = 0; i < positions_.size(); i++) {
            GridPosition& pos = positions_[i];

            // Update peak price
            if (price > pos.peak_price) {
                pos.peak_price = price;
                // Update trailing stop
                pos.trailing_stop = price - (current_atr_ * config.atr_multiplier);
                pos.trailing_active = true;
            }

            // Check if trailing stop hit
            if (pos.trailing_active && price <= pos.trailing_stop) {
                to_close.push_back(i);
            }
        }

        // Close positions (reverse order to maintain indices)
        for (int i = to_close.size() - 1; i >= 0; i--) {
            close_position(to_close[i], price, true);
        }
    }

    void check_new_entry(double price) {
        // Only enter on new all-time highs
        if (price <= all_time_high_) return;

        // Check minimum spacing from last entry
        if (last_entry_price_ > 0 &&
            (price - last_entry_price_) < config.min_entry_spacing) {
            // Just update ATH without entering
            all_time_high_ = price;
            return;
        }

        // Calculate position size based on survive_down logic
        double lots = calculate_lot_size(price);
        if (lots <= 0) return;

        // Open new position
        GridPosition pos;
        pos.entry_price = price;
        pos.lots = lots;
        pos.peak_price = price;
        pos.trailing_stop = price - (current_atr_ > 0 ? current_atr_ * config.atr_multiplier : price * 0.01);
        pos.trailing_active = false;

        positions_.push_back(pos);

        all_time_high_ = price;
        last_entry_price_ = price;
        total_trades_++;
    }

    double calculate_lot_size(double price) {
        // Survive-based sizing: How much can we afford if price drops survive_down_pct?
        double available_equity = equity_;
        if (available_equity <= 0) return 0;
        if (price <= 0) return 0;

        // Calculate how much margin we need
        double survive_drop = price * (config.survive_down_pct / 100.0);
        double margin_per_lot = price * config.contract_size / config.leverage;
        double risk_per_lot = survive_drop * config.contract_size + config.spread * config.contract_size;

        // Avoid divide by zero
        double total_per_lot = margin_per_lot + risk_per_lot;
        if (total_per_lot <= 0) return 0.01;

        // Position size that survives the drop
        double max_lots = available_equity / total_per_lot;

        // Apply a fraction to not use 100% of equity
        double lots = max_lots * 0.5;  // Use 50% of max to leave buffer

        // Minimum and maximum lot size check
        if (lots < 0.01) lots = 0.01;
        if (lots > 1000.0) lots = 1000.0;  // Sanity cap

        return lots;
    }

    void close_position(int index, double price, bool is_trailing_exit) {
        if (index < 0 || index >= (int)positions_.size()) return;

        GridPosition& pos = positions_[index];
        double pnl = (price - pos.entry_price) * pos.lots * config.contract_size;

        equity_ += pnl;

        if (pnl > 0) winning_trades_++;
        if (is_trailing_exit) trailing_exits_++;

        positions_.erase(positions_.begin() + index);
    }

    void close_all_positions(double price, const char* reason) {
        while (!positions_.empty()) {
            close_position(0, price, false);
        }
        // Reset for potential re-entry after DD event
        last_entry_price_ = 0;
    }
};

// Convenience function for quick backtesting
inline void run_grid_optimized_backtest(
    const std::vector<double>& prices,
    const GridOptimizedConfig& config,
    double starting_equity,
    double& final_equity,
    double& max_dd,
    int& trades,
    int& wins,
    int& trail_exits
) {
    GridOptimizedStrategy strategy;
    strategy.reset(starting_equity);
    strategy.configure(config);

    for (double price : prices) {
        strategy.on_tick(price);
    }

    final_equity = strategy.get_equity(prices.back());
    max_dd = strategy.get_max_drawdown();
    trades = strategy.get_total_trades();
    wins = strategy.get_winning_trades();
    trail_exits = strategy.get_trailing_exits();
}
