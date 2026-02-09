#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cfloat>
#include <set>

using namespace backtest;

/**
 * DualCapitalStrategy - Splits capital 50/50 between:
 * 1. TrendUp (Up While Up): Opens BUY during uptrends
 * 2. Reversal (Up While Down): Opens BUY during downtrends, holds through recovery
 */
class DualCapitalStrategy {
public:
    struct Config {
        double survive_pct = 13.0;
        double base_spacing = 1.50;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double spacing_mult = 2.0;

        // Capital split
        double trend_up_pct = 0.50;   // 50% for up while up
        double reversal_pct = 0.50;   // 50% for up while down

        // Reversal uses uniform small lots (not calculated)
        bool reversal_uniform_lots = true;
        double reversal_lot_size = 0.01;
    };

    struct Stats {
        int trend_up_entries = 0;
        int reversal_entries = 0;
        double trend_up_volume = 0.0;
        double reversal_volume = 0.0;
        double first_trend_up_lot = 0.0;
        double first_reversal_lot = 0.0;
        double max_trend_up_lot = 0.0;
        double max_reversal_lot = 0.0;
        int direction_changes = 0;
    };

private:
    Config config_;
    Stats stats_;
    int direction_ = 0;
    double bid_at_turn_up_ = DBL_MAX;
    double bid_at_turn_down_ = 0.0;
    double ask_at_turn_up_ = DBL_MAX;
    double ask_at_turn_down_ = 0.0;
    double last_trend_up_entry_ = 0.0;
    double last_reversal_entry_ = DBL_MAX;
    double trend_up_volume_ = 0.0;
    double reversal_volume_ = 0.0;
    std::set<int> trend_up_ids_;
    std::set<int> reversal_ids_;
    double current_spacing_ = 0.0;

public:
    DualCapitalStrategy(const Config& config) : config_(config) {
        current_spacing_ = config_.base_spacing;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double spread = tick.ask - tick.bid;
        int old_direction = direction_;
        UpdateDirection(tick, spread);
        if (direction_ != old_direction && old_direction != 0) {
            stats_.direction_changes++;
        }

        UpdateVolume(engine);

        if (direction_ == 1) {
            ExecuteTrendUp(tick, engine);
        } else if (direction_ == -1) {
            ExecuteReversal(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }

private:
    void UpdateDirection(const Tick& tick, double spread) {
        bid_at_turn_down_ = std::max(tick.bid, bid_at_turn_down_);
        bid_at_turn_up_ = std::min(tick.bid, bid_at_turn_up_);
        ask_at_turn_down_ = std::max(tick.ask, ask_at_turn_down_);
        ask_at_turn_up_ = std::min(tick.ask, ask_at_turn_up_);

        double threshold = spread * 2;

        if (direction_ != -1 &&
            ask_at_turn_down_ >= tick.ask + threshold &&
            bid_at_turn_down_ >= tick.bid + threshold) {
            direction_ = -1;
            bid_at_turn_up_ = DBL_MAX;
            ask_at_turn_up_ = DBL_MAX;
            bid_at_turn_down_ = 0.0;
            ask_at_turn_down_ = 0.0;
        }

        if (direction_ != 1 &&
            bid_at_turn_up_ <= tick.bid - threshold &&
            ask_at_turn_up_ <= tick.ask - threshold) {
            direction_ = 1;
            bid_at_turn_up_ = DBL_MAX;
            ask_at_turn_up_ = DBL_MAX;
            bid_at_turn_down_ = 0.0;
            ask_at_turn_down_ = 0.0;
        }
    }

    void UpdateVolume(TickBasedEngine& engine) {
        trend_up_volume_ = 0.0;
        reversal_volume_ = 0.0;

        std::set<int> current_ids;
        for (const Trade* trade : engine.GetOpenPositions()) {
            current_ids.insert(trade->id);
            if (trend_up_ids_.find(trade->id) != trend_up_ids_.end()) {
                trend_up_volume_ += trade->lot_size;
            } else if (reversal_ids_.find(trade->id) != reversal_ids_.end()) {
                reversal_volume_ += trade->lot_size;
            }
        }

        // Clean up closed positions
        std::set<int> to_remove;
        for (int id : trend_up_ids_) {
            if (current_ids.find(id) == current_ids.end()) to_remove.insert(id);
        }
        for (int id : to_remove) trend_up_ids_.erase(id);

        to_remove.clear();
        for (int id : reversal_ids_) {
            if (current_ids.find(id) == current_ids.end()) to_remove.insert(id);
        }
        for (int id : to_remove) reversal_ids_.erase(id);
    }

    double CalculateLotSize(const Tick& tick, TickBasedEngine& engine,
                           double allocation_pct, double mode_volume) {
        double equity = engine.GetEquity();
        double allocated_equity = equity * allocation_pct;
        double survive_distance = tick.ask * (config_.survive_pct / 100.0);
        double potential_loss = mode_volume * survive_distance * config_.contract_size;
        double available_equity = allocated_equity - potential_loss;

        if (available_equity <= 0) return 0.0;

        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double lots = (available_equity * 0.5) / margin_per_lot;
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;

        return std::max(lots, 0.0);
    }

    void ExecuteTrendUp(const Tick& tick, TickBasedEngine& engine) {
        double lots = CalculateLotSize(tick, engine, config_.trend_up_pct, trend_up_volume_);
        if (lots < config_.min_volume) return;

        double spacing = current_spacing_ * config_.spacing_mult;
        bool should_open = (last_trend_up_entry_ == 0.0) ||
                          (tick.ask >= last_trend_up_entry_ + spacing);

        if (should_open) {
            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
            if (trade) {
                trend_up_ids_.insert(trade->id);
                last_trend_up_entry_ = tick.ask;
                if (stats_.trend_up_entries == 0) stats_.first_trend_up_lot = lots;
                stats_.trend_up_entries++;
                stats_.trend_up_volume += lots;
                stats_.max_trend_up_lot = std::max(stats_.max_trend_up_lot, lots);
            }
        }
    }

    void ExecuteReversal(const Tick& tick, TickBasedEngine& engine) {
        // Use uniform lots if configured, otherwise calculate
        double lots;
        if (config_.reversal_uniform_lots) {
            lots = config_.reversal_lot_size;
            // Check margin availability for this mode's allocation
            double equity = engine.GetEquity();
            double allocated_equity = equity * config_.reversal_pct;
            double survive_distance = tick.ask * (config_.survive_pct / 100.0);
            double potential_loss = reversal_volume_ * survive_distance * config_.contract_size;
            double available = allocated_equity - potential_loss;
            double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
            if (available < margin_per_lot * lots) return;  // Can't afford this entry
        } else {
            lots = CalculateLotSize(tick, engine, config_.reversal_pct, reversal_volume_);
            if (lots < config_.min_volume) return;
        }

        double spacing = current_spacing_ * config_.spacing_mult;
        bool should_open = (last_reversal_entry_ >= DBL_MAX) ||
                          (tick.ask <= last_reversal_entry_ - spacing);

        if (should_open) {
            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
            if (trade) {
                reversal_ids_.insert(trade->id);
                last_reversal_entry_ = tick.ask;
                if (stats_.reversal_entries == 0) stats_.first_reversal_lot = lots;
                stats_.reversal_entries++;
                stats_.reversal_volume += lots;
                stats_.max_reversal_lot = std::max(stats_.max_reversal_lot, lots);
            }
        }
    }
};

void PrintResults(const std::string& name, const TickBasedEngine::BacktestResults& results,
                  double initial_balance) {
    double return_mult = results.final_balance / initial_balance;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(35) << name
              << " $" << std::setw(12) << results.final_balance
              << " (" << std::setw(6) << return_mult << "x)"
              << " DD: " << std::setw(6) << results.max_drawdown_pct << "%"
              << std::endl;
}

int main() {
    std::cout << "=== Dual Capital Allocation Test ===" << std::endl;
    std::cout << "Testing 50/50 split between TrendUp and Reversal" << std::endl;
    std::cout << "Period: 2025.10.17 - 2025.12.30 (crash period)" << std::endl;
    std::cout << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    double initial_balance = 10000.0;

    TickBacktestConfig base_config;
    base_config.symbol = "XAUUSD";
    base_config.initial_balance = initial_balance;
    base_config.account_currency = "USD";
    base_config.contract_size = 100.0;
    base_config.leverage = 500.0;
    base_config.margin_rate = 1.0;
    base_config.pip_size = 0.01;
    base_config.swap_long = -66.99;
    base_config.swap_short = 41.2;
    base_config.swap_mode = 1;
    base_config.swap_3days = 3;
    base_config.start_date = "2025.10.17";
    base_config.end_date = "2025.12.30";
    base_config.tick_data_config = tick_config;
    base_config.verbose = false;

    // Test 1: Shared capital (original behavior)
    std::cout << "=== Test 1: Shared Capital (Original) ===" << std::endl;
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        DualCapitalStrategy::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.spacing_mult = 2.0;
        cfg.trend_up_pct = 1.0;   // 100% shared
        cfg.reversal_pct = 1.0;   // 100% shared (both compete for same pool)

        DualCapitalStrategy strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        PrintResults("Shared Capital", results, initial_balance);
        std::cout << "  TrendUp entries: " << stats.trend_up_entries
                  << " | Volume: " << stats.trend_up_volume << " lots"
                  << " | First lot: " << stats.first_trend_up_lot << std::endl;
        std::cout << "  Reversal entries: " << stats.reversal_entries
                  << " | Volume: " << stats.reversal_volume << " lots"
                  << " | First lot: " << stats.first_reversal_lot << std::endl;
        double total_vol = stats.trend_up_volume + stats.reversal_volume;
        if (total_vol > 0) {
            std::cout << "  Volume split: TrendUp " << (stats.trend_up_volume / total_vol * 100) << "%"
                      << " | Reversal " << (stats.reversal_volume / total_vol * 100) << "%" << std::endl;
        }
    }

    std::cout << std::endl;

    // Test 2: 50/50 split capital
    std::cout << "=== Test 2: 50/50 Split Capital ===" << std::endl;
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        DualCapitalStrategy::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.spacing_mult = 2.0;
        cfg.trend_up_pct = 0.50;   // 50% reserved for TrendUp
        cfg.reversal_pct = 0.50;   // 50% reserved for Reversal

        DualCapitalStrategy strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        PrintResults("50/50 Split", results, initial_balance);
        std::cout << "  TrendUp entries: " << stats.trend_up_entries
                  << " | Volume: " << stats.trend_up_volume << " lots"
                  << " | First lot: " << stats.first_trend_up_lot << std::endl;
        std::cout << "  Reversal entries: " << stats.reversal_entries
                  << " | Volume: " << stats.reversal_volume << " lots"
                  << " | First lot: " << stats.first_reversal_lot << std::endl;
        double total_vol = stats.trend_up_volume + stats.reversal_volume;
        if (total_vol > 0) {
            std::cout << "  Volume split: TrendUp " << (stats.trend_up_volume / total_vol * 100) << "%"
                      << " | Reversal " << (stats.reversal_volume / total_vol * 100) << "%" << std::endl;
        }
    }

