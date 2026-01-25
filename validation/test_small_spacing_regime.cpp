/**
 * Small Spacing Regime Independence Test
 * Compare small vs wide spacing across 2024 and 2025
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>

using namespace backtest;

struct TestResult {
    double return_mult;
    double max_dd;
    int trades;
    double swap;
};

TestResult run_test(const std::vector<Tick>& ticks,
                    const std::string& start_date, const std::string& end_date,
                    double survive_pct, double base_spacing,
                    double lookback_hours, double typical_vol_pct) {

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
        return {res.final_balance / 10000.0, res.max_drawdown_pct, res.total_trades, res.total_swap_charged};

    } catch (const std::exception& e) {
        return {0, 100, 0, 0};
    }
}

std::vector<Tick> load_ticks(const std::string& path) {
    std::vector<Tick> ticks;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << path << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line);

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
    return ticks;
}

int main() {
    std::cout << "Loading 2025 tick data..." << std::flush;
    auto ticks_2025 = load_ticks("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    std::cout << " " << ticks_2025.size() << " ticks" << std::endl;

    std::cout << "Loading 2024 tick data..." << std::flush;
    auto ticks_2024 = load_ticks("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv");
    std::cout << " " << ticks_2024.size() << " ticks" << std::endl;

    std::cout << "\n=====================================================\n";
    std::cout << "  Small vs Wide Spacing: Regime Independence Test\n";
    std::cout << "  2024: +27% trend | 2025: +60% trend\n";
    std::cout << "=====================================================\n\n";

    // Test configurations: spacing, lookback, typvol
    struct Config {
        double spacing;
        double lookback;
        double typvol;
        std::string label;
    };

    std::vector<Config> configs = {
        // Small spacing configs
        {0.10, 4.0, 0.55, "sp=$0.10 (tiny)"},
        {0.20, 4.0, 0.55, "sp=$0.20 (tiny)"},
        {0.30, 4.0, 0.55, "sp=$0.30 (small)"},
        {0.50, 4.0, 0.55, "sp=$0.50 (small)"},

        // Middle spacing (trough)
        {1.50, 4.0, 0.55, "sp=$1.50 (baseline)"},
        {3.00, 4.0, 0.55, "sp=$3.00 (mid)"},
        {5.00, 4.0, 0.55, "sp=$5.00 (mid)"},

        // Wide spacing configs
        {20.0, 2.0, 0.05, "sp=$20 (wide, best DD)"},
        {50.0, 0.2, 0.05, "sp=$50 (wide, best ret)"},
    };

    std::cout << std::left << std::setw(25) << "Config"
              << std::right
              << std::setw(10) << "2025"
              << std::setw(10) << "2024"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2025 DD"
              << std::setw(12) << "2024 DD"
              << std::setw(12) << "2025 Trd"
              << std::setw(12) << "2024 Trd"
              << std::endl;
    std::cout << std::string(103, '-') << std::endl;

    for (const auto& cfg : configs) {
        auto r2025 = run_test(ticks_2025, "2025.01.01", "2025.12.30", 13.0, cfg.spacing, cfg.lookback, cfg.typvol);
        auto r2024 = run_test(ticks_2024, "2024.01.01", "2024.12.30", 13.0, cfg.spacing, cfg.lookback, cfg.typvol);

        double ratio = (r2024.return_mult > 0) ? r2025.return_mult / r2024.return_mult : 0;

        std::cout << std::left << std::setw(25) << cfg.label
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << r2025.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << r2024.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << ratio << "x"
                  << std::setw(11) << std::setprecision(1) << r2025.max_dd << "%"
                  << std::setw(11) << std::setprecision(1) << r2024.max_dd << "%"
                  << std::setw(12) << r2025.trades
                  << std::setw(12) << r2024.trades
                  << std::endl;
    }

    // H1/H2 consistency for small spacing
    std::cout << "\n=====================================================\n";
    std::cout << "  H1/H2 Consistency Check (2025)\n";
    std::cout << "=====================================================\n\n";

    std::cout << std::left << std::setw(25) << "Config"
              << std::right
              << std::setw(10) << "H1"
              << std::setw(10) << "H2"
              << std::setw(10) << "H1/H2"
              << std::setw(12) << "H1 DD"
              << std::setw(12) << "H2 DD"
              << std::endl;
    std::cout << std::string(79, '-') << std::endl;

    for (const auto& cfg : configs) {
        auto h1 = run_test(ticks_2025, "2025.01.01", "2025.06.30", 13.0, cfg.spacing, cfg.lookback, cfg.typvol);
        auto h2 = run_test(ticks_2025, "2025.07.01", "2025.12.30", 13.0, cfg.spacing, cfg.lookback, cfg.typvol);

        double ratio = (h2.return_mult > 0) ? h1.return_mult / h2.return_mult : 0;

        std::cout << std::left << std::setw(25) << cfg.label
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << h1.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << h2.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << ratio
                  << std::setw(11) << std::setprecision(1) << h1.max_dd << "%"
                  << std::setw(11) << std::setprecision(1) << h2.max_dd << "%"
                  << std::endl;
    }

    std::cout << "\n=====================================================\n";
    std::cout << "  Interpretation\n";
    std::cout << "=====================================================\n";
    std::cout << "2025/2024 Ratio close to 1.0 = regime independent\n";
    std::cout << "2025/2024 Ratio >> 1.0 = trend dependent (benefits from bull)\n";
    std::cout << "H1/H2 Ratio close to 1.0 = consistent within year\n";

    return 0;
}
