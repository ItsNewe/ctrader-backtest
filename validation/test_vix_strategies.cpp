/**
 * VIX Strategy Comparison - Spike Fade vs SHORT-Only Grid
 *
 * VIX specs (Broker, queried 2026-02-08):
 *   trade_calc_mode = 2 (CFD_INDEX)
 *   contract_size = 1000, digits = 3, point = 0.001
 *   leverage = 500, swap = 0 (disabled, rollover via price gaps)
 *   $1 move per 0.01 lot = $10 P/L
 *   margin per 0.01 lot @ $19 = $0.38
 *   spread: ~$0.083 (83 points)
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <atomic>

using namespace backtest;

// ============================================================================
// Strategy 1: Spike Fade (Mean-Reversion SHORT)
// ============================================================================

struct SpikeFadeConfig {
    double entry_threshold = 25.0;
    double scale_in_step = 3.0;
    double tp_target = 18.0;
    double survive_pct = 50.0;
    double max_total_lots = 5.0;
    double min_lot = 0.01;
    double contract_size = 1000.0;
    double leverage = 500.0;
};

class SpikeFade {
public:
    SpikeFade(const SpikeFadeConfig& cfg) : config_(cfg) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double bid = tick.bid;
        double ask = tick.ask;
        const auto& positions = engine.GetOpenPositions();

        double total_lots = 0;
        double highest_entry = 0;
        int short_count = 0;

        for (const auto* trade : positions) {
            if (trade->direction == TradeDirection::SELL) {
                total_lots += trade->lot_size;
                highest_entry = std::max(highest_entry, trade->entry_price);
                short_count++;
            }
        }

        // TP: Close all when VIX drops to target
        if (short_count > 0 && ask <= config_.tp_target) {
            std::vector<Trade*> to_close;
            for (auto* trade : positions) {
                if (trade->direction == TradeDirection::SELL)
                    to_close.push_back(trade);
            }
            for (auto* trade : to_close)
                engine.ClosePosition(trade, "TP_TARGET");
            return;
        }

        // ENTRY: Short when VIX > threshold
        if (bid >= config_.entry_threshold && total_lots < config_.max_total_lots) {
            bool should_enter = false;
            if (short_count == 0) {
                should_enter = true;
            } else if (bid >= highest_entry + config_.scale_in_step) {
                should_enter = true;
            }

            if (should_enter) {
                double lot = CalcLot(bid, engine, total_lots);
                if (lot >= config_.min_lot)
                    engine.OpenMarketOrder(TradeDirection::SELL, lot, 0.0, 0.0);
            }
        }
    }

private:
    SpikeFadeConfig config_;

    double CalcLot(double price, TickBasedEngine& engine, double current_lots) {
        double equity = engine.GetEquity();
        double worst_price = price * (1.0 + config_.survive_pct / 100.0);
        double adverse = worst_price - price;
        double existing_loss = current_lots * config_.contract_size * adverse;
        double available = equity - existing_loss;
        if (available <= 0) return 0;

        double margin_at_worst = (current_lots + config_.min_lot) * config_.contract_size * worst_price / config_.leverage;
        double target_equity = margin_at_worst * 0.3;
        double max_loss = available - target_equity;
        if (max_loss <= 0) return 0;

        double lot = max_loss / (config_.contract_size * adverse);
        lot = std::max(config_.min_lot, lot);
        lot = std::min(lot, config_.max_total_lots - current_lots);
        lot = std::floor(lot / config_.min_lot) * config_.min_lot;
        return std::max(0.0, lot);
    }
};

// ============================================================================
// Strategy 2: SHORT-Only Grid
// ============================================================================

struct ShortGridConfig {
    double spacing = 0.50;
    double activate_above = 20.0;
    double close_below = 16.0;
    double survive_pct = 50.0;
    double max_total_lots = 5.0;
    double min_lot = 0.01;
    double contract_size = 1000.0;
    double leverage = 500.0;
};

class ShortOnlyGrid {
public:
    ShortOnlyGrid(const ShortGridConfig& cfg) : config_(cfg) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double bid = tick.bid;
        double ask = tick.ask;
        const auto& positions = engine.GetOpenPositions();

        double total_lots = 0;
        double highest_entry = 0;
        double lowest_entry = 1e9;
        int short_count = 0;

        for (const auto* trade : positions) {
            if (trade->direction == TradeDirection::SELL) {
                total_lots += trade->lot_size;
                highest_entry = std::max(highest_entry, trade->entry_price);
                lowest_entry = std::min(lowest_entry, trade->entry_price);
                short_count++;
            }
        }

        // CLOSE ALL when VIX drops below deactivation
        if (short_count > 0 && ask <= config_.close_below) {
            std::vector<Trade*> to_close;
            for (auto* trade : positions) {
                if (trade->direction == TradeDirection::SELL)
                    to_close.push_back(trade);
            }
            for (auto* trade : to_close)
                engine.ClosePosition(trade, "DEACTIVATE");
            return;
        }

        // Individual TP: entry - spacing - spread
        {
            double spread = ask - bid;
            std::vector<Trade*> to_close;
            for (auto* trade : positions) {
                if (trade->direction == TradeDirection::SELL) {
                    double tp_price = trade->entry_price - config_.spacing - spread;
                    if (ask <= tp_price)
                        to_close.push_back(trade);
                }
            }
            for (auto* trade : to_close)
                engine.ClosePosition(trade, "GRID_TP");

            if (!to_close.empty()) {
                // Recount
                total_lots = 0; highest_entry = 0; lowest_entry = 1e9; short_count = 0;
                for (const auto* trade : engine.GetOpenPositions()) {
                    if (trade->direction == TradeDirection::SELL) {
                        total_lots += trade->lot_size;
                        highest_entry = std::max(highest_entry, trade->entry_price);
                        lowest_entry = std::min(lowest_entry, trade->entry_price);
                        short_count++;
                    }
                }
            }
        }

        // ENTRY
        if (bid < config_.activate_above) return;
        if (total_lots >= config_.max_total_lots) return;

        bool should_open = false;
        if (short_count == 0) {
            should_open = true;
        } else {
            if (bid >= highest_entry + config_.spacing) should_open = true;
            if (bid <= lowest_entry - config_.spacing && bid >= config_.activate_above) should_open = true;
        }

        if (should_open) {
            double lot = CalcLot(bid, engine, total_lots, short_count);
            if (lot >= config_.min_lot)
                engine.OpenMarketOrder(TradeDirection::SELL, lot, 0.0, 0.0);
        }
    }

private:
    ShortGridConfig config_;

    double CalcLot(double price, TickBasedEngine& engine, double current_lots, int pos_count) {
        double equity = engine.GetEquity();
        double worst_price = price * (1.0 + config_.survive_pct / 100.0);
        double adverse = worst_price - price;
        double num_future = std::max(1.0, adverse / config_.spacing);

        double existing_loss = current_lots * config_.contract_size * adverse;
        double available = equity - existing_loss;
        if (available <= 0) return 0;

        double per_pos_budget = available / std::max(1.0, num_future + 1);
        double lot = per_pos_budget / (config_.contract_size * adverse);

        // Margin floor
        double margin_at_worst = (current_lots + lot) * config_.contract_size * worst_price / config_.leverage;
        double equity_at_worst = equity - (current_lots + lot) * config_.contract_size * adverse;
        if (margin_at_worst > 0 && (equity_at_worst / margin_at_worst * 100.0) < 30.0)
            lot = config_.min_lot;

        lot = std::max(config_.min_lot, lot);
        lot = std::min(lot, config_.max_total_lots - current_lots);
        lot = std::floor(lot / config_.min_lot) * config_.min_lot;
        return std::max(0.0, lot);
    }
};

// ============================================================================
// Fast Tick Loading (same as test_dual_tp_sweep.cpp)
// ============================================================================

std::vector<Tick> LoadTicks(const std::string& file_path, const std::string& start_date, const std::string& end_date) {
    std::vector<Tick> ticks;
    ticks.reserve(2000000);

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << file_path << std::endl;
        return ticks;
    }

    std::string line;
    line.reserve(256);
    std::getline(file, line);  // skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        size_t pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);

        std::string date_str = tick.timestamp.substr(0, 10);
        if (!start_date.empty() && date_str < start_date) continue;
        if (!end_date.empty() && date_str > end_date) continue;

        size_t pos2 = line.find('\t', pos + 1);
        if (pos2 == std::string::npos) continue;
        tick.bid = std::stod(line.substr(pos + 1, pos2 - pos - 1));

        size_t pos3 = line.find('\t', pos2 + 1);
        if (pos3 == std::string::npos) pos3 = line.length();
        tick.ask = std::stod(line.substr(pos2 + 1, pos3 - pos2 - 1));

        ticks.push_back(tick);
    }

    return ticks;
}

// ============================================================================
// Job definitions (no std::function — direct config storage)
// ============================================================================

enum class StrategyType { SPIKE_FADE, SHORT_GRID };

struct Job {
    StrategyType type;
    std::string params;
    SpikeFadeConfig sf_cfg;
    ShortGridConfig sg_cfg;
};

struct TestResult {
    std::string strategy;
    std::string params;
    double final_balance = 0;
    double return_mult = 0;
    double max_dd_pct = 0;
    int total_trades = 0;
    bool stop_out = false;
};

TestResult RunJob(const Job& job, const std::vector<Tick>& ticks,
                  const std::string& start_date, const std::string& end_date) {
    // Engine config — same pattern as test_engine_xagusd.cpp
    TickDataConfig tick_config;
    tick_config.file_path = "";  // Empty - using shared pre-loaded ticks
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig ecfg;
    ecfg.symbol = "VIX";
    ecfg.initial_balance = 10000.0;
    ecfg.account_currency = "USD";
    ecfg.contract_size = 1000.0;
    ecfg.leverage = 500.0;
    ecfg.margin_rate = 1.0;
    ecfg.pip_size = 0.001;
    ecfg.digits = 3;
    ecfg.swap_long = 0.0;
    ecfg.swap_short = 0.0;
    ecfg.swap_mode = 0;
    ecfg.swap_3days = 5;
    ecfg.start_date = start_date;
    ecfg.end_date = end_date;
    ecfg.verbose = false;
    ecfg.tick_data_config = tick_config;

    TickBasedEngine engine(ecfg);

    if (job.type == StrategyType::SPIKE_FADE) {
        SpikeFade strategy(job.sf_cfg);
        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });
    } else {
        ShortOnlyGrid strategy(job.sg_cfg);
        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });
    }

    auto res = engine.GetResults();

    TestResult tr;
    tr.strategy = (job.type == StrategyType::SPIKE_FADE) ? "SpikeFade" : "ShortGrid";
    tr.params = job.params;
    tr.final_balance = res.final_balance;
    tr.return_mult = res.final_balance / 10000.0;
    tr.max_dd_pct = res.max_drawdown_pct;
    tr.total_trades = res.total_trades;
    tr.stop_out = res.stop_out_occurred;
    return tr;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "===========================================================" << std::endl;
    std::cout << "   VIX Strategy Comparison" << std::endl;
    std::cout << "   Spike Fade vs SHORT-Only Grid" << std::endl;
    std::cout << "   Balance: $10,000 | VIX ticks: 2024.01 - 2026.02" << std::endl;
    std::cout << "===========================================================" << std::endl;

    std::string tick_file = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\VIX_TICKS_FULL.csv";
    std::string start_date = "2024.01.01";
    std::string end_date = "2026.02.28";

    // Fast tick loading (same as test_dual_tp_sweep.cpp)
    std::cout << "\nLoading ticks..." << std::flush;
    auto load_start = std::chrono::steady_clock::now();
    std::vector<Tick> ticks = LoadTicks(tick_file, start_date, end_date);
    auto load_elapsed = std::chrono::steady_clock::now() - load_start;
    double load_sec = std::chrono::duration<double>(load_elapsed).count();
    std::cout << " " << ticks.size() << " ticks loaded in "
              << std::fixed << std::setprecision(1) << load_sec << "s" << std::endl;

    if (ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return 1;
    }

    // Show price range
    double min_price = 1e9, max_price = 0;
    for (const auto& t : ticks) {
        min_price = std::min(min_price, t.bid);
        max_price = std::max(max_price, t.bid);
    }
    std::cout << "Price range: $" << std::fixed << std::setprecision(3)
              << min_price << " - $" << max_price << std::endl;

    // ========================================================================
    // BUILD JOBS
    // ========================================================================

    std::vector<Job> jobs;

    // Spike Fade sweep
    double sf_thresholds[] = {20.0, 22.0, 25.0, 28.0, 30.0};
    double sf_steps[] = {2.0, 3.0, 5.0};
    double sf_tps[] = {15.0, 16.0, 18.0};
    double sf_survs[] = {40.0, 60.0, 80.0, 100.0};

    for (double thr : sf_thresholds) {
        for (double step : sf_steps) {
            for (double tp : sf_tps) {
                for (double surv : sf_survs) {
                    Job j;
                    j.type = StrategyType::SPIKE_FADE;
                    j.sf_cfg.entry_threshold = thr;
                    j.sf_cfg.scale_in_step = step;
                    j.sf_cfg.tp_target = tp;
                    j.sf_cfg.survive_pct = surv;
                    j.sf_cfg.max_total_lots = 5.0;

                    std::ostringstream oss;
                    oss << "thr=" << std::fixed << std::setprecision(0) << thr
                        << " step=" << step << " tp=" << tp
                        << " surv=" << surv << "%";
                    j.params = oss.str();
                    jobs.push_back(j);
                }
            }
        }
    }

    // SHORT Grid sweep
    double sg_spacings[] = {0.25, 0.50, 0.75, 1.00, 1.50};
    double sg_activates[] = {18.0, 20.0, 22.0};
    double sg_closes[] = {14.0, 16.0};
    double sg_survs[] = {40.0, 60.0, 80.0, 100.0};

    for (double sp : sg_spacings) {
        for (double act : sg_activates) {
            for (double cl : sg_closes) {
                if (cl >= act) continue;
                for (double surv : sg_survs) {
                    Job j;
                    j.type = StrategyType::SHORT_GRID;
                    j.sg_cfg.spacing = sp;
                    j.sg_cfg.activate_above = act;
                    j.sg_cfg.close_below = cl;
                    j.sg_cfg.survive_pct = surv;
                    j.sg_cfg.max_total_lots = 5.0;

                    std::ostringstream oss;
                    oss << "sp=" << std::fixed << std::setprecision(2) << sp
                        << " act=" << std::setprecision(0) << act
                        << " cl=" << cl << " surv=" << surv << "%";
                    j.params = oss.str();
                    jobs.push_back(j);
                }
            }
        }
    }

    size_t sf_count = 5 * 3 * 3 * 4;
    size_t sg_count = jobs.size() - sf_count;
    std::cout << "\nTotal: " << jobs.size() << " configs (SpikeFade=" << sf_count
              << ", ShortGrid=" << sg_count << ")" << std::endl;

    // ========================================================================
    // SEQUENTIAL EXECUTION
    // (VIX has only 1.58M ticks — sequential is fast enough, avoids
    //  GCC 15 thread stack issues with AVX alignment)
    // ========================================================================

    std::vector<TestResult> results(jobs.size());

    std::cout << "Running " << jobs.size() << " configs sequentially...\n" << std::endl;

    auto sweep_start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < jobs.size(); ++i) {
        results[i] = RunJob(jobs[i], ticks, start_date, end_date);
        if ((i + 1) % 10 == 0 || i + 1 == jobs.size()) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - sweep_start).count();
            std::cerr << "  Progress: " << (i + 1) << "/" << jobs.size()
                      << " (" << elapsed << "s)" << std::endl;
        }
    }

    auto sweep_end = std::chrono::high_resolution_clock::now();
    auto sweep_secs = std::chrono::duration_cast<std::chrono::seconds>(sweep_end - sweep_start).count();

    // ========================================================================
    // RESULTS
    // ========================================================================

    std::vector<TestResult> spike_results, grid_results;
    for (const auto& r : results) {
        if (r.strategy == "SpikeFade") spike_results.push_back(r);
        else grid_results.push_back(r);
    }

    auto sort_fn = [](const TestResult& a, const TestResult& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.return_mult > b.return_mult;
    };
    std::sort(spike_results.begin(), spike_results.end(), sort_fn);
    std::sort(grid_results.begin(), grid_results.end(), sort_fn);

    int sf_so = 0, sg_so = 0;
    for (const auto& r : spike_results) if (r.stop_out) sf_so++;
    for (const auto& r : grid_results) if (r.stop_out) sg_so++;

    std::cout << "\n===========================================================" << std::endl;
    std::cout << "   COMPLETE - " << sweep_secs << "s (" << jobs.size() << " tests)" << std::endl;
    std::cout << "===========================================================" << std::endl;

    // --- Spike Fade Top 15 ---
    std::cout << "\n--- SPIKE FADE: Top 15 (of " << spike_results.size()
              << ", " << sf_so << " stop-outs) ---\n" << std::endl;
    std::cout << std::setw(4) << "#"
              << std::setw(42) << "Parameters"
              << std::setw(12) << "Final$"
              << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Trades"
              << std::setw(6) << "SO" << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)15, spike_results.size()); ++i) {
        const auto& r = spike_results[i];
        std::cout << std::setw(4) << (i+1)
                  << std::setw(42) << r.params
                  << std::setw(12) << std::fixed << std::setprecision(0) << r.final_balance
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(8) << std::setprecision(1) << r.max_dd_pct
                  << std::setw(8) << r.total_trades
                  << std::setw(6) << (r.stop_out ? "YES" : "") << std::endl;
    }

    // --- SHORT Grid Top 15 ---
    std::cout << "\n--- SHORT-ONLY GRID: Top 15 (of " << grid_results.size()
              << ", " << sg_so << " stop-outs) ---\n" << std::endl;
    std::cout << std::setw(4) << "#"
              << std::setw(42) << "Parameters"
              << std::setw(12) << "Final$"
              << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Trades"
              << std::setw(6) << "SO" << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)15, grid_results.size()); ++i) {
        const auto& r = grid_results[i];
        std::cout << std::setw(4) << (i+1)
                  << std::setw(42) << r.params
                  << std::setw(12) << std::fixed << std::setprecision(0) << r.final_balance
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(8) << std::setprecision(1) << r.max_dd_pct
                  << std::setw(8) << r.total_trades
                  << std::setw(6) << (r.stop_out ? "YES" : "") << std::endl;
    }

    // --- Head-to-Head ---
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "   HEAD-TO-HEAD COMPARISON" << std::endl;
    std::cout << "===========================================================" << std::endl;

    auto best_sf = spike_results.empty() ? TestResult{} : spike_results[0];
    auto best_sg = grid_results.empty() ? TestResult{} : grid_results[0];

    // Best risk-adjusted
    TestResult best_sf_ra = spike_results[0], best_sg_ra = grid_results[0];
    for (const auto& r : spike_results) {
        if (!r.stop_out && r.max_dd_pct > 0 &&
            (r.return_mult / r.max_dd_pct) > (best_sf_ra.return_mult / std::max(0.1, best_sf_ra.max_dd_pct)))
            best_sf_ra = r;
    }
    for (const auto& r : grid_results) {
        if (!r.stop_out && r.max_dd_pct > 0 &&
            (r.return_mult / r.max_dd_pct) > (best_sg_ra.return_mult / std::max(0.1, best_sg_ra.max_dd_pct)))
            best_sg_ra = r;
    }

    std::cout << "\nBest Return:" << std::endl;
    std::cout << "  SpikeFade:  " << std::fixed << std::setprecision(2) << best_sf.return_mult << "x"
              << " (DD " << std::setprecision(1) << best_sf.max_dd_pct << "%, " << best_sf.total_trades << " trades)"
              << "\n              " << best_sf.params << std::endl;
    std::cout << "  ShortGrid:  " << std::setprecision(2) << best_sg.return_mult << "x"
              << " (DD " << std::setprecision(1) << best_sg.max_dd_pct << "%, " << best_sg.total_trades << " trades)"
              << "\n              " << best_sg.params << std::endl;

    std::cout << "\nBest Risk-Adjusted (Return/DD):" << std::endl;
    std::cout << "  SpikeFade:  " << std::setprecision(2) << best_sf_ra.return_mult << "x"
              << " (DD " << std::setprecision(1) << best_sf_ra.max_dd_pct << "%)"
              << " ratio=" << std::setprecision(3) << (best_sf_ra.return_mult / std::max(0.1, best_sf_ra.max_dd_pct))
              << "\n              " << best_sf_ra.params << std::endl;
    std::cout << "  ShortGrid:  " << std::setprecision(2) << best_sg_ra.return_mult << "x"
              << " (DD " << std::setprecision(1) << best_sg_ra.max_dd_pct << "%)"
              << " ratio=" << std::setprecision(3) << (best_sg_ra.return_mult / std::max(0.1, best_sg_ra.max_dd_pct))
              << "\n              " << best_sg_ra.params << std::endl;

    std::cout << "\nSurvival:" << std::endl;
    std::cout << "  SpikeFade:  " << (spike_results.size() - sf_so) << "/" << spike_results.size() << " survived" << std::endl;
    std::cout << "  ShortGrid:  " << (grid_results.size() - sg_so) << "/" << grid_results.size() << " survived" << std::endl;

    std::cout << std::endl;
    return 0;
}
