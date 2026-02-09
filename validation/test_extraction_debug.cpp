/**
 * Debug test - verify strategy is trading with proper tick loading
 */
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>

using namespace backtest;

std::vector<Tick> g_ticks;

void LoadTickData(const std::string& path) {
    std::cout << "Loading: " << path << "... " << std::flush;

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string token;

        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, token, '\t'); tick.bid = std::stod(token);
        std::getline(ss, token, '\t'); tick.ask = std::stod(token);

        g_ticks.push_back(tick);

        // Only load 2M ticks for quick test
        if (g_ticks.size() >= 2000000) break;
    }

    std::cout << g_ticks.size() << " ticks loaded" << std::endl;
}

int main() {
    std::cout << "Debug: Testing with RunWithTicks\n";

    // Load ticks
    LoadTickData("C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv");

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

    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.pct_spacing = false;  // Absolute spacing
    adaptive_cfg.typical_vol_pct = 0.55;
    adaptive_cfg.min_spacing_mult = 0.5;
    adaptive_cfg.max_spacing_mult = 3.0;

    TickBasedEngine engine(config);
    // Test with survive=12%, spacing=$1.50 (standard params)
    FillUpOscillation strategy(12.0, 1.5, 0.01, 10.0, 100.0, 500.0,
        FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

    std::cout << "Running with RunWithTicks..." << std::endl;

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto res = engine.GetResults();
    std::cout << "\n=== FINAL ===\n";
    std::cout << "Final Balance: $" << std::fixed << std::setprecision(2) << res.final_balance << std::endl;
    std::cout << "Total Trades: " << res.total_trades << std::endl;
    std::cout << "Max DD: " << res.max_drawdown_pct << "%" << std::endl;
    std::cout << "Stop-out: " << (res.stop_out_occurred ? "YES" : "NO") << std::endl;

    return 0;
}
