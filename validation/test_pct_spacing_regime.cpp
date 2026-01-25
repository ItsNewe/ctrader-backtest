/**
 * Percentage-Based Spacing Regime Independence Test
 * Does pct_spacing mode produce more consistent 2024/2025 ratios?
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
                    double survive_pct, double base_spacing_pct,
                    double lookback_hours, double typical_vol_pct,
                    bool use_pct_spacing) {

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
        acfg.pct_spacing = use_pct_spacing;

        if (use_pct_spacing) {
            // In pct mode, these are percentages
            acfg.min_spacing_abs = 0.01;   // 0.01% min
            acfg.max_spacing_abs = 5.0;    // 5% max
            acfg.spacing_change_threshold = 0.05;  // 0.05% threshold
        } else {
            acfg.min_spacing_abs = 0.05;
            acfg.max_spacing_abs = 100.0;
            acfg.spacing_change_threshold = 0.1;
        }

        FillUpOscillation strategy(
            survive_pct, base_spacing_pct, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1, 30.0, lookback_hours, acfg
        );

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        return {res.final_balance / 10000.0, res.max_drawdown_pct, (int)res.total_trades, res.total_swap_charged};

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
    std::cout << "  Percentage vs Absolute Spacing: Regime Independence\n";
    std::cout << "  2024 avg: ~$2,300 | 2025 avg: ~$3,500 (1.52x)\n";
    std::cout << "=====================================================\n";

    // First: Show equivalent spacings
    std::cout << "\nEquivalent spacing at different price levels:\n";
    std::cout << "-----------------------------------------------\n";
    std::cout << "Pct     @ $2,300    @ $3,500    @ $5,000\n";
    for (double pct : {0.05, 0.10, 0.20, 0.50, 1.00, 2.00}) {
        std::cout << std::fixed << std::setprecision(2)
                  << pct << "%   $" << std::setw(6) << std::setprecision(2) << (2300 * pct / 100)
                  << "     $" << std::setw(6) << (3500 * pct / 100)
                  << "     $" << std::setw(6) << (5000 * pct / 100) << std::endl;
    }

    std::cout << "\n=====================================================\n";
    std::cout << "  ABSOLUTE $ Spacing (current approach)\n";
    std::cout << "=====================================================\n\n";

    std::cout << std::left << std::setw(20) << "Config"
              << std::right
              << std::setw(10) << "2025"
              << std::setw(10) << "2024"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2025 Trd"
              << std::setw(12) << "2024 Trd"
              << std::setw(10) << "Trd Rat"
              << std::endl;
    std::cout << std::string(84, '-') << std::endl;

    // Absolute spacing tests
    std::vector<double> abs_spacings = {0.50, 1.00, 1.50, 3.00, 5.00, 10.00, 20.00};
    for (double sp : abs_spacings) {
        auto r2025 = run_test(ticks_2025, "2025.01.01", "2025.12.30", 13.0, sp, 4.0, 0.55, false);
        auto r2024 = run_test(ticks_2024, "2024.01.01", "2024.12.30", 13.0, sp, 4.0, 0.55, false);
        double ratio = (r2024.return_mult > 0) ? r2025.return_mult / r2024.return_mult : 0;
        double trd_ratio = (r2024.trades > 0) ? (double)r2025.trades / r2024.trades : 0;

        std::cout << std::left << std::setw(20) << ("$" + std::to_string((int)sp))
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << r2025.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << r2024.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << ratio << "x"
                  << std::setw(12) << r2025.trades
                  << std::setw(12) << r2024.trades
                  << std::setw(9) << std::setprecision(2) << trd_ratio << "x"
                  << std::endl;
    }

    std::cout << "\n=====================================================\n";
    std::cout << "  PERCENTAGE Spacing (pct_spacing=true)\n";
    std::cout << "=====================================================\n\n";

    std::cout << std::left << std::setw(20) << "Config"
              << std::right
              << std::setw(10) << "2025"
              << std::setw(10) << "2024"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2025 Trd"
              << std::setw(12) << "2024 Trd"
              << std::setw(10) << "Trd Rat"
              << std::endl;
    std::cout << std::string(84, '-') << std::endl;

    // Percentage spacing tests - equivalent to absolute at ~$3000
    std::vector<double> pct_spacings = {0.02, 0.04, 0.06, 0.10, 0.15, 0.30, 0.50, 1.00};
    for (double pct : pct_spacings) {
        auto r2025 = run_test(ticks_2025, "2025.01.01", "2025.12.30", 13.0, pct, 4.0, 0.55, true);
        auto r2024 = run_test(ticks_2024, "2024.01.01", "2024.12.30", 13.0, pct, 4.0, 0.55, true);
        double ratio = (r2024.return_mult > 0) ? r2025.return_mult / r2024.return_mult : 0;
        double trd_ratio = (r2024.trades > 0) ? (double)r2025.trades / r2024.trades : 0;

        std::ostringstream label;
        label << std::fixed << std::setprecision(2) << pct << "%";

        std::cout << std::left << std::setw(20) << label.str()
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << r2025.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << r2024.return_mult << "x"
                  << std::setw(9) << std::setprecision(2) << ratio << "x"
                  << std::setw(12) << r2025.trades
                  << std::setw(12) << r2024.trades
                  << std::setw(9) << std::setprecision(2) << trd_ratio << "x"
                  << std::endl;
    }

    std::cout << "\n=====================================================\n";
    std::cout << "  Interpretation\n";
    std::cout << "=====================================================\n";
    std::cout << "If pct_spacing reduces the 2025/2024 ratio, it means\n";
    std::cout << "part of the 'regime dependence' was actually just the\n";
    std::cout << "price level difference making $ spacing tighter in 2025.\n";
    std::cout << "\nRemaining ratio difference = true regime dependence\n";
    std::cout << "(oscillation frequency, volatility environment, etc.)\n";

    return 0;
}
