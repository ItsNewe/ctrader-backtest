//+------------------------------------------------------------------+
//| Test force_min_volume_entry mitigation options                   |
//| Compare: no force vs force at different survive% levels          |
//| Goal: Find configuration that survives Oct crash with minimal    |
//|       profit sacrifice from full-year performance                |
//+------------------------------------------------------------------+
#include "../include/tick_based_engine.h"
#include "../include/fill_up_oscillation.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace backtest;

struct TestResult {
    std::string config_name;
    double survive_pct;
    bool force_entry;
    double final_balance;
    double max_dd_pct;
    double return_mult;
    int trades;
    int forced_entries;
    bool stopped_out;
};

TestResult RunTest(const std::string& start_date, const std::string& end_date,
                   double survive_pct, bool force_entry, const std::string& config_name) {

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
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

    TestResult result;
    result.config_name = config_name;
    result.survive_pct = survive_pct;
    result.force_entry = force_entry;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(survive_pct, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

        // Configure force_min_volume_entry
        FillUpOscillation::SafetyConfig safety_cfg;
        safety_cfg.force_min_volume_entry = force_entry;
        strategy.SetSafetyConfig(safety_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        auto stats = strategy.GetStats();

        result.final_balance = res.final_balance;
        result.max_dd_pct = res.max_drawdown_pct;
        result.return_mult = res.final_balance / 10000.0;
        result.trades = res.total_trades;
        result.forced_entries = stats.forced_entries;
        result.stopped_out = (res.final_balance < 1000);
    } catch (...) {
        result.final_balance = 0;
        result.max_dd_pct = 100;
        result.return_mult = 0;
        result.trades = 0;
        result.forced_entries = 0;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << "========================================================================\n";
    std::cout << "FORCE ENTRY MITIGATION ANALYSIS\n";
    std::cout << "Comparing force_min_volume_entry ON vs OFF at different survive%\n";
    std::cout << "========================================================================\n\n";

    std::vector<TestResult> results;

    // Test configurations
    std::vector<double> survives = {12.0, 13.0, 14.0, 15.0, 16.0, 18.0, 20.0};
    std::vector<bool> force_options = {false, true};

    // PART 1: Test Oct 17 start (crash scenario)
    std::cout << "=== PART 1: October 17 Start (Crash Scenario) ===\n\n";

    for (double surv : survives) {
        for (bool force : force_options) {
            std::string name = "s" + std::to_string((int)surv) + "_" + (force ? "FORCE" : "noforce");
            std::cout << "  Testing " << name << "..." << std::flush;
            auto r = RunTest("2025.10.17", "2025.12.29", surv, force, name);
            results.push_back(r);
            std::cout << " " << (r.stopped_out ? "STOP-OUT" : "OK") << "\n";
        }
    }

    // Print crash scenario results
    std::cout << "\n";
    std::cout << std::left << std::setw(20) << "Config"
              << std::right << std::setw(10) << "Final$"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Forced"
              << std::setw(12) << "Status"
              << std::endl;
    std::cout << std::string(82, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(20) << r.config_name
                  << std::right << std::fixed << std::setprecision(0) << std::setw(10) << r.final_balance
                  << std::setprecision(2) << std::setw(9) << r.return_mult << "x"
                  << std::setprecision(1) << std::setw(9) << r.max_dd_pct << "%"
                  << std::setprecision(0) << std::setw(10) << r.trades
                  << std::setw(10) << r.forced_entries
                  << std::setw(12) << (r.stopped_out ? "STOP-OUT" : "SURVIVED")
                  << std::endl;
    }

    // Find minimum survive% that survives with force ON
    double min_survive_force = 100;
    for (const auto& r : results) {
        if (r.force_entry && !r.stopped_out && r.survive_pct < min_survive_force) {
            min_survive_force = r.survive_pct;
        }
    }

    results.clear();

    // PART 2: Full year performance comparison
    std::cout << "\n\n=== PART 2: Full Year 2025 Performance ===\n\n";

    // Test key configurations for full year
    std::vector<std::pair<double, bool>> key_configs = {
        {12.0, false}, {12.0, true},
        {13.0, false}, {13.0, true},
        {14.0, false}, {14.0, true},
        {15.0, false}, {15.0, true},
        {min_survive_force, true}  // Minimum that survives crash with force
    };

    for (const auto& [surv, force] : key_configs) {
        std::string name = "s" + std::to_string((int)surv) + "_" + (force ? "FORCE" : "noforce");
        std::cout << "  Testing " << name << " (full year)..." << std::flush;
        auto r = RunTest("2025.01.01", "2025.12.29", surv, force, name);
        results.push_back(r);
        std::cout << " " << std::fixed << std::setprecision(2) << r.return_mult << "x\n";
    }

    // Print full year results
    std::cout << "\n";
    std::cout << std::left << std::setw(20) << "Config"
              << std::right << std::setw(10) << "Final$"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Forced"
              << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(20) << r.config_name
                  << std::right << std::fixed << std::setprecision(0) << std::setw(10) << r.final_balance
                  << std::setprecision(2) << std::setw(9) << r.return_mult << "x"
                  << std::setprecision(1) << std::setw(9) << r.max_dd_pct << "%"
                  << std::setprecision(0) << std::setw(10) << r.trades
                  << std::setw(10) << r.forced_entries
                  << std::endl;
    }

    // Calculate profit sacrifice
    std::cout << "\n\n=== PROFIT SACRIFICE ANALYSIS ===\n\n";

    double baseline_return = 0;
    for (const auto& r : results) {
        if (r.survive_pct == 12.0 && r.force_entry) {
            baseline_return = r.return_mult;
            break;
        }
    }

    std::cout << "Baseline: s12_FORCE = " << std::fixed << std::setprecision(2) << baseline_return << "x\n\n";

    std::cout << std::left << std::setw(20) << "Config"
              << std::right << std::setw(12) << "Return"
              << std::setw(15) << "vs Baseline"
              << std::setw(18) << "Crash Survival"
              << std::endl;
    std::cout << std::string(65, '-') << std::endl;

    // Need to re-run crash tests for the configs we tested full year
    std::cout << "\n(Re-running crash scenario tests for comparison...)\n\n";

    for (const auto& [surv, force] : key_configs) {
        std::string name = "s" + std::to_string((int)surv) + "_" + (force ? "FORCE" : "noforce");
        auto crash_result = RunTest("2025.10.17", "2025.12.29", surv, force, name + "_crash");

        // Find the full year result
        double full_year_return = 0;
        for (const auto& r : results) {
            if (r.survive_pct == surv && r.force_entry == force) {
                full_year_return = r.return_mult;
                break;
            }
        }

        double sacrifice = (baseline_return > 0) ?
            ((baseline_return - full_year_return) / baseline_return * 100.0) : 0;

        std::cout << std::left << std::setw(20) << name
                  << std::right << std::fixed << std::setprecision(2) << std::setw(11) << full_year_return << "x"
                  << std::setprecision(1) << std::setw(14) << (sacrifice > 0 ? -sacrifice : sacrifice) << "%"
                  << std::setw(18) << (crash_result.stopped_out ? "STOP-OUT" : "SURVIVED")
                  << std::endl;
    }

    std::cout << "\n========================================================================\n";
    std::cout << "RECOMMENDATIONS\n";
    std::cout << "========================================================================\n";
    std::cout << "1. For maximum safety: Use force=OFF (original behavior)\n";
    std::cout << "2. For maximum profit with force=ON: Use survive=" << (int)min_survive_force << "%\n";
    std::cout << "3. Trade-off: Higher survive% = lower profit but survives crashes\n";

    return 0;
}
