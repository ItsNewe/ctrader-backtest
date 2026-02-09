/**
 * Thorough sweep of hyperbola strategy WITH trailing stop
 *
 * Trailing stop: when equity reaches a peak, set a floor at peak × (1 - trail_pct)
 * If equity drops below floor, close all positions and restart cycle
 */

#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>

using namespace backtest;

// Strategy with trailing stop
class NasdaqUpTrailing {
public:
    struct Config {
        double multiplier = 10.0;
        double power = -0.5;
        double stop_out_margin = 500.0;
        double trailing_pct = 0.0;      // 0 = disabled, e.g. 10.0 = 10% trailing stop
        double contract_size = 1.0;
        double leverage = 100.0;
        double min_volume = 0.01;
        double max_volume = 100.0;
    };

    struct Stats {
        int total_entries = 0;
        int stop_outs = 0;
        int trailing_stops = 0;
        double peak_equity = 0.0;
    };

    explicit NasdaqUpTrailing(const Config& config)
        : config_(config)
        , volume_of_open_trades_(0.0)
        , checked_last_open_price_(0.0)
        , starting_x_(0.0)
        , local_starting_room_(0.0)
        , room_(0.0)
        , first_run_(true)
        , trailing_floor_(0.0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double ask = tick.ask;
        double bid = tick.bid;
        double spread = ask - bid;

        double balance = engine.GetBalance();
        double equity = engine.GetEquity();
        const auto& positions = engine.GetOpenPositions();

        // Track peak equity for trailing stop
        if (equity > stats_.peak_equity) {
            stats_.peak_equity = equity;
            // Update trailing floor
            if (config_.trailing_pct > 0 && !positions.empty()) {
                trailing_floor_ = equity * (1.0 - config_.trailing_pct / 100.0);
            }
        }

        // Calculate margin
        double used_margin = 0.0;
        for (const Trade* t : positions) {
            used_margin += t->lot_size * config_.contract_size * t->entry_price / config_.leverage;
        }
        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        // === TRAILING STOP CHECK ===
        if (config_.trailing_pct > 0 && !positions.empty() && trailing_floor_ > 0) {
            if (equity < trailing_floor_) {
                CloseAll(engine, "TRAILING_STOP");
                stats_.trailing_stops++;
                Reset();
                return;
            }
        }

        // === MARGIN STOP-OUT CHECK ===
        if (margin_level < config_.stop_out_margin && margin_level > 0 && !positions.empty()) {
            CloseAll(engine, "MARGIN_STOP");
            stats_.stop_outs++;
            Reset();
            return;
        }

        // First run init
        if (first_run_) {
            InitFromPositions(positions);
            first_run_ = false;
        }

        // === OPEN CONDITIONS ===
        if (volume_of_open_trades_ == 0.0) {
            OpenFirst(balance, ask, spread, engine);
        }
        else if (ask > checked_last_open_price_) {
            OpenSubsequent(equity, used_margin, ask, spread, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }

private:
    Config config_;
    Stats stats_;

    double volume_of_open_trades_;
    double checked_last_open_price_;
    double starting_x_;
    double local_starting_room_;
    double room_;
    bool first_run_;
    double trailing_floor_;

    void Reset() {
        volume_of_open_trades_ = 0.0;
        checked_last_open_price_ = 0.0;
        starting_x_ = 0.0;
        local_starting_room_ = 0.0;
        room_ = 0.0;
        trailing_floor_ = 0.0;
        stats_.peak_equity = 0.0;  // Reset peak for next cycle
    }

    void InitFromPositions(const std::vector<Trade*>& positions) {
        volume_of_open_trades_ = 0.0;
        checked_last_open_price_ = 0.0;
        for (const Trade* t : positions) {
            if (t->IsBuy()) {
                volume_of_open_trades_ += t->lot_size;
                checked_last_open_price_ = std::max(checked_last_open_price_, t->entry_price);
            }
        }
    }

    void CloseAll(TickBasedEngine& engine, const std::string& reason) {
        auto positions = engine.GetOpenPositions();
        for (Trade* t : positions) {
            if (t->IsBuy()) {
                engine.ClosePosition(t, reason);
            }
        }
    }

    void OpenFirst(double balance, double ask, double spread, TickBasedEngine& engine) {
        local_starting_room_ = ask * config_.multiplier / 100.0;
        local_starting_room_ = std::max(0.01, std::min(1e9, local_starting_room_));

        double spread_cost = spread * config_.contract_size;
        double numerator = 100.0 * balance * config_.leverage;
        double denominator = 100.0 * local_starting_room_ * config_.leverage
                           + 100.0 * spread_cost * config_.leverage
                           + config_.stop_out_margin * ask;

        if (denominator <= 0) return;

        double lot_size = numerator / denominator / config_.contract_size;
        lot_size = std::max(config_.min_volume, std::min(config_.max_volume, lot_size));
        lot_size = std::round(lot_size * 100.0) / 100.0;

        if (lot_size >= config_.min_volume) {
            Trade* t = engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, 0.0);
            if (t) {
                volume_of_open_trades_ += lot_size;
                checked_last_open_price_ = ask;
                starting_x_ = ask;
                room_ = local_starting_room_;
                stats_.total_entries++;
            }
        }
    }

    void OpenSubsequent(double equity, double used_margin, double ask, double spread, TickBasedEngine& engine) {
        double price_gain = ask - starting_x_;

        if (price_gain < 1e-10) {
            room_ = local_starting_room_;
        } else {
            room_ = local_starting_room_ * std::pow(price_gain, config_.power);
            room_ = std::max(0.01, std::min(1e9, room_));
        }

        double spread_cost = spread * config_.contract_size;
        double numerator = 100.0 * equity * config_.leverage
                         - config_.leverage * config_.stop_out_margin * used_margin
                         - 100.0 * room_ * config_.leverage * volume_of_open_trades_;
        double denominator = 100.0 * room_ * config_.leverage
                           + 100.0 * spread_cost * config_.leverage
                           + config_.stop_out_margin * ask;

        if (denominator <= 0 || numerator <= 0) return;

        double lot_size = numerator / denominator / config_.contract_size;

        double free_margin = equity - used_margin;
        double margin_per_min = config_.min_volume * config_.contract_size * ask / config_.leverage;
        if (margin_per_min > 0) {
            double lot_by_margin = (free_margin / margin_per_min) * config_.min_volume;
            lot_size = std::min(lot_size, lot_by_margin);
        }

        lot_size = std::max(config_.min_volume, std::min(config_.max_volume, lot_size));
        lot_size = std::round(lot_size * 100.0) / 100.0;

        if (lot_size >= config_.min_volume) {
            Trade* t = engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, 0.0);
            if (t) {
                volume_of_open_trades_ += lot_size;
                checked_last_open_price_ = ask;
                stats_.total_entries++;
            }
        }
    }
};