    std::cout << std::endl;

    // Test 3: 70/30 split (favor TrendUp)
    std::cout << "=== Test 3: 70/30 Split (Favor TrendUp) ===" << std::endl;
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        DualCapitalStrategy::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.spacing_mult = 2.0;
        cfg.trend_up_pct = 0.70;   // 70% for TrendUp
        cfg.reversal_pct = 0.30;   // 30% for Reversal

        DualCapitalStrategy strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        PrintResults("70/30 Split", results, initial_balance);
        std::cout << "  TrendUp entries: " << stats.trend_up_entries
                  << " | Volume: " << stats.trend_up_volume << " lots"
                  << " | First lot: " << stats.first_trend_up_lot << std::endl;
        std::cout << "  Reversal entries: " << stats.reversal_entries
                  << " | Volume: " << stats.reversal_volume << " lots"
                  << " | First lot: " << stats.first_reversal_lot << std::endl;
    }

    std::cout << std::endl;

    // Test 4: 30/70 split (favor Reversal)
    std::cout << "=== Test 4: 30/70 Split (Favor Reversal) ===" << std::endl;
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        DualCapitalStrategy::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.spacing_mult = 2.0;
        cfg.trend_up_pct = 0.30;   // 30% for TrendUp
        cfg.reversal_pct = 0.70;   // 70% for Reversal

        DualCapitalStrategy strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        PrintResults("30/70 Split", results, initial_balance);
        std::cout << "  TrendUp entries: " << stats.trend_up_entries
                  << " | Volume: " << stats.trend_up_volume << " lots"
                  << " | First lot: " << stats.first_trend_up_lot << std::endl;
        std::cout << "  Reversal entries: " << stats.reversal_entries
                  << " | Volume: " << stats.reversal_volume << " lots"
                  << " | First lot: " << stats.first_reversal_lot << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Analysis ===" << std::endl;
    std::cout << "With shared capital: whichever mode triggers first takes most margin" << std::endl;
    std::cout << "With split capital: each mode has reserved funds regardless of trigger order" << std::endl;

    return 0;
}
