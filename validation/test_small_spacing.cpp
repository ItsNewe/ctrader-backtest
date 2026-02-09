/**
 * Small Spacing Analysis: Complete the U-curve picture
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>

using namespace backtest;

void run_test(const std::string& label, const std::vector<Tick>& ticks,
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

        std::cout << std::left << std::setw(30) << label
                  << std::right << std::fixed
                  << std::setw(10) << std::setprecision(2) << (res.final_balance / 10000.0) << "x"
                  << std::setw(10) << std::setprecision(1) << res.max_drawdown_pct << "%"
                  << std::setw(10) << res.total_trades
                  << std::setw(12) << std::setprecision(0) << res.total_swap_charged
                  << std::endl;

    } catch (const std::exception& e) {
        std::cout << std::left << std::setw(30) << label << " ERROR: " << e.what() << std::endl;
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
    return ticks;
}

int main() {
    std::cout << "Loading 2025 tick data..." << std::flush;
    auto ticks_2025 = load_ticks("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    std::cout << " " << ticks_2025.size() << " ticks loaded." << std::endl;

    std::cout << "\n=== 2025 Full Year: Spacing U-Curve (survive=12%, lb=4h, tv=0.55%) ===" << std::endl;
    std::cout << std::left << std::setw(30) << "Config"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Swap"
              << std::endl;
    std::cout << std::string(72, '-') << std::endl;

    std::vector<double> spacings = {0.10, 0.20, 0.30, 0.50, 0.75, 1.00, 1.50, 2.00, 3.00, 5.00, 8.00, 12.00, 20.00, 50.00};
    
    for (double sp : spacings) {
        std::ostringstream label;
        label << "sp=$" << std::fixed << std::setprecision(2) << sp;
        run_test(label.str(), ticks_2025, "2025.01.01", "2025.12.30", 12.0, sp, 4.0, 0.55);
    }

    std::cout << "\n=== Best from deep sweep: sp=$50, lb=0.2h, tv=0.05% ===" << std::endl;
    run_test("sp=$50, lb=0.2, tv=0.05", ticks_2025, "2025.01.01", "2025.12.30", 12.0, 50.0, 0.2, 0.05);
    
    std::cout << "\n=== Tiny spacing: sp=$0.10-0.30, lb=0.2h, tv=0.05% ===" << std::endl;
    for (double sp : {0.10, 0.20, 0.30}) {
        std::ostringstream label;
        label << "sp=$" << std::fixed << std::setprecision(2) << sp << ", lb=0.2, tv=0.05";
        run_test(label.str(), ticks_2025, "2025.01.01", "2025.12.30", 12.0, sp, 0.2, 0.05);
    }

    return 0;
}
