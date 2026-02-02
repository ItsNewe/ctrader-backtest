/**
 * FillUp/FillDown Hedge Strategy Test
 *
 * Tests Long Gold (FillUp) + Short Silver (FillDown) hedge with multiple allocation modes:
 * 1. Equal lots - same lot size on both legs
 * 2. Survive-based - each strategy sizes independently
 * 3. Ratio-normalized - size so 1% ratio move = neutral P&L
 * 4. Equal margin - equal margin allocation
 * 5. Notional-weighted - equalize notional exposure
 */

#include "../include/fill_up_oscillation.h"
#include "../include/fill_down_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace backtest;

// Synchronized tick for both symbols
struct SyncedTick {
    std::string timestamp;
    Tick gold;
    Tick silver;
    bool has_gold;
    bool has_silver;
};

// Load ticks from file
std::vector<Tick> LoadTicks(const std::string& file_path) {
    std::vector<Tick> ticks;
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << file_path << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Tick tick;
        size_t pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);
        line = line.substr(pos + 1);

        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.bid = std::stod(line.substr(0, pos));
        line = line.substr(pos + 1);

        pos = line.find('\t');
        if (pos == std::string::npos) pos = line.length();
        tick.ask = std::stod(line.substr(0, pos));

        ticks.push_back(tick);
    }
    return ticks;
}

// Synchronize gold and silver ticks by timestamp (second-level precision)
std::vector<SyncedTick> SynchronizeTicks(
    const std::vector<Tick>& gold_ticks,
    const std::vector<Tick>& silver_ticks)
{
    std::vector<SyncedTick> synced;

    // Create map of timestamp (truncated to second) -> ticks
    std::map<std::string, SyncedTick> tick_map;

    auto truncate_ts = [](const std::string& ts) -> std::string {
        if (ts.length() >= 19) return ts.substr(0, 19);
        return ts;
    };

    for (const auto& tick : gold_ticks) {
        std::string key = truncate_ts(tick.timestamp);
        auto& st = tick_map[key];
        st.timestamp = key;
        st.gold = tick;
        st.has_gold = true;
    }

    for (const auto& tick : silver_ticks) {
        std::string key = truncate_ts(tick.timestamp);
        auto& st = tick_map[key];
        st.timestamp = key;
        st.silver = tick;
        st.has_silver = true;
    }

    // Extract only ticks where we have both
    for (const auto& [key, st] : tick_map) {
        if (st.has_gold && st.has_silver) {
            synced.push_back(st);
        }
    }

    // Sort by timestamp
    std::sort(synced.begin(), synced.end(),
        [](const SyncedTick& a, const SyncedTick& b) {
            return a.timestamp < b.timestamp;
        });

    return synced;
}

// Hedge result for one allocation mode
struct HedgeResult {
    std::string mode_name;
    double initial_balance;
    double gold_final;
    double silver_final;
    double combined_final;
    double gold_max_dd_pct;
    double silver_max_dd_pct;
    double combined_max_dd_pct;
    int gold_entries;
    int silver_entries;
    double gold_peak;
    double silver_peak;
    double combined_peak;
    bool survived;
};

