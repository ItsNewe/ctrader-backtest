/**
 * Survive Capacity Diagnostic
 *
 * At each trade entry, calculate: "What % drop could we survive right now?"
 * This reveals if survival capacity fluctuates and whether by luck
 * we happen to have higher capacity right before crashes.
 */

#include "../include/strategy_parallel_dual_original.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cmath>

using namespace backtest;

struct TradeSnapshot {
    std::string timestamp;
    double price;
    double equity;
    double used_margin;
    double total_volume;
    double survive_capacity_pct;  // What % drop could we survive?
    double actual_drop_to_next_low;  // Actual drop that followed
    int trade_number;
};

class DiagnosticStrategy {
public:
    struct Config {
        double grid_allocation;
        double momentum_allocation;
        double min_volume;
        double max_volume;
        double contract_size;
        double leverage;
        double base_spacing;
        double momentum_spacing;
        double tp_distance;
        double margin_stop_out;

        Config() : grid_allocation(0.15), momentum_allocation(0.85),
                   min_volume(0.01), max_volume(5.0), contract_size(5000.0),
                   leverage(500.0), base_spacing(0.10), momentum_spacing(1.0),
                   tp_distance(0.30), margin_stop_out(20.0) {}
    };

    std::vector<TradeSnapshot> snapshots;

    explicit DiagnosticStrategy(const Config& config = Config())
        : config_(config), initialized_(false), last_momentum_entry_(0.0),
          grid_floor_(0.0), trade_count_(0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!initialized_) {
            Initialize(tick, engine);
            return;
        }

        // Try momentum entry
        if (tick.ask > last_momentum_entry_ + config_.momentum_spacing) {
            double lot = CalculateLotSize(tick, engine, config_.momentum_allocation);
            if (lot >= config_.min_volume && HasSufficientMargin(tick, engine, lot)) {
                double tp = tick.ask + config_.tp_distance;
                Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, tp);
                if (trade) {
                    last_momentum_entry_ = tick.ask;
                    trade_count_++;
                    RecordSnapshot(tick, engine);
                }
            }
        }

        // Try grid entry (on dips)
        if (tick.ask < grid_floor_ - config_.base_spacing) {
            double lot = CalculateLotSize(tick, engine, config_.grid_allocation);
            if (lot >= config_.min_volume && HasSufficientMargin(tick, engine, lot)) {
                double tp = tick.ask + config_.tp_distance;
                Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, tp);
                if (trade) {
                    grid_floor_ = tick.ask;
                    trade_count_++;
                    RecordSnapshot(tick, engine);
                }
            }
        }
    }

    void RecordSnapshot(const Tick& tick, TickBasedEngine& engine) {
        TradeSnapshot snap;
        snap.timestamp = tick.timestamp;
        snap.price = tick.ask;
        snap.equity = engine.GetEquity();
        snap.trade_number = trade_count_;

        // Calculate used margin and total volume
        double used_margin = 0.0;
        double total_volume = 0.0;
        double weighted_entry = 0.0;

        for (const Trade* t : engine.GetOpenPositions()) {
            double margin = t->lot_size * config_.contract_size * t->entry_price / config_.leverage;
            used_margin += margin;
            total_volume += t->lot_size;
            weighted_entry += t->lot_size * t->entry_price;
        }

        snap.used_margin = used_margin;
        snap.total_volume = total_volume;

        // Calculate survive capacity:
        // If price drops by X%, what's our margin level?
        // margin_level = equity / used_margin * 100
        // At stop-out: margin_level = 20%
        //
        // PnL from drop = total_volume * contract_size * price_drop
        // New equity = equity - PnL
        // We survive if: (equity - PnL) / used_margin * 100 > 20
        //
        // Solve for max price_drop:
        // equity - total_volume * contract_size * price_drop > used_margin * 0.20
        // price_drop < (equity - used_margin * 0.20) / (total_volume * contract_size)

        if (total_volume > 0) {
            double max_drop_dollars = (snap.equity - used_margin * 0.20) / (total_volume * config_.contract_size);
            snap.survive_capacity_pct = (max_drop_dollars / tick.ask) * 100.0;
        } else {
            snap.survive_capacity_pct = 100.0;  // No positions = infinite survival
        }

        snap.actual_drop_to_next_low = 0.0;  // Will be filled in post-processing

        snapshots.push_back(snap);
    }

    void PostProcess(const std::vector<Tick>& ticks) {
        // For each snapshot, find the max drop that occurred after it
        for (auto& snap : snapshots) {
            double min_price_after = snap.price;

            // Find minimum price after this snapshot
            bool found_start = false;
            for (const auto& tick : ticks) {
                if (!found_start) {
                    if (tick.timestamp >= snap.timestamp) {
                        found_start = true;
                    }
                    continue;
                }
                if (tick.bid < min_price_after) {
                    min_price_after = tick.bid;
                }
            }

            double drop = snap.price - min_price_after;
            snap.actual_drop_to_next_low = (drop / snap.price) * 100.0;
        }
    }

