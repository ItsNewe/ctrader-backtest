#include "../include/strategy_adaptive_regime_grid.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

using namespace backtest;

struct SweepResult {
    double survive_pct;
    double base_spacing;
    double dd_halt;
    double er_threshold;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    double risk_adjusted;
    int total_trades;
    int regime_changes;
    double peak_dd_pct;
    bool stop_out;
};

int main() {
    std::cout << "=== AdaptiveRegimeGrid Parameter Sweep ===" << std::endl;
    std::cout << "Loading tick data..." << std::endl;

    // Load ticks once into memory
    TickDataConfig tick_cfg;
    tick_cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_cfg.format = TickDataFormat::MT5_CSV;

    TickDataManager mgr(tick_cfg);
    std::vector<Tick> ticks;
    Tick tick;
    while (mgr.GetNextTick(tick)) {
        ticks.push_back(tick);
    }
    std::cout << "Loaded " << ticks.size() << " ticks." << std::endl;

    // Define parameter grid
    std::vector<double> survive_pcts = {10.0, 13.0, 16.0, 19.0};
    std::vector<double> base_spacings = {1.0, 1.5, 2.0};
    std::vector<double> dd_halts = {40.0, 50.0, 60.0};
    std::vector<double> er_thresholds = {0.3, 0.4, 0.5};

    int total_configs = (int)(survive_pcts.size() * base_spacings.size() *
                              dd_halts.size() * er_thresholds.size());
    std::cout << "Running " << total_configs << " configurations..." << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    std::vector<SweepResult> results;
    results.reserve(total_configs);
    int run_count = 0;

    for (double survive : survive_pcts) {
        for (double spacing : base_spacings) {
            for (double dd_halt : dd_halts) {
                for (double er_thresh : er_thresholds) {
                    run_count++;
                    if (run_count % 10 == 0) {
                        std::cout << "  Running config " << run_count << "/" << total_configs << "..." << std::endl;
                    }

                    // Configure engine
                    TickBacktestConfig engine_cfg;
                    engine_cfg.symbol = "XAUUSD";
                    engine_cfg.initial_balance = 10000.0;
                    engine_cfg.contract_size = 100.0;
                    engine_cfg.leverage = 500.0;
                    engine_cfg.pip_size = 0.01;
                    engine_cfg.swap_long = -66.99;
                    engine_cfg.swap_short = 41.2;
                    engine_cfg.swap_mode = 1;
                    engine_cfg.swap_3days = 3;
                    engine_cfg.start_date = "2025.01.01";
                    engine_cfg.end_date = "2025.12.30";
                    engine_cfg.verbose = false;

                    // Configure strategy
                    AdaptiveRegimeGrid::Config strat_cfg;
                    strat_cfg.survive_pct = survive;
                    strat_cfg.base_spacing = spacing;
                    strat_cfg.dd_halt_threshold = dd_halt;
                    strat_cfg.dd_reduce_threshold = dd_halt - 10.0;
                    strat_cfg.dd_liquidate_threshold = dd_halt + 10.0;
                    strat_cfg.er_trending_threshold = er_thresh;
                    strat_cfg.er_ranging_threshold = er_thresh - 0.2;

                    AdaptiveRegimeGrid strategy(strat_cfg);
                    TickBasedEngine engine(engine_cfg);

                    engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& eng) {
                        strategy.OnTick(t, eng);
                    });

                    auto res = engine.GetResults();
                    const auto& stats = strategy.GetStats();

                    SweepResult sr;
                    sr.survive_pct = survive;
                    sr.base_spacing = spacing;
                    sr.dd_halt = dd_halt;
                    sr.er_threshold = er_thresh;
                    sr.final_balance = res.final_balance;
                    sr.return_mult = res.final_balance / 10000.0;
                    sr.max_dd_pct = res.max_drawdown_pct;
                    sr.risk_adjusted = sr.return_mult / (sr.max_dd_pct / 100.0 + 0.01);
                    sr.total_trades = (int)res.total_trades;
                    sr.regime_changes = (int)stats.regime_changes;
                    sr.peak_dd_pct = stats.peak_dd_pct;
                    sr.stop_out = res.stop_out_occurred;

                    results.push_back(sr);
                }
            }
        }
    }

    // Sort by risk-adjusted return (descending)
    std::sort(results.begin(), results.end(),
              [](const SweepResult& a, const SweepResult& b) {
                  return a.risk_adjusted > b.risk_adjusted;
              });

    // Print results table
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS (sorted by risk-adjusted return)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::setw(8)  << "Surv%"
              << std::setw(8)  << "Space"
              << std::setw(8)  << "DDHalt"
              << std::setw(8)  << "ER"
              << std::setw(12) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(12) << "RiskAdj"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Regimes"
              << std::setw(8)  << "StopOut"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::setw(8)  << r.survive_pct
                  << std::setw(8)  << r.base_spacing
                  << std::setw(8)  << r.dd_halt
                  << std::setw(8)  << r.er_threshold
                  << std::setw(11) << r.return_mult << "x"
                  << std::setw(10) << r.max_dd_pct
                  << std::setw(12) << r.risk_adjusted
                  << std::setw(10) << r.total_trades
                  << std::setw(10) << r.regime_changes
                  << std::setw(8)  << (r.stop_out ? "YES" : "no")
                  << std::endl;
    }

    // Print top 5 summary
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "TOP 5 CONFIGURATIONS" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    for (int i = 0; i < 5 && i < (int)results.size(); ++i) {
        const auto& r = results[i];
        std::cout << "#" << (i + 1)
                  << " survive=" << r.survive_pct << "%"
                  << " spacing=$" << r.base_spacing
                  << " dd_halt=" << r.dd_halt << "%"
                  << " er=" << r.er_threshold
                  << " -> " << r.return_mult << "x return"
                  << " / " << r.max_dd_pct << "% DD"
                  << " (risk_adj=" << r.risk_adjusted << ")"
                  << std::endl;
    }

    std::cout << "\nSweep complete. " << total_configs << " configurations tested." << std::endl;
    return 0;
}
