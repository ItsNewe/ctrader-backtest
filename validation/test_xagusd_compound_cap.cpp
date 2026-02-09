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

struct TestConfig {
    double survive_pct;
    double base_spacing_pct;
    double lookback_hours;
    double typical_vol_pct;
    double max_compound_factor;  // 0 = unlimited
    std::string label;
};

struct PeriodResult {
    double return_x;
    double max_dd_pct;
    int total_trades;
    bool stopped_out;
};

struct TestResult {
    std::string label;
    TestConfig config;
    PeriodResult full, h1, h2, q1, q2, q3, q4;
    double consistency;
    double quarterly_cv;
};

std::mutex g_mutex;

PeriodResult RunOnePeriod(const TestConfig& cfg, const std::vector<Tick>& ticks,
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
        adaptive_cfg.max_compound_factor = cfg.max_compound_factor;

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
    std::cout << " XAGUSD COMPOUNDING CAP ANALYSIS" << std::endl;
    std::cout << " Testing how equity cap affects H1/H2 consistency" << std::endl;
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

    // === Test configurations ===
    // Base params from best consistency configs + original presets
    struct BaseParams {
        std::string name;
        double survive;
        double spacing;
        double lookback;
        double typical_vol;
    };

    std::vector<BaseParams> base_params = {
        {"Original_Aggressive", 18.0, 2.0, 1.0, 0.45},
        {"Original_Balanced",   19.0, 2.0, 1.0, 0.45},
        {"Original_Conservative", 20.0, 2.0, 1.0, 0.45},
        {"Best_Consistency",    35.0, 4.0, 1.0, 0.45},
        {"Best_ViableScore",    30.0, 6.0, 4.0, 0.97},
    };

    std::vector<double> compound_factors = {0.0, 1.5, 2.0, 3.0, 5.0, 10.0, 20.0};

    std::vector<TestConfig> configs;
    for (auto& bp : base_params) {
        for (double cf : compound_factors) {
            TestConfig tc;
            tc.survive_pct = bp.survive;
            tc.base_spacing_pct = bp.spacing;
            tc.lookback_hours = bp.lookback;
            tc.typical_vol_pct = bp.typical_vol;
            tc.max_compound_factor = cf;

            char buf[64];
            std::string cf_str = (cf == 0.0) ? "inf" : std::to_string((int)cf) + "x";
            snprintf(buf, sizeof(buf), "%s_cap%s", bp.name.c_str(), cf_str.c_str());
            tc.label = buf;
            configs.push_back(tc);
        }
    }

    std::cout << "\nRunning " << configs.size() << " configurations x 7 periods = "
              << configs.size() * 7 << " backtests..." << std::endl;

    // Run all
    std::vector<TestResult> results(configs.size());
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

            res.full = RunOnePeriod(cfg, ticks, "2025.01.02", "2026.01.24");
            res.h1   = RunOnePeriod(cfg, ticks, "2025.01.02", "2025.07.14");
            res.h2   = RunOnePeriod(cfg, ticks, "2025.07.14", "2026.01.24");
            res.q1   = RunOnePeriod(cfg, ticks, "2025.01.02", "2025.04.01");
            res.q2   = RunOnePeriod(cfg, ticks, "2025.04.01", "2025.07.01");
            res.q3   = RunOnePeriod(cfg, ticks, "2025.07.01", "2025.10.01");
            res.q4   = RunOnePeriod(cfg, ticks, "2025.10.01", "2026.01.24");

            if (!res.h1.stopped_out && !res.h2.stopped_out &&
                res.h1.return_x > 0 && res.h2.return_x > 0) {
                double ratio = res.h1.return_x / res.h2.return_x;
                res.consistency = std::min(ratio, 1.0 / ratio);
            } else {
                res.consistency = 0.0;
            }

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

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                completed++;
                if (completed % 5 == 0) {
                    std::cout << "  " << completed << "/" << configs.size() << " done\r" << std::flush;
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
    std::cout << "\nCompleted in " << std::fixed << std::setprecision(1) << elapsed_sec << "s" << std::endl;

    // === Display results grouped by base params ===
    std::cout << "\n================================================================" << std::endl;
    std::cout << " COMPOUNDING CAP EFFECT ON CONSISTENCY" << std::endl;
    std::cout << "================================================================" << std::endl;

    for (auto& bp : base_params) {
        std::cout << "\n--- " << bp.name << " (s" << (int)bp.survive
                  << "_" << (int)bp.spacing << "%_lb" << (int)bp.lookback << ") ---" << std::endl;
        std::cout << std::setw(10) << "Cap"
                  << std::setw(8) << "Full"
                  << std::setw(7) << "H1"
                  << std::setw(7) << "H2"
                  << std::setw(7) << "Q1"
                  << std::setw(7) << "Q2"
                  << std::setw(7) << "Q3"
                  << std::setw(7) << "Q4"
                  << std::setw(7) << "H1DD"
                  << std::setw(7) << "H2DD"
                  << std::setw(7) << "Cons."
                  << std::setw(6) << "CV" << std::endl;
        std::cout << std::string(87, '-') << std::endl;

        for (auto& r : results) {
            if (r.config.survive_pct != bp.survive ||
                r.config.base_spacing_pct != bp.spacing ||
                r.config.lookback_hours != bp.lookback) continue;

            std::string cap_str = (r.config.max_compound_factor == 0.0) ? "none" :
                                  std::to_string((int)r.config.max_compound_factor) + "x";

            std::cout << std::setw(10) << cap_str
                      << std::setw(7) << std::fixed << std::setprecision(1) << r.full.return_x << "x"
                      << std::setw(6) << r.h1.return_x << "x"
                      << std::setw(6) << r.h2.return_x << "x"
                      << std::setw(6) << r.q1.return_x << "x"
                      << std::setw(6) << r.q2.return_x << "x"
                      << std::setw(6) << r.q3.return_x << "x"
                      << std::setw(6) << r.q4.return_x << "x"
                      << std::setw(6) << r.h1.max_dd_pct << "%"
                      << std::setw(6) << r.h2.max_dd_pct << "%"
                      << std::setw(6) << std::setprecision(2) << r.consistency
                      << std::setw(6) << r.quarterly_cv << std::endl;
        }
    }

    // === Find best configurations by consistency with acceptable return ===
    std::cout << "\n================================================================" << std::endl;
    std::cout << " BEST CONFIGURATIONS (consistency > 0.5 AND return > 3x)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<size_t> good_indices;
    for (size_t i = 0; i < results.size(); i++) {
        auto& r = results[i];
        if (r.consistency > 0.5 && r.full.return_x > 3.0 && !r.full.stopped_out) {
            good_indices.push_back(i);
        }
    }

    std::sort(good_indices.begin(), good_indices.end(), [&](size_t a, size_t b) {
        // Score: consistency * return / (1 + worst_dd/100)
        auto& ra = results[a];
        auto& rb = results[b];
        double worst_a = std::max(ra.h1.max_dd_pct, ra.h2.max_dd_pct);
        double worst_b = std::max(rb.h1.max_dd_pct, rb.h2.max_dd_pct);
        double score_a = ra.consistency * ra.full.return_x / (1.0 + worst_a / 100.0);
        double score_b = rb.consistency * rb.full.return_x / (1.0 + worst_b / 100.0);
        return score_a > score_b;
    });

    if (good_indices.empty()) {
        std::cout << "No configs with consistency > 0.5 and return > 3x." << std::endl;
        std::cout << "Relaxing to consistency > 0.4 and return > 2x..." << std::endl;
        for (size_t i = 0; i < results.size(); i++) {
            auto& r = results[i];
            if (r.consistency > 0.4 && r.full.return_x > 2.0 && !r.full.stopped_out) {
                good_indices.push_back(i);
            }
        }
        std::sort(good_indices.begin(), good_indices.end(), [&](size_t a, size_t b) {
            auto& ra = results[a];
            auto& rb = results[b];
            double worst_a = std::max(ra.h1.max_dd_pct, ra.h2.max_dd_pct);
            double worst_b = std::max(rb.h1.max_dd_pct, rb.h2.max_dd_pct);
            double score_a = ra.consistency * ra.full.return_x / (1.0 + worst_a / 100.0);
            double score_b = rb.consistency * rb.full.return_x / (1.0 + worst_b / 100.0);
            return score_a > score_b;
        });
    }

    std::cout << std::setw(30) << "Config"
              << std::setw(7) << "Full"
              << std::setw(7) << "H1"
              << std::setw(7) << "H2"
              << std::setw(7) << "H1DD"
              << std::setw(7) << "H2DD"
              << std::setw(7) << "Cons."
              << std::setw(6) << "CV" << std::endl;
    std::cout << std::string(78, '-') << std::endl;

    for (size_t idx : good_indices) {
        auto& r = results[idx];
        std::cout << std::setw(30) << r.label
                  << std::setw(6) << std::fixed << std::setprecision(1) << r.full.return_x << "x"
                  << std::setw(6) << r.h1.return_x << "x"
                  << std::setw(6) << r.h2.return_x << "x"
                  << std::setw(6) << r.h1.max_dd_pct << "%"
                  << std::setw(6) << r.h2.max_dd_pct << "%"
                  << std::setw(6) << std::setprecision(2) << r.consistency
                  << std::setw(6) << r.quarterly_cv << std::endl;
    }

    // === Conclusion ===
    std::cout << "\n================================================================" << std::endl;
    std::cout << " ANALYSIS: HOW COMPOUNDING CAP AFFECTS CONSISTENCY" << std::endl;
    std::cout << "================================================================" << std::endl;

    // For each base param set, show the trend
    for (auto& bp : base_params) {
        std::cout << "\n" << bp.name << ":" << std::endl;
        double prev_cons = 0;
        for (auto& r : results) {
            if (r.config.survive_pct != bp.survive ||
                r.config.base_spacing_pct != bp.spacing ||
                r.config.lookback_hours != bp.lookback) continue;

            std::string cap_str = (r.config.max_compound_factor == 0.0) ? "unlimited" :
                                  std::to_string((int)r.config.max_compound_factor) + "x cap";
            std::string arrow = (r.consistency > prev_cons) ? " ^" :
                               (r.consistency < prev_cons) ? " v" : "  ";
            std::cout << "  " << std::setw(12) << cap_str
                      << " -> Return=" << std::setw(6) << std::fixed << std::setprecision(1) << r.full.return_x << "x"
                      << "  Consistency=" << std::setprecision(2) << r.consistency
                      << "  H1DD=" << std::setprecision(1) << r.h1.max_dd_pct << "%"
                      << arrow << std::endl;
            prev_cons = r.consistency;
        }
    }

    return 0;
}
