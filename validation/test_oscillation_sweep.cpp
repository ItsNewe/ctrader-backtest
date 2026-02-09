/**
 * FillUpOscillation Parameter Sweep
 *
 * Tests the strategy with percentage-based typical volatility
 * across multiple parameter combinations.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

using namespace backtest;

struct SweepResult {
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    int mode;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    double total_swap;
    int total_trades;
    bool stopped_out;
};

void RunSweep(const std::string& data_path, const std::string& start_date,
              const std::string& end_date, const std::string& period_name,
              std::vector<SweepResult>& results) {

    // Parameter ranges
    std::vector<double> survive_pcts = {10.0, 13.0, 15.0, 18.0, 20.0};
    std::vector<double> spacings = {1.0, 1.5, 2.0, 2.5};
    std::vector<double> lookbacks = {1.0, 2.0, 4.0};
    std::vector<int> modes = {0, 1};  // BASELINE, ADAPTIVE_SPACING

    double initial_balance = 10000.0;

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "SWEEP: " << period_name << " (" << start_date << " to " << end_date << ")" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::setw(8) << "Survive"
              << std::setw(8) << "Space"
              << std::setw(8) << "Lookbk"
              << std::setw(10) << "Mode"
              << std::setw(12) << "Final$"
              << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(10) << "Swap"
              << std::setw(8) << "Trades"
              << std::setw(8) << "Status"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    int config_num = 0;
    int total_configs = survive_pcts.size() * spacings.size() * lookbacks.size() * modes.size();

    for (double survive : survive_pcts) {
        for (double spacing : spacings) {
            for (double lookback : lookbacks) {
                for (int mode : modes) {
                    config_num++;

                    // Skip lookback variations for BASELINE mode (it doesn't use lookback)
                    if (mode == 0 && lookback != 4.0) continue;

                    TickDataConfig tick_config;
                    tick_config.file_path = data_path;
                    tick_config.format = TickDataFormat::MT5_CSV;
                    tick_config.load_all_into_memory = false;

                    TickBacktestConfig config;
                    config.symbol = "XAUUSD";
                    config.initial_balance = initial_balance;
                    config.account_currency = "USD";
                    config.contract_size = 100.0;
                    config.leverage = 500.0;
                    config.margin_rate = 1.0;
                    config.pip_size = 0.01;
                    config.swap_long = -66.99;
                    config.swap_short = 41.2;
                    config.swap_mode = 1;
                    config.swap_3days = 3;
                    config.start_date = start_date;
                    config.end_date = end_date;
                    config.tick_data_config = tick_config;
                    config.verbose = false;  // Suppress trade logging for sweep

                    try {
                        TickBasedEngine engine(config);

                        FillUpOscillation strategy(
                            survive,
                            spacing,
                            0.01,   // min_volume
                            10.0,   // max_volume
                            100.0,  // contract_size
                            500.0,  // leverage
                            static_cast<FillUpOscillation::Mode>(mode),
                            0.1,    // antifragile_scale
                            30.0,   // velocity_threshold
                            lookback
                        );

                        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                            strategy.OnTick(tick, eng);
                        });

                        auto res = engine.GetResults();

                        SweepResult sr;
                        sr.survive_pct = survive;
                        sr.base_spacing = spacing;
                        sr.lookback_hours = lookback;
                        sr.mode = mode;
                        sr.final_balance = res.final_balance;
                        sr.return_multiple = res.final_balance / initial_balance;
                        sr.max_dd_pct = (res.max_drawdown / initial_balance) * 100.0;
                        sr.total_swap = res.total_swap_charged;
                        sr.total_trades = res.total_trades;
                        sr.stopped_out = engine.IsStopOutOccurred();

                        results.push_back(sr);

                        std::cout << std::fixed << std::setprecision(1)
                                  << std::setw(7) << survive << "%"
                                  << std::setw(7) << "$" << spacing
                                  << std::setw(7) << lookback << "h"
                                  << std::setw(10) << (mode == 0 ? "BASE" : "ADAPTIVE")
                                  << std::setprecision(0)
                                  << std::setw(11) << "$" << res.final_balance
                                  << std::setprecision(2)
                                  << std::setw(7) << sr.return_multiple << "x"
                                  << std::setprecision(1)
                                  << std::setw(7) << sr.max_dd_pct << "%"
                                  << std::setprecision(0)
                                  << std::setw(9) << "$" << res.total_swap_charged
                                  << std::setw(8) << res.total_trades
                                  << std::setw(8) << (sr.stopped_out ? "STOP" : "OK")
                                  << std::endl;

                    } catch (const std::exception& e) {
                        std::cerr << "Error at config " << config_num << ": " << e.what() << std::endl;
                    }
                }
            }
        }
    }
}

int main() {
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "FILLUP OSCILLATION PARAMETER SWEEP" << std::endl;
    std::cout << "With percentage-based typical volatility (0.5% of price)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::vector<SweepResult> all_results;

    // Full year 2025
    RunSweep(
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "2025.01.01", "2025.12.30",
        "Full Year 2025",
        all_results
    );

    // Summary statistics
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Count survivors
    int survivors = 0;
    int stopped = 0;
    double best_return = 0;
    SweepResult best_config;

    for (const auto& r : all_results) {
        if (r.stopped_out) {
            stopped++;
        } else {
            survivors++;
            if (r.return_multiple > best_return) {
                best_return = r.return_multiple;
                best_config = r;
            }
        }
    }

    std::cout << "Total configurations: " << all_results.size() << std::endl;
    std::cout << "Survived: " << survivors << " (" << (100.0 * survivors / all_results.size()) << "%)" << std::endl;
    std::cout << "Stopped out: " << stopped << std::endl;

    if (survivors > 0) {
        std::cout << "\nBest configuration:" << std::endl;
        std::cout << "  Survive: " << best_config.survive_pct << "%" << std::endl;
        std::cout << "  Spacing: $" << best_config.base_spacing << std::endl;
        std::cout << "  Lookback: " << best_config.lookback_hours << "h" << std::endl;
        std::cout << "  Mode: " << (best_config.mode == 0 ? "BASELINE" : "ADAPTIVE_SPACING") << std::endl;
        std::cout << "  Return: " << std::fixed << std::setprecision(2) << best_config.return_multiple << "x" << std::endl;
        std::cout << "  Max DD: " << std::setprecision(1) << best_config.max_dd_pct << "%" << std::endl;
    }

    // Group by survive_pct
    std::cout << "\nBy survive_pct (ADAPTIVE mode, spacing=$1.5, lookback=4h):" << std::endl;
    for (const auto& r : all_results) {
        if (r.mode == 1 && r.base_spacing == 1.5 && r.lookback_hours == 4.0) {
            std::cout << "  " << r.survive_pct << "%: "
                      << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                      << " (DD: " << std::setprecision(1) << r.max_dd_pct << "%)"
                      << (r.stopped_out ? " STOPPED" : "")
                      << std::endl;
        }
    }

    // Group by spacing
    std::cout << "\nBy spacing (ADAPTIVE mode, survive=13%, lookback=4h):" << std::endl;
    for (const auto& r : all_results) {
        if (r.mode == 1 && r.survive_pct == 13.0 && r.lookback_hours == 4.0) {
            std::cout << "  $" << r.base_spacing << ": "
                      << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                      << " (DD: " << std::setprecision(1) << r.max_dd_pct << "%)"
                      << std::endl;
        }
    }

    // Group by lookback
    std::cout << "\nBy lookback (ADAPTIVE mode, survive=13%, spacing=$1.5):" << std::endl;
    for (const auto& r : all_results) {
        if (r.mode == 1 && r.survive_pct == 13.0 && r.base_spacing == 1.5) {
            std::cout << "  " << r.lookback_hours << "h: "
                      << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                      << " (DD: " << std::setprecision(1) << r.max_dd_pct << "%)"
                      << std::endl;
        }
    }

    // BASELINE vs ADAPTIVE comparison
    std::cout << "\nBASELINE vs ADAPTIVE (survive=13%, spacing=$1.5):" << std::endl;
    for (const auto& r : all_results) {
        if (r.survive_pct == 13.0 && r.base_spacing == 1.5 && r.lookback_hours == 4.0) {
            std::cout << "  " << (r.mode == 0 ? "BASELINE" : "ADAPTIVE") << ": "
                      << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                      << " (DD: " << std::setprecision(1) << r.max_dd_pct << "%)"
                      << std::endl;
        }
    }

    return 0;
}