// Run one hedge test with specified allocation mode
HedgeResult RunHedgeTest(
    const std::vector<SyncedTick>& ticks,
    const std::string& mode_name,
    double gold_max_volume,
    double silver_max_volume,
    double gold_survive_pct,
    double silver_survive_pct,
    double initial_balance)
{
    HedgeResult result;
    result.mode_name = mode_name;
    result.initial_balance = initial_balance;

    // Configure gold engine (long)
    TickBacktestConfig gold_config;
    gold_config.symbol = "XAUUSD";
    gold_config.initial_balance = initial_balance / 2.0;  // Split balance
    gold_config.contract_size = 100.0;
    gold_config.leverage = 500.0;
    gold_config.pip_size = 0.01;
    gold_config.swap_long = -78.57;
    gold_config.swap_short = 39.14;
    gold_config.start_date = "2024.01.01";
    gold_config.end_date = "2027.01.01";
    gold_config.verbose = false;

    // Configure silver engine (short)
    TickBacktestConfig silver_config;
    silver_config.symbol = "XAGUSD";
    silver_config.initial_balance = initial_balance / 2.0;  // Split balance
    silver_config.contract_size = 5000.0;
    silver_config.leverage = 500.0;
    silver_config.pip_size = 0.001;
    silver_config.swap_long = -22.34;
    silver_config.swap_short = 0.13;
    silver_config.start_date = "2024.01.01";
    silver_config.end_date = "2027.01.01";
    silver_config.verbose = false;

    TickBasedEngine gold_engine(gold_config);
    TickBasedEngine silver_engine(silver_config);

    // Configure gold strategy (FillUp - long)
    FillUpOscillation::Config gold_strat_config;
    gold_strat_config.survive_pct = gold_survive_pct;
    gold_strat_config.base_spacing = 1.5;
    gold_strat_config.min_volume = 0.01;
    gold_strat_config.max_volume = gold_max_volume;
    gold_strat_config.contract_size = 100.0;
    gold_strat_config.leverage = 500.0;
    gold_strat_config.mode = FillUpOscillation::ADAPTIVE_SPACING;
    gold_strat_config.volatility_lookback_hours = 4.0;
    gold_strat_config.adaptive.typical_vol_pct = 0.55;
    gold_strat_config.safety.force_min_volume_entry = true;

    FillUpOscillation gold_strategy(gold_strat_config);

    // Configure silver strategy (FillDown - short)
    FillDownOscillation::Config silver_strat_config;
    silver_strat_config.survive_pct = silver_survive_pct;
    silver_strat_config.base_spacing = 0.5;  // % of price
    silver_strat_config.min_volume = 0.01;
    silver_strat_config.max_volume = silver_max_volume;
    silver_strat_config.contract_size = 5000.0;
    silver_strat_config.leverage = 500.0;
    silver_strat_config.mode = FillDownOscillation::ADAPTIVE_SPACING;
    silver_strat_config.adaptive.pct_spacing = true;
    silver_strat_config.adaptive.typical_vol_pct = 0.45;
    silver_strat_config.force_min_volume_entry = true;

    FillDownOscillation silver_strategy(silver_strat_config);

    // Track combined equity
    double combined_peak = initial_balance;
    double combined_max_dd_pct = 0.0;

    // Convert synced ticks to separate vectors
    std::vector<Tick> gold_tick_vec, silver_tick_vec;
    for (const auto& st : ticks) {
        Tick gt = st.gold;
        gt.timestamp = st.timestamp;
        gold_tick_vec.push_back(gt);

        Tick st_tick = st.silver;
        st_tick.timestamp = st.timestamp;
        silver_tick_vec.push_back(st_tick);
    }

    // Run gold engine with FillUp strategy
    gold_engine.RunWithTicks(gold_tick_vec, [&gold_strategy](const Tick& tick, TickBasedEngine& eng) {
        gold_strategy.OnTick(tick, eng);
    });

    // Run silver engine with FillDown strategy
    silver_engine.RunWithTicks(silver_tick_vec, [&silver_strategy](const Tick& tick, TickBasedEngine& eng) {
        silver_strategy.OnTick(tick, eng);
    });

    // Collect results
    auto gold_results = gold_engine.GetResults();
    auto silver_results = silver_engine.GetResults();

    result.gold_final = gold_results.final_balance;
    result.silver_final = silver_results.final_balance;
    result.combined_final = result.gold_final + result.silver_final;
    result.gold_max_dd_pct = gold_results.max_drawdown_pct;
    result.silver_max_dd_pct = silver_results.max_drawdown_pct;
    result.combined_max_dd_pct = combined_max_dd_pct;
    result.gold_entries = gold_strategy.GetStats().forced_entries + gold_strategy.GetStats().peak_positions;
    result.silver_entries = silver_strategy.GetStats().total_entries;
    result.gold_peak = gold_strategy.GetPeakEquity();
    result.silver_peak = silver_strategy.GetStats().peak_equity;
    result.combined_peak = combined_peak;
    result.survived = (result.gold_final > 0 && result.silver_final > 0);

    return result;
}

