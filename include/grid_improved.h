#pragma once
/**
 * grid_improved.h
 *
 * Improved Grid strategy with:
 * 1. Regime filter (only trade in uptrends)
 * 2. Profit taking (lock in gains periodically)
 * 3. Crash detection (exit before major crashes)
 * 4. Volatility-adjusted sizing
 */

#include <vector>
#include <cmath>
#include <algorithm>
#include <deque>

struct ImprovedConfig {
    // === Original Grid Parameters ===
    double survive_down_pct = 30.0;
    double min_entry_spacing = 50.0;
    double leverage = 500.0;
    double stop_out_level = 50.0;
    double contract_size = 1.0;
    double spread = 1.0;

    // === IMPROVEMENT 1: Regime Filter ===
    bool enable_regime_filter = false;
    int sma_period = 200;           // Only trade when price > SMA(200)
    double sma_buffer_pct = 0.5;    // Buffer below SMA before exiting

    // === IMPROVEMENT 2: Profit Taking ===
    bool enable_profit_taking = false;
    double profit_take_pct = 50.0;  // Take profits when up X% from entry avg
    double profit_take_amount = 0.5; // Close this fraction of positions

    // === IMPROVEMENT 3: Crash Detection ===
    bool enable_crash_detection = false;
    double crash_velocity_threshold = -2.0;  // % drop per 1000 ticks
    int crash_lookback = 1000;
    double crash_exit_pct = 1.0;    // Exit this fraction on crash signal

    // === IMPROVEMENT 4: Volatility Sizing ===
    bool enable_volatility_sizing = false;
    int volatility_period = 100;
    double volatility_multiplier = 1.0;  // Size = base / (1 + vol * multiplier)

    // === IMPROVEMENT 5: Equity Curve Filter ===
    bool enable_equity_filter = false;
    int equity_sma_period = 50;     // Only trade when equity > equity SMA
};

struct ImprovedPosition {
    double entry_price;
    double lots;
};

struct ImprovedResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int total_trades;
    int profit_takes;
    int crash_exits;
    int regime_blocks;
    bool margin_call_occurred;
};

class ImprovedGrid {
private:
    ImprovedConfig config_;
    std::vector<ImprovedPosition> positions_;

    // Account state
    double balance_;
    double initial_balance_;
    double peak_equity_;

    // Tracking
    double all_time_high_;
    double last_entry_price_;
    double avg_entry_price_;

    // SMA for regime filter
    std::deque<double> price_history_;
    double current_sma_;

    // Volatility tracking
    std::deque<double> returns_;
    double current_volatility_;
    double prev_price_;

    // Crash detection
    std::deque<double> recent_prices_;

    // Equity curve filter
    std::deque<double> equity_history_;
    double equity_sma_;

    // Statistics
    ImprovedResult result_;

public:
    void configure(const ImprovedConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        all_time_high_ = 0.0;
        last_entry_price_ = 0.0;
        avg_entry_price_ = 0.0;

        price_history_.clear();
        current_sma_ = 0.0;

        returns_.clear();
        current_volatility_ = 0.0;
        prev_price_ = 0.0;

        recent_prices_.clear();

        equity_history_.clear();
        equity_sma_ = starting_balance;

        result_ = ImprovedResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;

        // Update indicators
        update_sma(mid);
        update_volatility(mid);
        update_crash_detection(mid);

        double equity = get_equity(bid);
        update_equity_filter(equity);

        // Check margin stop-out
        if (check_margin_stop_out(bid)) {
            return;
        }

        // IMPROVEMENT 3: Crash detection exit
        if (config_.enable_crash_detection && detect_crash()) {
            exit_on_crash(bid);
        }

        // IMPROVEMENT 2: Profit taking
        if (config_.enable_profit_taking && !positions_.empty()) {
            check_profit_taking(bid);
        }

        // IMPROVEMENT 1: Regime filter for new entries
        bool regime_ok = true;
        if (config_.enable_regime_filter) {
            regime_ok = check_regime(mid);
            if (!regime_ok) {
                result_.regime_blocks++;
            }
        }

        // IMPROVEMENT 5: Equity curve filter
        bool equity_ok = true;
        if (config_.enable_equity_filter) {
            equity_ok = equity > equity_sma_;
        }

        // Check for new entry
        if (regime_ok && equity_ok) {
            check_new_entry(ask, bid);
        }

        // Update tracking
        if (equity > result_.max_equity) {
            result_.max_equity = equity;
        }
        if (equity > peak_equity_) {
            peak_equity_ = equity;
        }

        if (peak_equity_ > 0) {
            double dd = 100.0 * (peak_equity_ - equity) / peak_equity_;
            if (dd > result_.max_drawdown_pct) {
                result_.max_drawdown_pct = dd;
            }
        }
    }

    double get_equity(double bid) const {
        double unrealized_pnl = 0.0;
        for (const auto& pos : positions_) {
            unrealized_pnl += (bid - pos.entry_price) * pos.lots * config_.contract_size;
        }
        return balance_ + unrealized_pnl;
    }

    ImprovedResult get_result(double final_bid) {
        result_.final_equity = get_equity(final_bid);
        return result_;
    }

private:
    void update_sma(double price) {
        price_history_.push_back(price);
        if (price_history_.size() > (size_t)config_.sma_period) {
            price_history_.pop_front();
        }

        if (price_history_.size() >= (size_t)config_.sma_period) {
            double sum = 0.0;
            for (double p : price_history_) sum += p;
            current_sma_ = sum / price_history_.size();
        }
    }