private:
    Config config_;
    bool initialized_;
    double last_momentum_entry_;
    double grid_floor_;
    int trade_count_;

    void Initialize(const Tick& tick, TickBasedEngine& engine) {
        last_momentum_entry_ = tick.ask;
        grid_floor_ = tick.ask;

        double lot = CalculateLotSize(tick, engine, config_.momentum_allocation);
        if (lot >= config_.min_volume) {
            double tp = tick.ask + config_.tp_distance;
            Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, tp);
            if (trade) {
                trade_count_++;
                RecordSnapshot(tick, engine);
            }
        }
        initialized_ = true;
    }

    double CalculateLotSize(const Tick& tick, TickBasedEngine& engine, double allocation) {
        double equity = engine.GetEquity();
        double margin_per_lot = tick.ask * config_.contract_size / config_.leverage;
        double risk_factor = 0.05 * allocation;
        double lot = (equity * risk_factor) / margin_per_lot;

        double used_margin = 0.0;
        for (const Trade* t : engine.GetOpenPositions()) {
            used_margin += t->lot_size * config_.contract_size * t->entry_price / config_.leverage;
        }
        double free_margin = equity - used_margin;
        double max_lot = (free_margin * 0.5) / margin_per_lot;
        lot = std::min(lot, max_lot);

        lot = std::max(lot, config_.min_volume);
        lot = std::min(lot, config_.max_volume);
        lot = std::floor(lot * 100.0) / 100.0;

        return lot;
    }

    bool HasSufficientMargin(const Tick& tick, TickBasedEngine& engine, double lot) {
        double equity = engine.GetEquity();
        double used_margin = 0.0;
        for (const Trade* t : engine.GetOpenPositions()) {
            used_margin += t->lot_size * config_.contract_size * t->entry_price / config_.leverage;
        }
        double new_margin = lot * config_.contract_size * tick.ask / config_.leverage;
        double total_margin = used_margin + new_margin;
        if (total_margin <= 0) return true;
        double margin_level = (equity / total_margin) * 100.0;
        return margin_level > config_.margin_stop_out * 2.0;
    }
};

std::vector<Tick> LoadTicks(const std::string& file_path) {
    std::vector<Tick> ticks;
    std::ifstream file(file_path);
    if (!file.is_open()) return ticks;

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Tick tick;
        size_t pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);
        line = line.substr(pos + 1);

        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.bid = std::stod(line.substr(0, pos));
        line = line.substr(pos + 1);

        pos = line.find('\t');
        if (pos == std::string::npos) pos = line.length();
        tick.ask = std::stod(line.substr(0, pos));

        ticks.push_back(tick);
    }
    return ticks;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "   Survive Capacity Diagnostic - XAGUSD" << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "\nLoading MT5 tick data..." << std::endl;
    auto ticks = LoadTicks(
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_MT5_EXPORT.csv");
    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.pip_size = 0.001;
    config.swap_long = -22.34;
    config.swap_short = 0.13;
    config.start_date = "2024.12.31";
    config.end_date = "2026.01.30";
    config.verbose = false;

    TickBasedEngine engine(config);
    DiagnosticStrategy strategy;

    std::cout << "Running backtest with diagnostic tracking..." << std::endl;

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    std::cout << "Post-processing: calculating actual drops after each entry..." << std::endl;
    strategy.PostProcess(ticks);

    auto results = engine.GetResults();
    std::cout << "\nFinal balance: $" << std::fixed << std::setprecision(2) << results.final_balance << std::endl;
    std::cout << "Max DD: " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "Total snapshots: " << strategy.snapshots.size() << std::endl;

    // Print analysis
    std::cout << "\n=== SURVIVE CAPACITY AT EACH ENTRY ===" << std::endl;
    std::cout << std::setw(6) << "#"
              << std::setw(24) << "Timestamp"
              << std::setw(10) << "Price"
              << std::setw(14) << "Equity"
              << std::setw(10) << "Volume"
              << std::setw(12) << "Survive%"
              << std::setw(12) << "ActualDrop%"
              << std::setw(10) << "Safe?"
              << std::endl;
    std::cout << std::string(108, '-') << std::endl;

    int close_calls = 0;
    int disasters = 0;

    for (const auto& snap : strategy.snapshots) {
        bool safe = snap.survive_capacity_pct > snap.actual_drop_to_next_low;
        std::string status = safe ? "OK" : "FAIL";

        if (snap.survive_capacity_pct < snap.actual_drop_to_next_low + 2.0 && safe) {
            status = "CLOSE";
            close_calls++;
        }
        if (!safe) {
            disasters++;
        }

        std::cout << std::setw(6) << snap.trade_number
                  << std::setw(24) << snap.timestamp
                  << std::setw(10) << std::fixed << std::setprecision(3) << snap.price
                  << std::setw(14) << std::setprecision(2) << snap.equity
                  << std::setw(10) << std::setprecision(2) << snap.total_volume
                  << std::setw(12) << std::setprecision(2) << snap.survive_capacity_pct
                  << std::setw(12) << std::setprecision(2) << snap.actual_drop_to_next_low
                  << std::setw(10) << status
                  << std::endl;
    }

    std::cout << "\n=== SUMMARY ===" << std::endl;
    std::cout << "Close calls (within 2% of failure): " << close_calls << std::endl;
    std::cout << "Disasters (would have failed): " << disasters << std::endl;

    // Find the minimum survive capacity
    double min_capacity = 1000.0;
    double max_actual_drop = 0.0;
    TradeSnapshot worst_snap;

    for (const auto& snap : strategy.snapshots) {
        if (snap.survive_capacity_pct < min_capacity) {
            min_capacity = snap.survive_capacity_pct;
            worst_snap = snap;
        }
        if (snap.actual_drop_to_next_low > max_actual_drop) {
            max_actual_drop = snap.actual_drop_to_next_low;
        }
    }

    std::cout << "\nLowest survive capacity: " << std::setprecision(2) << min_capacity << "% at trade #" << worst_snap.trade_number << std::endl;
    std::cout << "Largest actual drop in data: " << max_actual_drop << "%" << std::endl;

    return 0;
}
