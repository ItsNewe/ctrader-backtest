#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cmath>

using namespace backtest;

struct SweepConfig {
    double survive_pct;
    double base_spacing_pct;
    double lookback_hours;
    double typical_vol_pct;
    std::string label;
};

struct PeriodResult {
    double return_x;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    bool stopped_out;
};

struct SweepResult {
    std::string label;
    SweepConfig config;
    PeriodResult full;
    PeriodResult h1;
    PeriodResult h2;
    PeriodResult q1, q2, q3, q4;
    double consistency;     // min(H1/H2, H2/H1)
    double quarterly_cv;    // coefficient of variation across quarters
    double worst_dd;        // worst DD across all periods
    double risk_adj_score;  // composite score
};

std::mutex g_mutex;

PeriodResult RunOnePeriod(const SweepConfig& cfg, const std::vector<Tick>& ticks,
                          const std::string& start_date, const std::string& end_date) {
    PeriodResult result;

    TickDataConfig tick_config;
    tick_config.file_path = "";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = -15.0;
    config.swap_short = 0.0;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.verbose = false;
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.typical_vol_pct = cfg.typical_vol_pct;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;
        adaptive_cfg.min_spacing_abs = 0.05;
        adaptive_cfg.max_spacing_abs = 15.0;
        adaptive_cfg.spacing_change_threshold = 0.2;
        adaptive_cfg.pct_spacing = true;

        FillUpOscillation strategy(
            cfg.survive_pct,
            cfg.base_spacing_pct,
            0.01,
            100.0,
            5000.0,
            500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,
            30.0,
            cfg.lookback_hours,
            adaptive_cfg
        );

        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        double floating = 0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            double exit_price = engine.GetCurrentTick().bid;
            floating += (exit_price - trade->entry_price) * trade->lot_size * config.contract_size;
        }
        result.return_x = (results.final_balance + floating) / 10000.0;
        result.max_dd_pct = results.max_drawdown_pct;
        result.total_trades = results.total_trades_opened;
        result.total_swap = results.total_swap_charged;
        result.stopped_out = results.stop_out_occurred;

    } catch (const std::exception& e) {
        result.return_x = 0;
        result.max_dd_pct = 100;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " XAGUSD CONSISTENCY-OPTIMIZED PARAMETER SWEEP" << std::endl;
    std::cout << " Goal: Find params that work in ALL periods, not just bull runs" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Load ticks
    std::cout << "Loading XAGUSD ticks..." << std::flush;
    TickDataConfig load_cfg;
    load_cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";
    load_cfg.format = TickDataFormat::MT5_CSV;
    load_cfg.load_all_into_memory = true;
    TickDataManager loader(load_cfg);
    const std::vector<Tick>& ticks = loader.GetAllTicks();
    std::cout << " " << ticks.size() << " ticks" << std::endl;

    // === Price analysis: what happened in each period? ===
    std::cout << "\n=== PRICE ACTION ANALYSIS ===" << std::endl;
    {
        double first_price = 0, last_price = 0;

        // Sample price at start of each quarter
        std::vector<std::string> quarter_starts = {"2025.01", "2025.04", "2025.07", "2025.10", "2026.01"};
        size_t qi = 0;

        for (const auto& tick : ticks) {
            if (first_price == 0) first_price = tick.bid;
            last_price = tick.bid;

            std::string date_str = tick.timestamp.substr(0, 7); // YYYY.MM
            if (qi < quarter_starts.size() && date_str >= quarter_starts[qi]) {
                std::cout << "  " << quarter_starts[qi] << ": $" << std::fixed << std::setprecision(2) << tick.bid << std::endl;
                qi++;
            }
        }
        std::cout << "  End: $" << std::fixed << std::setprecision(2) << last_price << std::endl;
        std::cout << "  Total move: " << std::setprecision(1)
                  << ((last_price - first_price) / first_price * 100.0) << "%" << std::endl;
    }

    // === Build sweep configurations ===
    std::vector<SweepConfig> configs;

    // Wider ranges than original presets
    std::vector<double> survive_vals = {20, 22, 25, 28, 30, 35, 40};
    std::vector<double> spacing_vals = {2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0};
    std::vector<double> lookback_vals = {1.0, 4.0};

    for (double survive : survive_vals) {
        for (double spacing : spacing_vals) {
            for (double lookback : lookback_vals) {
                SweepConfig cfg;
                cfg.survive_pct = survive;
                cfg.base_spacing_pct = spacing;
                cfg.lookback_hours = lookback;
                cfg.typical_vol_pct = (lookback <= 1.0) ? 0.45 : 0.97;

                char buf[64];
                snprintf(buf, sizeof(buf), "s%d_%.0f%%_lb%d",
                         (int)survive, spacing, (int)lookback);
                cfg.label = buf;
                configs.push_back(cfg);
            }
        }
    }

    std::cout << "\nSweeping " << configs.size() << " configurations x 7 periods = "
              << configs.size() * 7 << " backtests..." << std::endl;

    // Periods
    struct Period {
        std::string name;
        std::string start;
        std::string end;
    };
    std::vector<Period> periods = {
        {"Full", "2025.01.02", "2026.01.24"},
        {"H1",   "2025.01.02", "2025.07.14"},
        {"H2",   "2025.07.14", "2026.01.24"},
        {"Q1",   "2025.01.02", "2025.04.01"},
        {"Q2",   "2025.04.01", "2025.07.01"},
        {"Q3",   "2025.07.01", "2025.10.01"},
        {"Q4",   "2025.10.01", "2026.01.24"},
    };

    // Run all combinations
    std::vector<SweepResult> results(configs.size());
    int num_threads = (int)std::thread::hardware_concurrency();
    if (num_threads < 1) num_threads = 4;
    std::cout << "Threads: " << num_threads << std::endl;

    auto start_time = std::chrono::steady_clock::now();
    size_t next_config = 0;
    std::mutex config_mutex;
    int completed = 0;

    auto worker = [&]() {
        while (true) {
            size_t idx;
            {
                std::lock_guard<std::mutex> lock(config_mutex);
                if (next_config >= configs.size()) return;
                idx = next_config++;
            }

            auto& cfg = configs[idx];
            auto& res = results[idx];
            res.label = cfg.label;
            res.config = cfg;

            // Run all 7 periods
            res.full = RunOnePeriod(cfg, ticks, "2025.01.02", "2026.01.24");
            res.h1   = RunOnePeriod(cfg, ticks, "2025.01.02", "2025.07.14");
            res.h2   = RunOnePeriod(cfg, ticks, "2025.07.14", "2026.01.24");
            res.q1   = RunOnePeriod(cfg, ticks, "2025.01.02", "2025.04.01");
            res.q2   = RunOnePeriod(cfg, ticks, "2025.04.01", "2025.07.01");
            res.q3   = RunOnePeriod(cfg, ticks, "2025.07.01", "2025.10.01");
            res.q4   = RunOnePeriod(cfg, ticks, "2025.10.01", "2026.01.24");

            // Calculate consistency metrics
            if (!res.h1.stopped_out && !res.h2.stopped_out &&
                res.h1.return_x > 0 && res.h2.return_x > 0) {
                double ratio = res.h1.return_x / res.h2.return_x;
                res.consistency = std::min(ratio, 1.0 / ratio);
            } else {
                res.consistency = 0.0;
            }

            // Quarterly CV
            std::vector<double> q_returns;
            if (!res.q1.stopped_out) q_returns.push_back(res.q1.return_x);
            if (!res.q2.stopped_out) q_returns.push_back(res.q2.return_x);
            if (!res.q3.stopped_out) q_returns.push_back(res.q3.return_x);
            if (!res.q4.stopped_out) q_returns.push_back(res.q4.return_x);

            if (q_returns.size() >= 4) {
                double mean = 0;
                for (double v : q_returns) mean += v;
                mean /= q_returns.size();
                double var = 0;
                for (double v : q_returns) var += (v - mean) * (v - mean);
                var /= q_returns.size();
                res.quarterly_cv = (mean > 0) ? std::sqrt(var) / mean : 99.0;
            } else {
                res.quarterly_cv = 99.0;
            }

            // Worst DD across all periods
            res.worst_dd = std::max({res.full.max_dd_pct, res.h1.max_dd_pct, res.h2.max_dd_pct,
                                     res.q1.max_dd_pct, res.q2.max_dd_pct, res.q3.max_dd_pct, res.q4.max_dd_pct});

            // Composite risk-adjusted score:
            // Reward consistency + return, penalize DD and variance
            // Score = consistency * full_return / (1 + worst_dd/100) / (1 + quarterly_cv)
            if (res.full.return_x > 0 && !res.full.stopped_out && res.worst_dd < 95) {
                res.risk_adj_score = res.consistency * res.full.return_x /
                                     (1.0 + res.worst_dd / 100.0) /
                                     (1.0 + res.quarterly_cv);
            } else {
                res.risk_adj_score = 0.0;
            }

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                completed++;
                if (completed % 10 == 0) {
                    std::cout << "  " << completed << "/" << configs.size() << " configs done\r" << std::flush;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time;
    double elapsed_sec = std::chrono::duration<double>(elapsed).count();
    std::cout << "\nCompleted " << configs.size() << " configs in "
              << std::fixed << std::setprecision(1) << elapsed_sec << "s" << std::endl;

    // === Sort by composite score ===
    std::vector<size_t> indices(results.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return results[a].risk_adj_score > results[b].risk_adj_score;
    });

    // === Display Top 30 by composite score ===
    std::cout << "\n=== TOP 30 BY CONSISTENCY-ADJUSTED SCORE ===" << std::endl;
    std::cout << "Score = consistency * return / (1+DD/100) / (1+CV)" << std::endl;
    std::cout << std::setw(16) << "Config"
              << std::setw(7) << "Full"
              << std::setw(6) << "H1"
              << std::setw(6) << "H2"
              << std::setw(6) << "Q1"
              << std::setw(6) << "Q2"
              << std::setw(6) << "Q3"
              << std::setw(6) << "Q4"
              << std::setw(7) << "Cons."
              << std::setw(6) << "CV"
              << std::setw(7) << "WstDD"
              << std::setw(7) << "Score" << std::endl;
    std::cout << std::string(89, '-') << std::endl;

    int shown = 0;
    for (size_t idx : indices) {
        if (shown >= 30) break;
        auto& r = results[idx];
        if (r.risk_adj_score <= 0) continue;

        std::cout << std::setw(16) << r.label
                  << std::setw(6) << std::fixed << std::setprecision(1) << r.full.return_x << "x"
                  << std::setw(5) << std::setprecision(1) << r.h1.return_x << "x"
                  << std::setw(5) << r.h2.return_x << "x"
                  << std::setw(5) << r.q1.return_x << "x"
                  << std::setw(5) << r.q2.return_x << "x"
                  << std::setw(5) << r.q3.return_x << "x"
                  << std::setw(5) << r.q4.return_x << "x"
                  << std::setw(6) << std::setprecision(2) << r.consistency
                  << std::setw(6) << std::setprecision(2) << r.quarterly_cv
                  << std::setw(6) << std::setprecision(1) << r.worst_dd << "%"
                  << std::setw(6) << std::setprecision(2) << r.risk_adj_score << std::endl;
        shown++;
    }

    // === Sort by consistency only (for configs with >2x return) ===
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        // Filter: must have >2x return and not stopped out
        bool a_valid = results[a].full.return_x > 2.0 && !results[a].full.stopped_out;
        bool b_valid = results[b].full.return_x > 2.0 && !results[b].full.stopped_out;
        if (a_valid != b_valid) return a_valid;
        return results[a].consistency > results[b].consistency;
    });

    std::cout << "\n=== TOP 20 BY CONSISTENCY (min return > 2x) ===" << std::endl;
    std::cout << std::setw(16) << "Config"
              << std::setw(7) << "Full"
              << std::setw(6) << "H1"
              << std::setw(6) << "H2"
              << std::setw(7) << "H1DD"
              << std::setw(7) << "H2DD"
              << std::setw(7) << "Cons."
              << std::setw(6) << "CV"
              << std::setw(7) << "Score" << std::endl;
    std::cout << std::string(73, '-') << std::endl;

    shown = 0;
    for (size_t idx : indices) {
        if (shown >= 20) break;
        auto& r = results[idx];
        if (r.full.return_x <= 2.0 || r.full.stopped_out) continue;

        std::cout << std::setw(16) << r.label
                  << std::setw(6) << std::fixed << std::setprecision(1) << r.full.return_x << "x"
                  << std::setw(5) << r.h1.return_x << "x"
                  << std::setw(5) << r.h2.return_x << "x"
                  << std::setw(6) << r.h1.max_dd_pct << "%"
                  << std::setw(6) << r.h2.max_dd_pct << "%"
                  << std::setw(6) << std::setprecision(2) << r.consistency
                  << std::setw(6) << r.quarterly_cv
                  << std::setw(6) << r.risk_adj_score << std::endl;
        shown++;
    }

    // === Filter: configs where H1 DD < 50% AND full return > 3x ===
    std::cout << "\n=== VIABLE CONFIGS (H1 DD < 50% AND return > 3x) ===" << std::endl;
    std::cout << std::setw(16) << "Config"
              << std::setw(7) << "Full"
              << std::setw(6) << "H1"
              << std::setw(6) << "H2"
              << std::setw(7) << "H1DD"
              << std::setw(7) << "H2DD"
              << std::setw(6) << "Q1"
              << std::setw(6) << "Q2"
              << std::setw(6) << "Q3"
              << std::setw(6) << "Q4"
              << std::setw(7) << "Cons." << std::endl;
    std::cout << std::string(84, '-') << std::endl;

    std::vector<size_t> viable;
    for (size_t i = 0; i < results.size(); i++) {
        auto& r = results[i];
        if (!r.full.stopped_out && r.full.return_x > 3.0 &&
            r.h1.max_dd_pct < 50.0 && !r.h1.stopped_out && !r.h2.stopped_out) {
            viable.push_back(i);
        }
    }
    std::sort(viable.begin(), viable.end(), [&](size_t a, size_t b) {
        return results[a].risk_adj_score > results[b].risk_adj_score;
    });

    if (viable.empty()) {
        std::cout << "  No configs meet these criteria." << std::endl;
        std::cout << "  Relaxing to H1 DD < 60%..." << std::endl;
        for (size_t i = 0; i < results.size(); i++) {
            auto& r = results[i];
            if (!r.full.stopped_out && r.full.return_x > 2.0 &&
                r.h1.max_dd_pct < 60.0 && !r.h1.stopped_out && !r.h2.stopped_out) {
                viable.push_back(i);
            }
        }
        std::sort(viable.begin(), viable.end(), [&](size_t a, size_t b) {
            return results[a].risk_adj_score > results[b].risk_adj_score;
        });
    }

    for (size_t idx : viable) {
        auto& r = results[idx];
        std::cout << std::setw(16) << r.label
                  << std::setw(6) << std::fixed << std::setprecision(1) << r.full.return_x << "x"
                  << std::setw(5) << r.h1.return_x << "x"
                  << std::setw(5) << r.h2.return_x << "x"
                  << std::setw(6) << r.h1.max_dd_pct << "%"
                  << std::setw(6) << r.h2.max_dd_pct << "%"
                  << std::setw(5) << r.q1.return_x << "x"
                  << std::setw(5) << r.q2.return_x << "x"
                  << std::setw(5) << r.q3.return_x << "x"
                  << std::setw(5) << r.q4.return_x << "x"
                  << std::setw(6) << std::setprecision(2) << r.consistency << std::endl;
    }

    // === Summary statistics ===
    std::cout << "\n=== PARAMETER SENSITIVITY ===" << std::endl;

    // By survive_pct
    std::cout << "\nBy survive_pct (avg over all spacing/lookback):" << std::endl;
    std::cout << std::setw(10) << "Survive"
              << std::setw(8) << "Return"
              << std::setw(8) << "H1DD"
              << std::setw(8) << "Cons."
              << std::setw(8) << "SO%" << std::endl;
    for (double s : survive_vals) {
        double sum_ret = 0, sum_dd = 0, sum_cons = 0;
        int count = 0, so_count = 0;
        for (auto& r : results) {
            if (r.config.survive_pct == s) {
                if (!r.full.stopped_out) {
                    sum_ret += r.full.return_x;
                    sum_dd += r.h1.max_dd_pct;
                    sum_cons += r.consistency;
                    count++;
                } else {
                    so_count++;
                }
            }
        }
        if (count > 0) {
            std::cout << std::setw(8) << std::fixed << std::setprecision(0) << s << "%"
                      << std::setw(7) << std::setprecision(1) << (sum_ret / count) << "x"
                      << std::setw(7) << (sum_dd / count) << "%"
                      << std::setw(7) << std::setprecision(2) << (sum_cons / count)
                      << std::setw(7) << std::setprecision(0) << (so_count * 100.0 / (count + so_count)) << "%" << std::endl;
        }
    }

    // By spacing_pct
    std::cout << "\nBy spacing_pct (avg over all survive/lookback):" << std::endl;
    std::cout << std::setw(10) << "Spacing"
              << std::setw(8) << "Return"
              << std::setw(8) << "H1DD"
              << std::setw(8) << "Cons."
              << std::setw(8) << "Trades" << std::endl;
    for (double sp : spacing_vals) {
        double sum_ret = 0, sum_dd = 0, sum_cons = 0;
        int sum_trades = 0, count = 0;
        for (auto& r : results) {
            if (r.config.base_spacing_pct == sp && !r.full.stopped_out) {
                sum_ret += r.full.return_x;
                sum_dd += r.h1.max_dd_pct;
                sum_cons += r.consistency;
                sum_trades += r.full.total_trades;
                count++;
            }
        }
        if (count > 0) {
            std::cout << std::setw(8) << std::fixed << std::setprecision(0) << sp << "%"
                      << std::setw(7) << std::setprecision(1) << (sum_ret / count) << "x"
                      << std::setw(7) << (sum_dd / count) << "%"
                      << std::setw(7) << std::setprecision(2) << (sum_cons / count)
                      << std::setw(8) << (sum_trades / count) << std::endl;
        }
    }

    // By lookback
    std::cout << "\nBy lookback (avg over all survive/spacing):" << std::endl;
    std::cout << std::setw(10) << "Lookback"
              << std::setw(8) << "Return"
              << std::setw(8) << "H1DD"
              << std::setw(8) << "Cons." << std::endl;
    for (double lb : lookback_vals) {
        double sum_ret = 0, sum_dd = 0, sum_cons = 0;
        int count = 0;
        for (auto& r : results) {
            if (r.config.lookback_hours == lb && !r.full.stopped_out) {
                sum_ret += r.full.return_x;
                sum_dd += r.h1.max_dd_pct;
                sum_cons += r.consistency;
                count++;
            }
        }
        if (count > 0) {
            std::cout << std::setw(8) << std::fixed << std::setprecision(0) << lb << "h"
                      << std::setw(7) << std::setprecision(1) << (sum_ret / count) << "x"
                      << std::setw(7) << (sum_dd / count) << "%"
                      << std::setw(7) << std::setprecision(2) << (sum_cons / count) << std::endl;
        }
    }

    std::cout << "\n================================================================" << std::endl;
    std::cout << " RECOMMENDATION" << std::endl;
    std::cout << "================================================================" << std::endl;

    if (!viable.empty()) {
        auto& best = results[viable[0]];
        std::cout << "\nBest regime-independent config: " << best.label << std::endl;
        std::cout << "  survive_pct = " << best.config.survive_pct << "%" << std::endl;
        std::cout << "  base_spacing_pct = " << best.config.base_spacing_pct << "%" << std::endl;
        std::cout << "  lookback_hours = " << best.config.lookback_hours << "h" << std::endl;
        std::cout << "  typical_vol_pct = " << best.config.typical_vol_pct << "%" << std::endl;
        std::cout << "\n  Full year: " << std::setprecision(1) << best.full.return_x << "x, DD=" << best.full.max_dd_pct << "%" << std::endl;
        std::cout << "  H1: " << best.h1.return_x << "x, DD=" << best.h1.max_dd_pct << "%" << std::endl;
        std::cout << "  H2: " << best.h2.return_x << "x, DD=" << best.h2.max_dd_pct << "%" << std::endl;
        std::cout << "  Consistency: " << std::setprecision(2) << best.consistency << std::endl;
        std::cout << "  Quarterly: Q1=" << std::setprecision(1) << best.q1.return_x
                  << "x Q2=" << best.q2.return_x
                  << "x Q3=" << best.q3.return_x
                  << "x Q4=" << best.q4.return_x << "x" << std::endl;
    } else {
        std::cout << "\nNo viable config found with H1 DD < 60% and return > 2x." << std::endl;
        std::cout << "XAGUSD may require a fundamentally different approach for consistency." << std::endl;
    }

    return 0;
}