struct SweepResult {
    double multiplier;
    double power;
    double stop_out;
    double trailing_pct;
    double final_balance;
    double return_pct;
    double max_drawdown_pct;
    int total_trades;
    int trailing_stops;
    int margin_stops;
};

int main() {
    std::cout << "=== Hyperbola + Trailing Stop Sweep ===" << std::endl;
    std::cout << "Testing if trailing stop improves returns\n" << std::endl;

    // Load ticks
    std::cout << "Loading NAS100 tick data..." << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\.claude-worktrees\\ctrader-backtest\\beautiful-margulis\\validation\\Grid\\NAS100_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    manager.LoadAllTicks();
    const std::vector<Tick>& ticks = manager.GetAllTicks();

    std::cout << "Loaded " << ticks.size() << " ticks\n" << std::endl;

    // Fine sweep around optimal trailing %
    std::vector<double> multipliers = {10.0};
    std::vector<double> powers = {-0.5};
    std::vector<double> stop_outs = {500.0};
    std::vector<double> trailing_pcts = {0.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 15.0};  // Fine-tune around 10%

    // Build configs
    std::vector<std::tuple<double, double, double, double>> configs;
    for (double mult : multipliers) {
        for (double power : powers) {
            for (double stop : stop_outs) {
                for (double trail : trailing_pcts) {
                    configs.push_back({mult, power, stop, trail});
                }
            }
        }
    }

    std::cout << "Testing " << configs.size() << " configurations..." << std::endl;
    std::cout << "  Multipliers: " << multipliers.size() << " values" << std::endl;
    std::cout << "  Powers: " << powers.size() << " values" << std::endl;
    std::cout << "  Stop-outs: " << stop_outs.size() << " values" << std::endl;
    std::cout << "  Trailing %: " << trailing_pcts.size() << " values (0=disabled)\n" << std::endl;

    // Results
    std::vector<SweepResult> results(configs.size());
    std::atomic<int> completed(0);
    std::mutex print_mutex;

    auto run_test = [&](size_t idx) {
        auto [mult, power, stop, trail] = configs[idx];

        TickBacktestConfig engine_config;
        engine_config.symbol = "NAS100";
        engine_config.initial_balance = 10000.0;
        engine_config.contract_size = 1.0;
        engine_config.leverage = 100.0;
        engine_config.pip_size = 0.01;
        engine_config.swap_long = -17.14;
        engine_config.swap_short = 5.76;
        engine_config.swap_mode = 5;
        engine_config.swap_3days = 5;
        engine_config.start_date = "2025.04.07";
        engine_config.end_date = "2025.10.30";
        engine_config.tick_data_config = tick_config;
        engine_config.verbose = false;

        NasdaqUpTrailing::Config strat_config;
        strat_config.multiplier = mult;
        strat_config.power = power;
        strat_config.stop_out_margin = stop;
        strat_config.trailing_pct = trail;
        strat_config.contract_size = 1.0;
        strat_config.leverage = 100.0;

        TickBasedEngine engine(engine_config);
        NasdaqUpTrailing strategy(strat_config);

        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        auto stats = strategy.GetStats();

        SweepResult sr;
        sr.multiplier = mult;
        sr.power = power;
        sr.stop_out = stop;
        sr.trailing_pct = trail;
        sr.final_balance = res.final_balance;
        sr.return_pct = (res.final_balance - 10000.0) / 10000.0 * 100.0;
        sr.max_drawdown_pct = res.max_drawdown_pct;
        sr.total_trades = res.total_trades;
        sr.trailing_stops = stats.trailing_stops;
        sr.margin_stops = stats.stop_outs;

        results[idx] = sr;

        int done = ++completed;
        if (done % 10 == 0 || done == (int)configs.size()) {
            std::lock_guard<std::mutex> lock(print_mutex);
            std::cout << "Progress: " << done << "/" << configs.size() << std::endl;
            std::cout.flush();
        }
    };

    // Sequential execution for stability
    std::cout << "Running sequentially...\n" << std::endl;

    for (size_t idx = 0; idx < configs.size(); idx++) {
        run_test(idx);
    }

    // Sort by return
    std::sort(results.begin(), results.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.final_balance > b.final_balance;
    });

    // Print top 30
    std::cout << "\n\n====== TOP 30 CONFIGURATIONS ======\n" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::setw(5) << "Rank"
              << std::setw(7) << "Mult"
              << std::setw(7) << "Power"
              << std::setw(8) << "StopOut"
              << std::setw(8) << "Trail%"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return%"
              << std::setw(9) << "MaxDD%"
              << std::setw(7) << "Trades"
              << std::setw(8) << "TrlStop"
              << std::setw(8) << "MgnStop"
              << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)results.size()); i++) {
        const auto& r = results[i];
        std::cout << std::setw(5) << (i + 1)
                  << std::setw(7) << r.multiplier
                  << std::setw(7) << r.power
                  << std::setw(8) << r.stop_out
                  << std::setw(8) << r.trailing_pct
                  << std::setw(12) << r.final_balance
                  << std::setw(10) << r.return_pct
                  << std::setw(9) << r.max_drawdown_pct
                  << std::setw(7) << r.total_trades
                  << std::setw(8) << r.trailing_stops
                  << std::setw(8) << r.margin_stops
                  << std::endl;
    }

    // Analysis by trailing %
    std::cout << "\n\n====== ANALYSIS BY TRAILING STOP % ======\n" << std::endl;
    std::cout << std::setw(10) << "Trail%"
              << std::setw(12) << "AvgRet%"
              << std::setw(12) << "BestRet%"
              << std::setw(12) << "WorstRet%"
              << std::setw(12) << "AvgMaxDD%"
              << std::setw(12) << "Profitable"
              << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (double trail : trailing_pcts) {
        double sum_ret = 0, best = -1e9, worst = 1e9, sum_dd = 0;
        int count = 0, profitable = 0;
        for (const auto& r : results) {
            if (r.trailing_pct == trail) {
                sum_ret += r.return_pct;
                sum_dd += r.max_drawdown_pct;
                best = std::max(best, r.return_pct);
                worst = std::min(worst, r.return_pct);
                count++;
                if (r.return_pct > 0) profitable++;
            }
        }
        std::cout << std::setw(10) << trail
                  << std::setw(12) << (sum_ret / count)
                  << std::setw(12) << best
                  << std::setw(12) << worst
                  << std::setw(12) << (sum_dd / count)
                  << std::setw(12) << (profitable * 100 / count) << "%"
                  << std::endl;
    }

    // Compare trailing vs no trailing for same base params
    std::cout << "\n\n====== TRAILING VS NO-TRAILING COMPARISON ======\n" << std::endl;
    std::cout << "For each (mult, power, stop_out), comparing trail=0 vs best trailing:\n" << std::endl;

    std::cout << std::setw(7) << "Mult"
              << std::setw(7) << "Power"
              << std::setw(8) << "StopOut"
              << std::setw(14) << "NoTrail$"
              << std::setw(14) << "BestTrail$"
              << std::setw(10) << "BestTrl%"
              << std::setw(12) << "Improvement"
              << std::endl;
    std::cout << std::string(72, '-') << std::endl;

    for (double mult : multipliers) {
        for (double power : powers) {
            for (double stop : stop_outs) {
                double no_trail_balance = 0;
                double best_trail_balance = 0;
                double best_trail_pct = 0;

                for (const auto& r : results) {
                    if (r.multiplier == mult && r.power == power && r.stop_out == stop) {
                        if (r.trailing_pct == 0) {
                            no_trail_balance = r.final_balance;
                        } else if (r.final_balance > best_trail_balance) {
                            best_trail_balance = r.final_balance;
                            best_trail_pct = r.trailing_pct;
                        }
                    }
                }

                double improvement = (no_trail_balance > 0)
                    ? (best_trail_balance - no_trail_balance) / no_trail_balance * 100.0
                    : 0;

                std::cout << std::setw(7) << mult
                          << std::setw(7) << power
                          << std::setw(8) << stop
                          << std::setw(14) << no_trail_balance
                          << std::setw(14) << best_trail_balance
                          << std::setw(10) << best_trail_pct
                          << std::setw(12) << improvement << "%"
                          << std::endl;
            }
        }
    }

    // Summary
    std::cout << "\n\n====== SUMMARY ======\n" << std::endl;

    int profitable = 0;
    double total_ret = 0;
    for (const auto& r : results) {
        if (r.return_pct > 0) profitable++;
        total_ret += r.return_pct;
    }

    std::cout << "Total configs: " << results.size() << std::endl;
    std::cout << "Profitable: " << profitable << " (" << (profitable * 100.0 / results.size()) << "%)" << std::endl;
    std::cout << "Average return: " << (total_ret / results.size()) << "%" << std::endl;
    std::cout << "\nBest overall: mult=" << results[0].multiplier
              << ", power=" << results[0].power
              << ", stop=" << results[0].stop_out
              << ", trail=" << results[0].trailing_pct
              << " -> $" << results[0].final_balance
              << " (" << results[0].return_pct << "%)" << std::endl;

    return 0;
}