    void update_volatility(double price) {
        if (prev_price_ > 0) {
            double ret = (price - prev_price_) / prev_price_;
            returns_.push_back(ret);
            if (returns_.size() > (size_t)config_.volatility_period) {
                returns_.pop_front();
            }

            if (returns_.size() >= 20) {
                double sum = 0.0, sum_sq = 0.0;
                for (double r : returns_) {
                    sum += r;
                    sum_sq += r * r;
                }
                double mean = sum / returns_.size();
                double variance = sum_sq / returns_.size() - mean * mean;
                current_volatility_ = std::sqrt(variance) * 100.0;  // As percentage
            }
        }
        prev_price_ = price;
    }

    void update_crash_detection(double price) {
        recent_prices_.push_back(price);
        if (recent_prices_.size() > (size_t)config_.crash_lookback) {
            recent_prices_.pop_front();
        }
    }

    void update_equity_filter(double equity) {
        equity_history_.push_back(equity);
        if (equity_history_.size() > (size_t)config_.equity_sma_period) {
            equity_history_.pop_front();
        }

        if (equity_history_.size() >= (size_t)config_.equity_sma_period) {
            double sum = 0.0;
            for (double e : equity_history_) sum += e;
            equity_sma_ = sum / equity_history_.size();
        }
    }

    bool check_regime(double price) {
        if (current_sma_ <= 0) return true;  // Not enough data yet

        double threshold = current_sma_ * (1.0 - config_.sma_buffer_pct / 100.0);
        return price > threshold;
    }

    bool detect_crash() {
        if (recent_prices_.size() < (size_t)config_.crash_lookback) return false;

        double start_price = recent_prices_.front();
        double end_price = recent_prices_.back();
        double change_pct = (end_price - start_price) / start_price * 100.0;

        return change_pct < config_.crash_velocity_threshold;
    }

    void exit_on_crash(double bid) {
        if (positions_.empty()) return;

        int to_close = (int)(positions_.size() * config_.crash_exit_pct);
        to_close = std::max(1, to_close);

        for (int i = 0; i < to_close && !positions_.empty(); i++) {
            close_position(0, bid);
            result_.crash_exits++;
        }
    }

    void check_profit_taking(double bid) {
        if (avg_entry_price_ <= 0) return;

        double profit_pct = (bid - avg_entry_price_) / avg_entry_price_ * 100.0;

        if (profit_pct >= config_.profit_take_pct) {
            int to_close = (int)(positions_.size() * config_.profit_take_amount);
            to_close = std::max(1, to_close);

            for (int i = 0; i < to_close && !positions_.empty(); i++) {
                close_position(0, bid);
                result_.profit_takes++;
            }

            // Update avg entry after partial close
            update_avg_entry();
        }
    }

    void update_avg_entry() {
        if (positions_.empty()) {
            avg_entry_price_ = 0.0;
            return;
        }

        double total_value = 0.0;
        double total_lots = 0.0;
        for (const auto& pos : positions_) {
            total_value += pos.entry_price * pos.lots;
            total_lots += pos.lots;
        }
        avg_entry_price_ = total_lots > 0 ? total_value / total_lots : 0.0;
    }

    bool check_margin_stop_out(double bid) {
        if (positions_.empty()) return false;

        double used_margin = get_used_margin(bid);
        double equity = get_equity(bid);
        double margin_level = (equity / used_margin) * 100.0;

        if (margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            while (!positions_.empty()) {
                close_position(0, bid);
            }
            return true;
        }
        return false;
    }

    double get_used_margin(double price) const {
        double margin = 0.0;
        for (const auto& pos : positions_) {
            margin += pos.lots * config_.contract_size * price / config_.leverage;
        }
        return margin;
    }

    void check_new_entry(double ask, double bid) {
        if (ask <= all_time_high_) return;

        if (last_entry_price_ > 0 &&
            (ask - last_entry_price_) < config_.min_entry_spacing) {
            all_time_high_ = ask;
            return;
        }

        double lots = calculate_lot_size(ask, bid);
        if (lots < 0.01) return;

        double required_margin = lots * config_.contract_size * ask / config_.leverage;
        double free_margin = get_equity(bid) - get_used_margin(bid);

        if (required_margin > free_margin * 0.9) {
            return;
        }

        ImprovedPosition pos;
        pos.entry_price = ask;
        pos.lots = lots;
        positions_.push_back(pos);

        all_time_high_ = ask;
        last_entry_price_ = ask;
        result_.total_trades++;

        update_avg_entry();
    }

    double calculate_lot_size(double price, double bid) {
        double equity = get_equity(bid);
        if (equity <= 0) return 0;

        double survive_drop = price * (config_.survive_down_pct / 100.0);

        double existing_loss = 0.0;
        for (const auto& pos : positions_) {
            existing_loss += survive_drop * pos.lots * config_.contract_size;
        }

        double available = equity - existing_loss;
        if (available <= 0) return 0;

        double margin_per_lot = price * config_.contract_size / config_.leverage;
        double loss_per_lot = survive_drop * config_.contract_size;
        double total_per_lot = margin_per_lot + loss_per_lot + config_.spread * config_.contract_size;

        if (total_per_lot <= 0) return 0.01;

        double lots = (available * 0.5) / total_per_lot;

        // IMPROVEMENT 4: Volatility adjustment
        if (config_.enable_volatility_sizing && current_volatility_ > 0) {
            double vol_factor = 1.0 / (1.0 + current_volatility_ * config_.volatility_multiplier);
            lots *= vol_factor;
        }

        lots = std::max(0.01, std::min(100.0, lots));
        lots = std::floor(lots * 100.0) / 100.0;

        return lots;
    }

    void close_position(int index, double bid) {
        if (index < 0 || index >= (int)positions_.size()) return;

        ImprovedPosition& pos = positions_[index];
        double pnl = (bid - pos.entry_price) * pos.lots * config_.contract_size;
        balance_ += pnl;

        positions_.erase(positions_.begin() + index);
    }
};
