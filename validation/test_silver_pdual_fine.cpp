#include "../include/strategy_parallel_dual.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

using namespace backtest;

std::vector<Tick> g_ticks;

void LoadTickData() {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    TickDataManager manager(tick_config);
    manager.Reset();
    Tick tick;
    while (manager.GetNextTick(tick)) g_ticks.push_back(tick);
    std::cout << "Loaded " << g_ticks.size() << " ticks, $" << g_ticks.front().bid << " -> $" << g_ticks.back().bid << std::endl;
}

struct Result { double survive, spacing, safety, ret, dd; int entries; bool stop_out; };

Result Run(double survive, double spacing_pct, double safety) {
    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.pip_size = 0.001;
    config.swap_long = -15.0;
    config.swap_short = 13.72;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.verbose = false;

    TickBasedEngine engine(config);

    double ref_price = 50.0;
    double min_spacing = ref_price * spacing_pct / 100.0;

    ParallelDualStrategy::Config sc;
    sc.survive_pct = survive;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 5000.0;
    sc.leverage = 500.0;
    sc.min_spacing = min_spacing;
    sc.max_spacing = min_spacing * 10.0;
    sc.base_spacing = min_spacing * 2.0;
    sc.target_trades_in_range = 20;
    sc.margin_stop_out = 20.0;
    sc.safety_buffer = safety;

    ParallelDualStrategy strategy(sc);
    engine.RunWithTicks(g_ticks, [&](const Tick& tick, TickBasedEngine& eng) { strategy.OnTick(tick, eng); });

    auto r = engine.GetResults();
    auto s = strategy.GetStats();
    double unreal = 0;
    for (const Trade* t : engine.GetOpenPositions())
        unreal += (g_ticks.back().bid - t->entry_price) * t->lot_size * 5000.0;

    Result res;
    res.survive = survive; res.spacing = spacing_pct; res.safety = safety;
    res.ret = (r.final_balance + unreal) / 10000.0;
    res.dd = r.max_drawdown_pct;
    res.entries = s.total_entries;
    res.stop_out = r.stop_out_occurred;
    return res;
}

int main() {
    std::cout << "=== XAGUSD Fine Optimization (UNIFIED) ===" << std::endl;
    LoadTickData();

    std::vector<Result> results;
    std::vector<double> survives = {15, 16, 17, 18, 19, 20, 22};
    std::vector<double> spacings = {0.08, 0.10, 0.12, 0.15, 0.18, 0.20, 0.25};
    std::vector<double> safeties = {1.2, 1.3, 1.4, 1.5, 1.6, 1.8, 2.0};

    for (double surv : survives)
      for (double sp : spacings)
        for (double saf : safeties)
          results.push_back(Run(surv, sp, saf));

    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.ret > b.ret;
    });

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== TOP 20 (sorted by return) ===" << std::endl;
    std::cout << "survive  spacing  safety   Return    DD%   Entries" << std::endl;
    for (int i = 0; i < 20 && i < (int)results.size(); i++) {
        const Result& r = results[i];
        if (r.stop_out) continue;
        std::cout << std::setw(5) << r.survive << "%   " << std::setw(5) << r.spacing << "%   "
                  << std::setw(4) << r.safety << "x   " << std::setw(6) << r.ret << "x   "
                  << std::setw(5) << r.dd << "   " << r.entries << std::endl;
    }

    // Find best risk-adjusted (return / DD)
    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return (a.ret / a.dd) > (b.ret / b.dd);
    });

    std::cout << "\n=== TOP 10 RISK-ADJUSTED (Return/DD) ===" << std::endl;
    for (int i = 0; i < 10 && i < (int)results.size(); i++) {
        const Result& r = results[i];
        if (r.stop_out) continue;
        std::cout << std::setw(5) << r.survive << "%   " << std::setw(5) << r.spacing << "%   "
                  << std::setw(4) << r.safety << "x   " << std::setw(6) << r.ret << "x   "
                  << std::setw(5) << r.dd << "   ratio=" << (r.ret/r.dd) << std::endl;
    }

    std::cout << "\n=== BEST CONFIGURATION ===" << std::endl;
    const Result& best = results[0];
    std::cout << "survive_pct = " << best.survive << "%" << std::endl;
    std::cout << "min_spacing_pct = " << best.spacing << "% ($" << (50.0 * best.spacing / 100.0) << " at $50)" << std::endl;
    std::cout << "safety_buffer = " << best.safety << "x" << std::endl;
    std::cout << "Result: " << best.ret << "x return, " << best.dd << "% DD" << std::endl;

    return 0;
}
