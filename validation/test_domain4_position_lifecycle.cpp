/**
 * Domain 4: Position Lifecycle Analysis
 *
 * Simulate the strategy and analyze:
 * - Time-to-TP distribution
 * - Holding time percentiles
 * - Recovery patterns for underwater positions
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <cfloat>

typedef unsigned long long ulong;

using namespace backtest;

struct PositionLifecycle {
    long open_tick;
    long close_tick;
    double entry_price;
    double exit_price;
    double max_drawdown;      // Max negative excursion
    double max_favorable;     // Max positive excursion
    double lot_size;
    bool hit_tp;
};

class PositionLifecycleAnalyzer {
public:
    void Analyze() {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "POSITION LIFECYCLE ANALYSIS" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        TickDataConfig tick_config;
        tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;
        tick_config.load_all_into_memory = false;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.account_currency = "USD";
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.margin_rate = 1.0;
        config.pip_size = 0.01;
        config.swap_long = -66.99;
        config.swap_short = 41.2;
        config.swap_mode = 1;
        config.swap_3days = 3;
        config.start_date = "2025.01.01";
        config.end_date = "2025.12.30";
        config.tick_data_config = tick_config;

        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,   // survive_pct
            1.0,    // base_spacing
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1, 30.0, 1.0
        );

        // Track position lifecycles
        std::map<ulong, PositionLifecycle> active_positions;
        std::vector<PositionLifecycle> completed_positions;

        long tick_count = 0;
        ulong next_id = 1;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;

            // Track open positions
            for (const Trade* trade : eng.GetOpenPositions()) {
                ulong id = (ulong)trade;
                if (active_positions.find(id) == active_positions.end()) {
                    PositionLifecycle pl;
                    pl.open_tick = tick_count;
                    pl.entry_price = trade->entry_price;
                    pl.lot_size = trade->lot_size;
                    pl.max_drawdown = 0;
                    pl.max_favorable = 0;
                    active_positions[id] = pl;
                } else {
                    // Update excursions
                    auto& pl = active_positions[id];
                    double current_pnl = (tick.bid - pl.entry_price) * config.contract_size * pl.lot_size;
                    if (current_pnl < pl.max_drawdown) pl.max_drawdown = current_pnl;
                    if (current_pnl > pl.max_favorable) pl.max_favorable = current_pnl;
                }
            }

            // Check for closed positions
            std::vector<ulong> to_remove;
            for (auto& [id, pl] : active_positions) {
                bool still_open = false;
                for (const Trade* trade : eng.GetOpenPositions()) {
                    if ((ulong)trade == id) {
                        still_open = true;
                        break;
                    }
                }
                if (!still_open) {
                    pl.close_tick = tick_count;
                    pl.exit_price = tick.bid;
                    pl.hit_tp = true;  // Assume TP hit if closed
                    completed_positions.push_back(pl);
                    to_remove.push_back(id);
                }
            }
            for (ulong id : to_remove) {
                active_positions.erase(id);
            }

            // Run strategy
            strategy.OnTick(tick, eng);

            if (tick_count % 10000000 == 0) {
                std::cout << "Tick " << tick_count / 1000000 << "M, "
                          << completed_positions.size() << " trades completed..." << std::endl;
            }
        });

        // Analysis
        if (completed_positions.empty()) {
            std::cout << "No completed positions to analyze!" << std::endl;
            return;
        }

        std::cout << "\n=== POSITION COUNT ===" << std::endl;
        std::cout << "Total completed positions: " << completed_positions.size() << std::endl;
        std::cout << "Positions still open at end: " << active_positions.size() << std::endl;

        // Holding time distribution
        std::vector<long> holding_times;
        for (const auto& pl : completed_positions) {
            holding_times.push_back(pl.close_tick - pl.open_tick);
        }
        std::sort(holding_times.begin(), holding_times.end());

        double ticks_per_minute = 200.0 * 60;

        std::cout << "\n=== HOLDING TIME DISTRIBUTION ===" << std::endl;
        double mean_ticks = 0;
        for (long t : holding_times) mean_ticks += t;
        mean_ticks /= holding_times.size();

        std::cout << "Mean: " << std::fixed << std::setprecision(1)
                  << mean_ticks / ticks_per_minute << " minutes" << std::endl;
        std::cout << "Median: " << holding_times[holding_times.size()/2] / ticks_per_minute << " minutes" << std::endl;
        std::cout << "10th percentile: " << holding_times[holding_times.size()/10] / ticks_per_minute << " min" << std::endl;
        std::cout << "25th percentile: " << holding_times[holding_times.size()/4] / ticks_per_minute << " min" << std::endl;
        std::cout << "75th percentile: " << holding_times[holding_times.size()*3/4] / ticks_per_minute << " min" << std::endl;
        std::cout << "90th percentile: " << holding_times[holding_times.size()*9/10] / ticks_per_minute << " min" << std::endl;
        std::cout << "95th percentile: " << holding_times[holding_times.size()*95/100] / ticks_per_minute << " min" << std::endl;
        std::cout << "99th percentile: " << holding_times[holding_times.size()*99/100] / ticks_per_minute << " min" << std::endl;
        std::cout << "Max: " << holding_times.back() / ticks_per_minute << " minutes ("
                  << holding_times.back() / ticks_per_minute / 60 << " hours)" << std::endl;

        // Holding time histogram
        std::cout << "\n=== HOLDING TIME HISTOGRAM ===" << std::endl;
        std::map<int, int> time_hist;
        for (long t : holding_times) {
            int bucket = (int)(t / ticks_per_minute / 5);  // 5-minute buckets
            time_hist[bucket]++;
        }

        for (const auto& [bucket, count] : time_hist) {
            if (bucket <= 24) {  // Up to 2 hours
                double pct = 100.0 * count / holding_times.size();
                std::cout << std::setw(3) << bucket * 5 << "-" << std::setw(3) << (bucket + 1) * 5 << " min: "
                          << std::setw(6) << count << " (" << std::setw(5) << std::setprecision(1) << pct << "%) "
                          << std::string((int)(pct / 2), '#') << std::endl;
            }
        }

        // Long-tail analysis
        int over_1h = 0, over_4h = 0, over_24h = 0;
        for (long t : holding_times) {
            double hours = t / ticks_per_minute / 60;
            if (hours > 1) over_1h++;
            if (hours > 4) over_4h++;
            if (hours > 24) over_24h++;
        }
        std::cout << "\n=== LONG-TAIL POSITIONS ===" << std::endl;
        std::cout << "Over 1 hour: " << over_1h << " (" << std::setprecision(2)
                  << 100.0 * over_1h / holding_times.size() << "%)" << std::endl;
        std::cout << "Over 4 hours: " << over_4h << " (" << 100.0 * over_4h / holding_times.size() << "%)" << std::endl;
        std::cout << "Over 24 hours: " << over_24h << " (" << 100.0 * over_24h / holding_times.size() << "%)" << std::endl;

        // Max adverse excursion analysis
        std::cout << "\n=== MAX ADVERSE EXCURSION (MAE) ===" << std::endl;
        std::vector<double> maes;
        for (const auto& pl : completed_positions) {
            maes.push_back(std::abs(pl.max_drawdown));
        }
        std::sort(maes.begin(), maes.end());

        double mean_mae = 0;
        for (double m : maes) mean_mae += m;
        mean_mae /= maes.size();

        std::cout << "Mean MAE: $" << std::setprecision(2) << mean_mae << std::endl;
        std::cout << "Median MAE: $" << maes[maes.size()/2] << std::endl;
        std::cout << "90th percentile: $" << maes[maes.size()*9/10] << std::endl;
        std::cout << "99th percentile: $" << maes[maes.size()*99/100] << std::endl;
        std::cout << "Max MAE: $" << maes.back() << std::endl;

        // Max favorable excursion analysis
        std::cout << "\n=== MAX FAVORABLE EXCURSION (MFE) ===" << std::endl;
        std::vector<double> mfes;
        for (const auto& pl : completed_positions) {
            mfes.push_back(pl.max_favorable);
        }
        std::sort(mfes.begin(), mfes.end());

        double mean_mfe = 0;
        for (double m : mfes) mean_mfe += m;
        mean_mfe /= mfes.size();

        std::cout << "Mean MFE: $" << mean_mfe << std::endl;
        std::cout << "Median MFE: $" << mfes[mfes.size()/2] << std::endl;

        // Recommendations
        std::cout << "\n=== LIFECYCLE INSIGHTS ===" << std::endl;
        double pct_quick = 100.0 * (holding_times.size() -
            std::count_if(holding_times.begin(), holding_times.end(),
                [&](long t) { return t > ticks_per_minute * 30; })) / holding_times.size();
        std::cout << "1. " << std::setprecision(1) << pct_quick << "% of positions close within 30 minutes" << std::endl;
        std::cout << "2. Positions open > 4 hours may indicate regime problem" << std::endl;
        std::cout << "3. Consider time-based exit for positions > " << holding_times[holding_times.size()*99/100] / ticks_per_minute / 60 << " hours" << std::endl;
    }
};

int main() {
    PositionLifecycleAnalyzer analyzer;
    analyzer.Analyze();
    return 0;
}
