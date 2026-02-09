#include "../include/strategy_parallel_dual.h"
#include "../include/strategy_parallel_dual_original.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

using namespace backtest;

std::vector<Tick> g_ticks;

void LoadTickData() {
    std::cout << "Loading XAGUSD tick data..." << std::endl;
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickDataManager manager(tick_config);
    manager.Reset();

    Tick tick;
    while (manager.GetNextTick(tick)) {
        g_ticks.push_back(tick);
    }
    std::cout << "Loaded " << g_ticks.size() << " ticks" << std::endl;
    if (!g_ticks.empty())
        std::cout << "Price: $" << g_ticks.front().bid << " -> $" << g_ticks.back().bid << std::endl;
}

struct Result {
    double p1, p2, p3, p4;
    double equity, ret, dd;
    int entries;
    bool stop_out;
};

Result RunOriginal(double survive, double grid_alloc, double spacing_pct, double tp_pct) {
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

    // Use percentage-based spacing (silver price varies greatly)
    double ref_price = 50.0;  // Mid-range reference
    double spacing = ref_price * spacing_pct / 100.0;
    double tp_dist = ref_price * tp_pct / 100.0;

    ParallelDualStrategyOriginal::Config sc;
    sc.survive_pct = survive;
    sc.grid_allocation = grid_alloc;
    sc.momentum_allocation = 1.0 - grid_alloc;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 5000.0;
    sc.leverage = 500.0;
    sc.base_spacing = spacing;
    sc.momentum_spacing = spacing * 3.0;
    sc.force_min_volume_entry = false;
    sc.use_take_profit = true;
    sc.tp_distance = tp_dist;

    ParallelDualStrategyOriginal strategy(sc);
    engine.RunWithTicks(g_ticks, [&](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto r = engine.GetResults();
    auto s = strategy.GetStats();

    double unreal = 0;
    for (const Trade* t : engine.GetOpenPositions())
        unreal += (g_ticks.back().bid - t->entry_price) * t->lot_size * 5000.0;

    Result res;
    res.p1 = survive; res.p2 = grid_alloc; res.p3 = spacing; res.p4 = tp_dist;
    res.equity = r.final_balance + unreal;
    res.ret = res.equity / 10000.0;
    res.dd = r.max_drawdown_pct;
    res.entries = s.total_entries;
    res.stop_out = r.stop_out_occurred;
    return res;
}

Result RunUnified(double survive, double min_spacing_pct, double safety_buffer) {
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

    // Use percentage-based spacing
    double ref_price = 50.0;
    double min_spacing = ref_price * min_spacing_pct / 100.0;

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
    sc.safety_buffer = safety_buffer;

    ParallelDualStrategy strategy(sc);
    engine.RunWithTicks(g_ticks, [&](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto r = engine.GetResults();
    auto s = strategy.GetStats();

    double unreal = 0;
    for (const Trade* t : engine.GetOpenPositions())
        unreal += (g_ticks.back().bid - t->entry_price) * t->lot_size * 5000.0;

    Result res;
    res.p1 = survive; res.p2 = min_spacing; res.p3 = safety_buffer; res.p4 = 0;
    res.equity = r.final_balance + unreal;
    res.ret = res.equity / 10000.0;
    res.dd = r.max_drawdown_pct;
    res.entries = s.total_entries;
    res.stop_out = r.stop_out_occurred;
    return res;
}

int main() {
    std::cout << "=== XAGUSD Parameter Optimization ===" << std::endl;
    LoadTickData();
    if (g_ticks.empty()) return 1;

    std::vector<Result> orig_results, unif_results;

    std::cout << "\n=== ORIGINAL STRATEGY SWEEP (%-based) ===" << std::endl;
    std::vector<double> survives = {15, 18, 20, 22, 25, 30};
    std::vector<double> grid_allocs = {0.01, 0.02, 0.03, 0.04, 0.05};  // Very conservative
    std::vector<double> spacings = {0.5, 1.0, 1.5, 2.0, 2.5};  // % of price
    std::vector<double> tp_dists = {0.3, 0.5, 0.8, 1.0, 1.5};  // % of price

    int total = survives.size() * grid_allocs.size() * spacings.size() * tp_dists.size();
    int cnt = 0;
    for (double surv : survives)
      for (double grid : grid_allocs)
        for (double sp : spacings)
          for (double tp : tp_dists) {
            orig_results.push_back(RunOriginal(surv, grid, sp, tp));
            if (++cnt % 100 == 0) std::cout << cnt << "/" << total << std::endl;
          }

    std::sort(orig_results.begin(), orig_results.end(), [](const Result& a, const Result& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.ret > b.ret;
    });

    std::cout << "\n=== TOP 15 ORIGINAL ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "surv%  grid%  spac$  TP$    Return    DD%     Entries" << std::endl;
    for (int i = 0; i < 15 && i < (int)orig_results.size(); i++) {
        const Result& r = orig_results[i];
        if (r.stop_out) std::cout << r.p1 << "   " << r.p2*100 << "   " << r.p3 << "   " << r.p4 << "   STOP-OUT\n";
        else std::cout << r.p1 << "   " << r.p2*100 << "   " << r.p3 << "   " << r.p4 << "   " << r.ret << "x   " << r.dd << "   " << r.entries << std::endl;
    }

    std::cout << "\n=== UNIFIED STRATEGY SWEEP (%-based) ===" << std::endl;
    std::vector<double> min_sps = {0.3, 0.5, 0.8, 1.0, 1.5};  // % of price
    std::vector<double> safeties = {1.5, 2.0, 2.5, 3.0};

    for (double surv : survives)
      for (double sp : min_sps)
        for (double saf : safeties)
          unif_results.push_back(RunUnified(surv, sp, saf));

    std::sort(unif_results.begin(), unif_results.end(), [](const Result& a, const Result& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.ret > b.ret;
    });

    std::cout << "\n=== TOP 15 UNIFIED ===" << std::endl;
    std::cout << "surv%  spacing  safety  Return    DD%     Entries" << std::endl;
    for (int i = 0; i < 15 && i < (int)unif_results.size(); i++) {
        const Result& r = unif_results[i];
        if (r.stop_out) std::cout << r.p1 << "   " << r.p2 << "   " << r.p3 << "x   STOP-OUT\n";
        else std::cout << r.p1 << "   " << r.p2 << "   " << r.p3 << "x   " << r.ret << "x   " << r.dd << "   " << r.entries << std::endl;
    }

    std::cout << "\n=== BEST CONFIGURATIONS ===" << std::endl;
    if (!orig_results.empty() && !orig_results[0].stop_out) {
        const Result& b = orig_results[0];
        std::cout << "ORIGINAL: survive=" << b.p1 << "%, grid=" << b.p2*100 << "%, spacing=$" << b.p3 << ", TP=$" << b.p4 << " -> " << b.ret << "x, DD=" << b.dd << "%" << std::endl;
    }
    if (!unif_results.empty() && !unif_results[0].stop_out) {
        const Result& b = unif_results[0];
        std::cout << "UNIFIED: survive=" << b.p1 << "%, spacing=$" << b.p2 << ", safety=" << b.p3 << "x -> " << b.ret << "x, DD=" << b.dd << "%" << std::endl;
    }

    return 0;
}
