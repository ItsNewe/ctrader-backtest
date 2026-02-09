#include "../include/strategy_triple_hybrid.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

// Custom strategy to track lot sizes per mode
class DiagnosticHybrid {
public:
    struct Config {
        double survive_pct = 13.0;
        double base_spacing = 1.50;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double spacing_mult = 2.0;
    };

    struct Stats {
        double total_trend_up_volume = 0.0;
        double total_reversal_volume = 0.0;
        int trend_up_entries = 0;
        int reversal_entries = 0;
        double max_trend_up_lot = 0.0;
        double max_reversal_lot = 0.0;
        double first_trend_up_lot = 0.0;
        double first_reversal_lot = 0.0;
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
    double total_volume_ = 0.0;
    double current_spacing_ = 0.0;

public:
    DiagnosticHybrid(const Config& config) : config_(config) {
        current_spacing_ = config_.base_spacing;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double spread = tick.ask - tick.bid;
        UpdateDirection(tick, spread);
        UpdateVolume(engine);

        double lots = CalculateLotSize(tick, engine);

        if (direction_ == 1) {
            ExecuteTrendUp(tick, engine, lots);
        } else if (direction_ == -1) {
            ExecuteReversal(tick, engine, lots);
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
        total_volume_ = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                total_volume_ += trade->lot_size;
            }
        }
    }

    double CalculateLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_distance = tick.ask * (config_.survive_pct / 100.0);
        double potential_loss = total_volume_ * survive_distance * config_.contract_size;
        double available_equity = equity - potential_loss;

        if (available_equity <= 0) return 0.0;

        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double lots = (available_equity * 0.5) / margin_per_lot;
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;

        return std::max(lots, 0.0);
    }

    void ExecuteTrendUp(const Tick& tick, TickBasedEngine& engine, double lots) {
        if (lots < config_.min_volume) return;

        double trend_spacing = current_spacing_ * config_.spacing_mult;
        bool should_open = (last_trend_up_entry_ == 0.0) ||
                          (tick.ask >= last_trend_up_entry_ + trend_spacing);

        if (should_open) {
            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
            if (trade) {
                if (stats_.trend_up_entries == 0) {
                    stats_.first_trend_up_lot = lots;
                }
                stats_.trend_up_entries++;
                stats_.total_trend_up_volume += lots;
                stats_.max_trend_up_lot = std::max(stats_.max_trend_up_lot, lots);
                last_trend_up_entry_ = tick.ask;
            }
        }
    }

    void ExecuteReversal(const Tick& tick, TickBasedEngine& engine, double lots) {
        if (lots < config_.min_volume) return;

        double reversal_spacing = current_spacing_ * config_.spacing_mult;
        bool should_open = (last_reversal_entry_ >= DBL_MAX) ||
                          (tick.ask <= last_reversal_entry_ - reversal_spacing);

        if (should_open) {
            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
            if (trade) {
                if (stats_.reversal_entries == 0) {
                    stats_.first_reversal_lot = lots;
                }
                stats_.reversal_entries++;
                stats_.total_reversal_volume += lots;
                stats_.max_reversal_lot = std::max(stats_.max_reversal_lot, lots);
                last_reversal_entry_ = tick.ask;
            }
        }
    }
};

int main() {
    std::cout << "=== Margin Allocation Diagnostic ===" << std::endl;
    std::cout << "Testing from 2025.10.17 to see lot size distribution" << std::endl;
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

    // Test: Combined strategy
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        DiagnosticHybrid::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.spacing_mult = 2.0;

        DiagnosticHybrid strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Final Balance: $" << results.final_balance
                  << " (" << (results.final_balance / initial_balance) << "x)" << std::endl;
        std::cout << "Max DD: " << results.max_drawdown_pct << "%" << std::endl;
        std::cout << std::endl;

        std::cout << "=== TrendUp (Up While Up) ===" << std::endl;
        std::cout << "  Entries: " << stats.trend_up_entries << std::endl;
        std::cout << "  Total Volume: " << stats.total_trend_up_volume << " lots" << std::endl;
        std::cout << "  First Entry Lot: " << stats.first_trend_up_lot << std::endl;
        std::cout << "  Max Lot Size: " << stats.max_trend_up_lot << std::endl;
        if (stats.trend_up_entries > 0) {
            std::cout << "  Avg Lot Size: " << (stats.total_trend_up_volume / stats.trend_up_entries) << std::endl;
        }
        std::cout << std::endl;

        std::cout << "=== Reversal (Up While Down) ===" << std::endl;
        std::cout << "  Entries: " << stats.reversal_entries << std::endl;
        std::cout << "  Total Volume: " << stats.total_reversal_volume << " lots" << std::endl;
        std::cout << "  First Entry Lot: " << stats.first_reversal_lot << std::endl;
        std::cout << "  Max Lot Size: " << stats.max_reversal_lot << std::endl;
        if (stats.reversal_entries > 0) {
            std::cout << "  Avg Lot Size: " << (stats.total_reversal_volume / stats.reversal_entries) << std::endl;
        }
        std::cout << std::endl;

        double total_volume = stats.total_trend_up_volume + stats.total_reversal_volume;
        std::cout << "=== Volume Distribution ===" << std::endl;
        std::cout << "  TrendUp: " << (stats.total_trend_up_volume / total_volume * 100) << "%" << std::endl;
        std::cout << "  Reversal: " << (stats.total_reversal_volume / total_volume * 100) << "%" << std::endl;
    }

    return 0;
}
