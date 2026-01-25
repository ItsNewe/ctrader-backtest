/**
 * Wide Spacing Analysis: Is it trend or oscillation?
 *
 * Tests the top wide-spacing configs on:
 * 1. H1 vs H2 2025 (consistency check)
 * 2. Full 2024 (weaker trend year)
 * 3. Trade-by-trade analysis to see profit source
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>

using namespace backtest;

void run_test(const std::string& label, const std::string& path,
              const std::string& start_date, const std::string& end_date,
              double survive_pct, double base_spacing,
              double lookback_hours, double typical_vol_pct) {

    // Load ticks
    std::vector<Tick> ticks;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "Cannot open: " << path << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;
        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');
        tick.timestamp = datetime_str;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;
        ticks.push_back(tick);
    }

    TickBacktestConfig cfg;
    cfg.symbol = "XAUUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 100.0;
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.01;
    cfg.swap_long = -66.99;
    cfg.swap_short = 41.2;
    cfg.swap_mode = 1;
    cfg.swap_3days = 3;
    cfg.start_date = start_date;
    cfg.end_date = end_date;
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        FillUpOscillation::AdaptiveConfig acfg;
        acfg.typical_vol_pct = typical_vol_pct;
        acfg.min_spacing_mult = 0.5;
        acfg.max_spacing_mult = 3.0;
        acfg.min_spacing_abs = 0.05;
        acfg.max_spacing_abs = 100.0;
        acfg.spacing_change_threshold = 0.1;
        acfg.pct_spacing = false;

        FillUpOscillation strategy(
            survive_pct, base_spacing, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1, 30.0, lookback_hours, acfg
        );

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();

        std::cout << std::left << std::setw(45) << label
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << (res.final_balance / 10000.0) << "x"
                  << std::setw(8) << std::setprecision(1) << res.max_drawdown_pct << "%"
                  << std::setw(8) << res.total_trades
                  << std::setw(12) << std::setprecision(0) << res.total_swap_charged
                  << std::endl;

    } catch (const std::exception& e) {
        std::cout << std::left << std::setw(45) << label << " ERROR: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "  Wide Spacing Analysis: Trend vs Oscillation" << std::endl;
    std::cout << "=====================================================" << std::endl << std::endl;

    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";

    std::cout << "Config                                        Return  MaxDD%  Trades        Swap" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    // Test 1: Wide spacing ($50) configs on different periods
    std::cout << "\n=== sp=$50, lb=0.2h, tv=0.05% (top performer) ===" << std::endl;
    run_test("2025 Full Year", path_2025, "2025.01.01", "2025.12.30", 12.0, 50.0, 0.2, 0.05);
    run_test("2025 H1 (Jan-Jun)", path_2025, "2025.01.01", "2025.06.30", 12.0, 50.0, 0.2, 0.05);
    run_test("2025 H2 (Jul-Dec)", path_2025, "2025.07.01", "2025.12.30", 12.0, 50.0, 0.2, 0.05);
    run_test("2024 Full Year", path_2024, "2024.01.01", "2024.12.30", 12.0, 50.0, 0.2, 0.05);

    std::cout << "\n=== sp=$20, lb=2h, tv=0.05% (lowest DD) ===" << std::endl;
    run_test("2025 Full Year", path_2025, "2025.01.01", "2025.12.30", 12.0, 20.0, 2.0, 0.05);
    run_test("2025 H1 (Jan-Jun)", path_2025, "2025.01.01", "2025.06.30", 12.0, 20.0, 2.0, 0.05);
    run_test("2025 H2 (Jul-Dec)", path_2025, "2025.07.01", "2025.12.30", 12.0, 20.0, 2.0, 0.05);
    run_test("2024 Full Year", path_2024, "2024.01.01", "2024.12.30", 12.0, 20.0, 2.0, 0.05);

    std::cout << "\n=== sp=$1.50, lb=4h, tv=0.55% (standard baseline) ===" << std::endl;
    run_test("2025 Full Year", path_2025, "2025.01.01", "2025.12.30", 13.0, 1.5, 4.0, 0.55);
    run_test("2025 H1 (Jan-Jun)", path_2025, "2025.01.01", "2025.06.30", 13.0, 1.5, 4.0, 0.55);
    run_test("2025 H2 (Jul-Dec)", path_2025, "2025.07.01", "2025.12.30", 13.0, 1.5, 4.0, 0.55);
    run_test("2024 Full Year", path_2024, "2024.01.01", "2024.12.30", 13.0, 1.5, 4.0, 0.55);

    // Test 2: What if we compare with survive=13% (safer)?
    std::cout << "\n=== sp=$50 with survive=13% (safer) ===" << std::endl;
    run_test("2025 Full Year (s13)", path_2025, "2025.01.01", "2025.12.30", 13.0, 50.0, 0.2, 0.05);
    run_test("2024 Full Year (s13)", path_2024, "2024.01.01", "2024.12.30", 13.0, 50.0, 0.2, 0.05);

    std::cout << "\n=== sp=$20 with survive=13% (safer) ===" << std::endl;
    run_test("2025 Full Year (s13)", path_2025, "2025.01.01", "2025.12.30", 13.0, 20.0, 2.0, 0.05);
    run_test("2024 Full Year (s13)", path_2024, "2024.01.01", "2024.12.30", 13.0, 20.0, 2.0, 0.05);

    // Test 3: Different spacing values on 2024 to understand trend dependency
    std::cout << "\n=== 2024 Spacing Comparison (survive=12, lb=4h, tv=0.55%) ===" << std::endl;
    for (double sp : {1.5, 3.0, 5.0, 8.0, 12.0, 20.0, 50.0}) {
        std::string label = "2024 sp=$" + std::to_string((int)sp);
        if (sp < 10) label = "2024 sp=$" + std::to_string(sp).substr(0,4);
        run_test(label, path_2024, "2024.01.01", "2024.12.30", 12.0, sp, 4.0, 0.55);
    }

    std::cout << "\n=== Price Context ===" << std::endl;
    std::cout << "2024: Gold ~$2,063 -> ~$2,624 (+27% trend)" << std::endl;
    std::cout << "2025: Gold ~$2,624 -> ~$4,200 (+60% trend)" << std::endl;
    std::cout << "\nWith $50 spacing:" << std::endl;
    std::cout << "  - Grid only fills on $50+ moves" << std::endl;
    std::cout << "  - At $2,700: survive 12% = $324 drop = 6.5 grid levels" << std::endl;
    std::cout << "  - At $4,000: survive 12% = $480 drop = 9.6 grid levels" << std::endl;
    std::cout << "  - 283 trades/year = ~1 trade per trading day" << std::endl;
    std::cout << "\nThis is essentially trend-following with occasional dip-buying," << std::endl;
    std::cout << "NOT oscillation capture. It works in bull markets, fails in bear." << std::endl;

    return 0;
}