void PrintResult(const HedgeResult& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== " << r.mode_name << " ===" << std::endl;
    std::cout << "Initial: $" << r.initial_balance << std::endl;
    std::cout << "Gold:   $" << r.gold_final << " (" << (r.gold_final / (r.initial_balance/2)) << "x)"
              << " DD=" << r.gold_max_dd_pct << "% Entries=" << r.gold_entries << std::endl;
    std::cout << "Silver: $" << r.silver_final << " (" << (r.silver_final / (r.initial_balance/2)) << "x)"
              << " DD=" << r.silver_max_dd_pct << "% Entries=" << r.silver_entries << std::endl;
    std::cout << "Combined: $" << r.combined_final << " (" << (r.combined_final / r.initial_balance) << "x)"
              << " DD=" << r.combined_max_dd_pct << "%" << std::endl;
    std::cout << "Survived: " << (r.survived ? "YES" : "NO") << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "==================================================" << std::endl;
    std::cout << "   FillUp/FillDown Hedge Strategy Test" << std::endl;
    std::cout << "   Long Gold + Short Silver" << std::endl;
    std::cout << "==================================================" << std::endl;

    // Check command line for which data to use
    bool use_full_2025 = false;
    if (argc > 1 && std::string(argv[1]) == "full") {
        use_full_2025 = true;
    }

    // Load tick data
    std::cout << "\nLoading gold ticks..." << std::endl;
    std::string gold_path = use_full_2025
        ? "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv"
        : "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_RECENT.csv";
    auto gold_ticks = LoadTicks(gold_path);
    std::cout << "Loaded " << gold_ticks.size() << " gold ticks from " << gold_path << std::endl;

    std::cout << "Loading silver ticks..." << std::endl;
    std::string silver_path = use_full_2025
        ? "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_MT5_EXPORT.csv"
        : "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_RECENT.csv";
    auto silver_ticks = LoadTicks(silver_path);
    std::cout << "Loaded " << silver_ticks.size() << " silver ticks from " << silver_path << std::endl;

    // Synchronize
    std::cout << "Synchronizing ticks..." << std::endl;
    auto synced = SynchronizeTicks(gold_ticks, silver_ticks);
    std::cout << "Synchronized " << synced.size() << " tick pairs" << std::endl;

    if (synced.empty()) {
        std::cerr << "No synchronized ticks!" << std::endl;
        return 1;
    }

    // Show price ranges
    double gold_start = synced.front().gold.bid;
    double gold_end = synced.back().gold.bid;
    double silver_start = synced.front().silver.bid;
    double silver_end = synced.back().silver.bid;

    std::cout << "\nPrice range:" << std::endl;
    std::cout << "Gold:   $" << gold_start << " -> $" << gold_end
              << " (" << ((gold_end - gold_start) / gold_start * 100) << "%)" << std::endl;
    std::cout << "Silver: $" << silver_start << " -> $" << silver_end
              << " (" << ((silver_end - silver_start) / silver_start * 100) << "%)" << std::endl;
    std::cout << "Period: " << synced.front().timestamp << " to " << synced.back().timestamp << std::endl;

    double initial_balance = 10000.0;
    std::vector<HedgeResult> results;

    // Mode 1: Equal lots (conservative)
    std::cout << "\n--- Testing Mode 1: Equal Lots ---" << std::endl;
    results.push_back(RunHedgeTest(synced, "Equal Lots (1 lot each)",
        1.0, 1.0,    // max_volume: 1 lot each
        15.0, 20.0,  // survive_pct
        initial_balance));

    // Mode 2: Survive-based sizing (independent)
    std::cout << "--- Testing Mode 2: Survive-Based ---" << std::endl;
    results.push_back(RunHedgeTest(synced, "Survive-Based (5 lots max)",
        5.0, 5.0,    // max_volume: 5 lots each
        15.0, 20.0,  // survive_pct
        initial_balance));

    // Mode 3: Ratio-normalized (equalize notional per lot considering contract sizes)
    // Gold: 1 lot = 100 oz * $5000 = $500K notional
    // Silver: 1 lot = 5000 oz * $100 = $500K notional (roughly equal at current prices)
    // So for equal notional, we can use similar lot sizes
    std::cout << "--- Testing Mode 3: Ratio-Normalized ---" << std::endl;
    results.push_back(RunHedgeTest(synced, "Ratio-Normalized (gold=5, silver=1)",
        5.0, 1.0,    // Gold 5 lots, Silver 1 lot (gold contract 50x smaller)
        15.0, 25.0,  // survive_pct (higher for silver due to bigger moves)
        initial_balance));

    // Mode 4: Equal margin allocation
    std::cout << "--- Testing Mode 4: Equal Margin ---" << std::endl;
    // Gold margin per lot: $5000 * 100 / 500 = $1000
    // Silver margin per lot: $100 * 5000 / 500 = $1000 (similar at these prices)
    results.push_back(RunHedgeTest(synced, "Equal Margin (2 lots each)",
        2.0, 2.0,
        15.0, 20.0,
        initial_balance));

    // Mode 5: Aggressive (higher volumes)
    std::cout << "--- Testing Mode 5: Aggressive ---" << std::endl;
    results.push_back(RunHedgeTest(synced, "Aggressive (10 lots each)",
        10.0, 10.0,
        10.0, 15.0,  // Lower survive = bigger positions
        initial_balance));

    // Print summary
    std::cout << "\n\n========================================" << std::endl;
    std::cout << "           RESULTS SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;

    for (const auto& r : results) {
        PrintResult(r);
    }

    // Find best mode
    std::cout << "\n\n=== BEST RESULTS ===" << std::endl;

    auto best_return = std::max_element(results.begin(), results.end(),
        [](const HedgeResult& a, const HedgeResult& b) {
            if (!a.survived) return true;
            if (!b.survived) return false;
            return a.combined_final < b.combined_final;
        });

    auto best_dd = std::min_element(results.begin(), results.end(),
        [](const HedgeResult& a, const HedgeResult& b) {
            if (!a.survived) return false;
            if (!b.survived) return true;
            return a.combined_max_dd_pct < b.combined_max_dd_pct;
        });

    std::cout << "Best Return: " << best_return->mode_name
              << " (" << (best_return->combined_final / best_return->initial_balance) << "x)" << std::endl;
    std::cout << "Lowest DD:   " << best_dd->mode_name
              << " (" << best_dd->combined_max_dd_pct << "%)" << std::endl;

    return 0;
}
