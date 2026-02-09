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

struct PresetConfig {
    std::string name;
    std::string symbol;
    double survive_pct;
    double base_spacing_pct;
    double lookback_hours;
    double typical_vol_pct;
    double min_spacing_pct;
    double max_spacing_pct;
    double min_spacing_mult;
    double max_spacing_mult;
    double spacing_change_threshold_pct;
    double contract_size;
    double swap_long;
    double pip_size;
    std::string tick_path;
};

struct PeriodResult {
    std::string preset_name;
    std::string period_name;
    double final_equity;
    double return_x;
    double max_dd_pct;
    int total_trades;
    int spacing_changes;
    double total_swap;
    bool stopped_out;
};

std::mutex print_mutex;

PeriodResult RunPeriod(const PresetConfig& preset, const std::vector<Tick>& all_ticks,
                       const std::string& start_date, const std::string& end_date,
                       const std::string& period_name) {
    PeriodResult result;
    result.preset_name = preset.name;
    result.period_name = period_name;

    TickDataConfig tick_config;
    tick_config.file_path = "";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = preset.symbol;
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = preset.contract_size;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = preset.pip_size;

    config.swap_long = preset.swap_long;
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
        adaptive_cfg.typical_vol_pct = preset.typical_vol_pct;
        adaptive_cfg.min_spacing_mult = preset.min_spacing_mult;
        adaptive_cfg.max_spacing_mult = preset.max_spacing_mult;
        adaptive_cfg.min_spacing_abs = preset.min_spacing_pct;
        adaptive_cfg.max_spacing_abs = preset.max_spacing_pct;
        adaptive_cfg.spacing_change_threshold = preset.spacing_change_threshold_pct;
        adaptive_cfg.pct_spacing = true;

        FillUpOscillation strategy(
            preset.survive_pct,
            preset.base_spacing_pct,
            0.01,
            (preset.symbol == "XAGUSD") ? 100.0 : 10.0,
            preset.contract_size,
            500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,
            30.0,
            preset.lookback_hours,
            adaptive_cfg
        );

        engine.RunWithTicks(all_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        double floating = 0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            double exit_price = engine.GetCurrentTick().bid;
            floating += (exit_price - trade->entry_price) * trade->lot_size * config.contract_size;
        }
        result.final_equity = results.final_balance + floating;
        result.return_x = result.final_equity / 10000.0;
        result.max_dd_pct = results.max_drawdown_pct;
        result.total_trades = results.total_trades_opened;
        result.spacing_changes = strategy.GetAdaptiveSpacingChanges();
        result.total_swap = results.total_swap_charged;
        result.stopped_out = results.stop_out_occurred;

    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cerr << "Error [" << preset.name << " " << period_name << "]: " << e.what() << std::endl;
        result.final_equity = 0;
        result.return_x = 0;
        result.max_dd_pct = 100;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " PRESET OVERFITTING VALIDATION (H1/H2 Split)" << std::endl;
    std::cout << " Testing all 6 presets for out-of-sample consistency" << std::endl;
    std::cout << "================================================================" << std::endl;

    // === Define all 6 presets ===
    std::vector<PresetConfig> presets;

    // XAUUSD Presets
    {
        PresetConfig p;
        p.symbol = "XAUUSD";
        p.contract_size = 100.0;
        p.swap_long = -66.99;
        p.pip_size = 0.01;
        p.lookback_hours = 4.0;
        p.typical_vol_pct = 0.55;
        p.min_spacing_pct = 0.005;
        p.max_spacing_pct = 1.0;
        p.min_spacing_mult = 0.5;
        p.max_spacing_mult = 3.0;
        p.spacing_change_threshold_pct = 0.01;
        p.tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

        p.name = "XAUUSD_Balanced";
        p.survive_pct = 13.0;
        p.base_spacing_pct = 0.06;
        presets.push_back(p);

        p.name = "XAUUSD_Conservative";
        p.survive_pct = 15.0;
        p.base_spacing_pct = 0.10;
        presets.push_back(p);

        p.name = "XAUUSD_Aggressive";
        p.survive_pct = 12.0;
        p.base_spacing_pct = 0.05;
        presets.push_back(p);
    }

    // XAGUSD Presets
    {
        PresetConfig p;
        p.symbol = "XAGUSD";
        p.contract_size = 5000.0;
        p.swap_long = -15.0;
        p.pip_size = 0.001;
        p.lookback_hours = 1.0;
        p.typical_vol_pct = 0.45;
        p.min_spacing_pct = 0.05;
        p.max_spacing_pct = 15.0;
        p.min_spacing_mult = 0.5;
        p.max_spacing_mult = 3.0;
        p.spacing_change_threshold_pct = 0.2;
        p.tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

        p.name = "XAGUSD_Balanced";
        p.survive_pct = 19.0;
        p.base_spacing_pct = 2.0;
        presets.push_back(p);

        p.name = "XAGUSD_Conservative";
        p.survive_pct = 20.0;
        p.base_spacing_pct = 2.0;
        presets.push_back(p);

        p.name = "XAGUSD_Aggressive";
        p.survive_pct = 18.0;
        p.base_spacing_pct = 2.0;
        presets.push_back(p);
    }

    // === Load tick data for each instrument ===
    std::cout << "\nLoading XAUUSD ticks..." << std::flush;
    TickDataConfig xau_load;
    xau_load.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    xau_load.format = TickDataFormat::MT5_CSV;
    xau_load.load_all_into_memory = true;
    TickDataManager xau_loader(xau_load);
    const std::vector<Tick>& xau_ticks = xau_loader.GetAllTicks();
    std::cout << " " << xau_ticks.size() << " ticks" << std::endl;

    std::cout << "Loading XAGUSD ticks..." << std::flush;
    TickDataConfig xag_load;
    xag_load.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";
    xag_load.format = TickDataFormat::MT5_CSV;
    xag_load.load_all_into_memory = true;
    TickDataManager xag_loader(xag_load);
    const std::vector<Tick>& xag_ticks = xag_loader.GetAllTicks();
    std::cout << " " << xag_ticks.size() << " ticks" << std::endl;

    // === Define periods ===
    // XAUUSD: 2025.01.01 - 2025.12.29
    //   H1: Jan-Jun, H2: Jul-Dec
    //   Also test quarterly: Q1, Q2, Q3, Q4
    struct Period {
        std::string name;
        std::string start;
        std::string end;
    };

    std::vector<Period> xau_periods = {
        {"Full",  "2025.01.01", "2025.12.29"},
        {"H1",    "2025.01.01", "2025.06.30"},
        {"H2",    "2025.07.01", "2025.12.29"},
        {"Q1",    "2025.01.01", "2025.03.31"},
        {"Q2",    "2025.04.01", "2025.06.30"},
        {"Q3",    "2025.07.01", "2025.09.30"},
        {"Q4",    "2025.10.01", "2025.12.29"},
    };

    // XAGUSD: 2025.01.02 - 2026.01.24
    //   H1: Jan-Jul 2025, H2: Jul 2025 - Jan 2026
    //   Also quarterly
    std::vector<Period> xag_periods = {
        {"Full",  "2025.01.02", "2026.01.24"},
        {"H1",    "2025.01.02", "2025.07.14"},
        {"H2",    "2025.07.14", "2026.01.24"},
        {"Q1",    "2025.01.02", "2025.04.01"},
        {"Q2",    "2025.04.01", "2025.07.01"},
        {"Q3",    "2025.07.01", "2025.10.01"},
        {"Q4",    "2025.10.01", "2026.01.24"},
    };

    // === Run all preset/period combinations ===
    struct TestCase {
        size_t preset_idx;
        size_t period_idx;
        bool is_xauusd;
    };

    std::vector<TestCase> test_cases;
    for (size_t i = 0; i < 3; i++) {  // XAUUSD presets
        for (size_t j = 0; j < xau_periods.size(); j++) {
            test_cases.push_back({i, j, true});
        }
    }
    for (size_t i = 3; i < 6; i++) {  // XAGUSD presets
        for (size_t j = 0; j < xag_periods.size(); j++) {
            test_cases.push_back({i, j, false});
        }
    }

    std::cout << "\nRunning " << test_cases.size() << " test cases..." << std::endl;

    std::vector<PeriodResult> all_results(test_cases.size());
    int num_threads = (int)std::thread::hardware_concurrency();
    if (num_threads < 1) num_threads = 4;
    std::cout << "Threads: " << num_threads << std::endl;

    auto start_time = std::chrono::steady_clock::now();

    size_t next_case = 0;
    std::mutex case_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx;
            {
                std::lock_guard<std::mutex> lock(case_mutex);
                if (next_case >= test_cases.size()) return;
                idx = next_case++;
            }
            auto& tc = test_cases[idx];
            auto& preset = presets[tc.preset_idx];
            const auto& ticks = tc.is_xauusd ? xau_ticks : xag_ticks;
            const auto& periods = tc.is_xauusd ? xau_periods : xag_periods;
            auto& period = periods[tc.period_idx];

            all_results[idx] = RunPeriod(preset, ticks, period.start, period.end, period.name);

            {
                std::lock_guard<std::mutex> lock(print_mutex);
                auto& r = all_results[idx];
                std::cout << "  " << std::setw(22) << r.preset_name
                          << " " << std::setw(5) << r.period_name
                          << "  " << std::setw(7) << std::fixed << std::setprecision(1) << r.return_x << "x"
                          << "  DD=" << std::setw(5) << r.max_dd_pct << "%"
                          << "  Trades=" << std::setw(6) << r.total_trades
                          << (r.stopped_out ? "  **STOPPED OUT**" : "") << std::endl;
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

    // === Analysis ===
    std::cout << "\n================================================================" << std::endl;
    std::cout << " OVERFITTING ANALYSIS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Group results by preset
    for (size_t pi = 0; pi < presets.size(); pi++) {
        auto& preset = presets[pi];
        bool is_xauusd = (pi < 3);
        const auto& periods = is_xauusd ? xau_periods : xag_periods;

        std::cout << "\n--- " << preset.name << " (survive=" << preset.survive_pct
                  << "%, spacing=" << preset.base_spacing_pct << "%) ---" << std::endl;

        std::cout << std::setw(8) << "Period"
                  << std::setw(9) << "Return"
                  << std::setw(8) << "MaxDD"
                  << std::setw(8) << "Trades"
                  << std::setw(9) << "Swap"
                  << std::setw(6) << "Status" << std::endl;
        std::cout << std::string(48, '-') << std::endl;

        PeriodResult h1_result, h2_result, full_result;

        for (size_t ci = 0; ci < test_cases.size(); ci++) {
            auto& tc = test_cases[ci];
            if (tc.preset_idx != pi) continue;
            auto& r = all_results[ci];
            auto& period = periods[tc.period_idx];

            std::cout << std::setw(8) << period.name
                      << std::setw(8) << std::fixed << std::setprecision(2) << r.return_x << "x"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::setw(8) << r.total_trades
                      << std::setw(8) << std::setprecision(0) << r.total_swap << "$"
                      << std::setw(6) << (r.stopped_out ? "FAIL" : "ok") << std::endl;

            if (period.name == "H1") h1_result = r;
            if (period.name == "H2") h2_result = r;
            if (period.name == "Full") full_result = r;
        }

        // H1/H2 consistency analysis
        if (!h1_result.stopped_out && !h2_result.stopped_out &&
            h1_result.return_x > 0 && h2_result.return_x > 0) {
            double ratio = h1_result.return_x / h2_result.return_x;
            double consistency = std::min(ratio, 1.0 / ratio);  // 1.0 = perfect, <0.5 = concern

            std::cout << "\n  H1/H2 Consistency:" << std::endl;
            std::cout << "    H1 return: " << std::setprecision(2) << h1_result.return_x << "x" << std::endl;
            std::cout << "    H2 return: " << std::setprecision(2) << h2_result.return_x << "x" << std::endl;
            std::cout << "    Ratio (H1/H2): " << std::setprecision(2) << ratio << std::endl;
            std::cout << "    Consistency score: " << std::setprecision(2) << consistency
                      << (consistency > 0.7 ? " [GOOD - NOT OVERFIT]" :
                         consistency > 0.5 ? " [MODERATE - MONITOR]" :
                         consistency > 0.3 ? " [CONCERN - POSSIBLE OVERFIT]" :
                                            " [SEVERE - LIKELY OVERFIT]") << std::endl;

            // Annualize: if H1 and H2 both work, sequential compounding should match full
            double sequential = (h1_result.return_x) * (h2_result.return_x);
            std::cout << "    Sequential compound (H1*H2): " << std::setprecision(2) << sequential << "x" << std::endl;
            std::cout << "    Full year actual: " << std::setprecision(2) << full_result.return_x << "x" << std::endl;

            // Quarterly stability (coefficient of variation)
            std::vector<double> quarterly_returns;
            for (size_t ci = 0; ci < test_cases.size(); ci++) {
                auto& tc = test_cases[ci];
                if (tc.preset_idx != pi) continue;
                auto& r = all_results[ci];
                auto& period = periods[tc.period_idx];
                if (period.name.substr(0, 1) == "Q" && !r.stopped_out) {
                    quarterly_returns.push_back(r.return_x);
                }
            }

            if (quarterly_returns.size() >= 4) {
                double mean = 0;
                for (double v : quarterly_returns) mean += v;
                mean /= quarterly_returns.size();

                double var = 0;
                for (double v : quarterly_returns) var += (v - mean) * (v - mean);
                var /= quarterly_returns.size();
                double std_dev = std::sqrt(var);
                double cv = std_dev / mean;

                double min_q = *std::min_element(quarterly_returns.begin(), quarterly_returns.end());
                double max_q = *std::max_element(quarterly_returns.begin(), quarterly_returns.end());

                std::cout << "\n  Quarterly Stability:" << std::endl;
                std::cout << "    Mean quarterly return: " << std::setprecision(2) << mean << "x" << std::endl;
                std::cout << "    Std deviation: " << std::setprecision(3) << std_dev << std::endl;
                std::cout << "    CV (lower=more stable): " << std::setprecision(2) << cv
                          << (cv < 0.3 ? " [STABLE]" :
                             cv < 0.5 ? " [MODERATE]" :
                             cv < 0.8 ? " [VARIABLE]" :
                                        " [UNSTABLE]") << std::endl;
                std::cout << "    Min/Max quarterly: " << std::setprecision(2)
                          << min_q << "x / " << max_q << "x"
                          << " (ratio " << std::setprecision(2) << min_q / max_q << ")" << std::endl;
            }
        } else {
            if (h1_result.stopped_out || h2_result.stopped_out) {
                std::cout << "\n  *** STOPPED OUT in "
                          << (h1_result.stopped_out ? "H1" : "")
                          << (h1_result.stopped_out && h2_result.stopped_out ? " and " : "")
                          << (h2_result.stopped_out ? "H2" : "")
                          << " - STRATEGY NOT VIABLE ***" << std::endl;
            }
        }
    }

    // === Final Summary ===
    std::cout << "\n================================================================" << std::endl;
    std::cout << " FINAL OVERFITTING VERDICT" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::cout << std::setw(24) << "Preset"
              << std::setw(8) << "H1"
              << std::setw(8) << "H2"
              << std::setw(8) << "Full"
              << std::setw(9) << "H1/H2"
              << std::setw(8) << "Score"
              << std::setw(14) << "Verdict" << std::endl;
    std::cout << std::string(77, '-') << std::endl;

    for (size_t pi = 0; pi < presets.size(); pi++) {
        PeriodResult h1_r, h2_r, full_r;
        bool is_xauusd = (pi < 3);
        const auto& periods = is_xauusd ? xau_periods : xag_periods;

        for (size_t ci = 0; ci < test_cases.size(); ci++) {
            auto& tc = test_cases[ci];
            if (tc.preset_idx != pi) continue;
            auto& period = periods[tc.period_idx];
            if (period.name == "H1") h1_r = all_results[ci];
            if (period.name == "H2") h2_r = all_results[ci];
            if (period.name == "Full") full_r = all_results[ci];
        }

        double ratio = 0, consistency = 0;
        std::string verdict = "N/A";

        if (!h1_r.stopped_out && !h2_r.stopped_out && h1_r.return_x > 0 && h2_r.return_x > 0) {
            ratio = h1_r.return_x / h2_r.return_x;
            consistency = std::min(ratio, 1.0 / ratio);
            if (consistency > 0.7) verdict = "NOT OVERFIT";
            else if (consistency > 0.5) verdict = "MONITOR";
            else if (consistency > 0.3) verdict = "CONCERN";
            else verdict = "OVERFIT";
        } else {
            verdict = h1_r.stopped_out || h2_r.stopped_out ? "STOPPED OUT" : "ERROR";
        }

        std::cout << std::setw(24) << presets[pi].name
                  << std::setw(7) << std::fixed << std::setprecision(1) << h1_r.return_x << "x"
                  << std::setw(7) << h2_r.return_x << "x"
                  << std::setw(7) << full_r.return_x << "x"
                  << std::setw(8) << std::setprecision(2) << ratio
                  << std::setw(7) << std::setprecision(2) << consistency
                  << "  " << verdict << std::endl;
    }

    std::cout << "\nScoring: consistency = min(H1/H2, H2/H1)" << std::endl;
    std::cout << "  >0.70 = NOT OVERFIT (both halves perform similarly)" << std::endl;
    std::cout << "  0.50-0.70 = MONITOR (some asymmetry, may be regime-dependent)" << std::endl;
    std::cout << "  0.30-0.50 = CONCERN (significant asymmetry, possible overfitting)" << std::endl;
    std::cout << "  <0.30 = OVERFIT (one half dominates, parameters don't generalize)" << std::endl;

    std::cout << "\nCompleted in " << std::fixed << std::setprecision(1) << elapsed_sec << "s" << std::endl;

    return 0;
}
