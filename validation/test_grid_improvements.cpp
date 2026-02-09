#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../include/grid_improved.h"

struct TestResult {
    std::string name;
    double final_equity;
    double max_dd;
    int trades;
    int profit_takes;
    int crash_exits;
    int regime_blocks;
    bool margin_call;
};

TestResult run_test(const char* filename, const ImprovedConfig& cfg, const std::string& name) {
    std::ifstream file(filename);
    TestResult tr = {};
    tr.name = name;

    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return tr;
    }

    std::string line;
    std::getline(file, line);

    ImprovedGrid bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    double last_bid = 0;
    while (std::getline(file, line)) {
        try {
            std::stringstream ss(line);
            std::string token;

            std::getline(ss, token, '\t');
            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            double bid = std::stod(token);

            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            double ask = std::stod(token);

            if (bid > 0 && ask > 0) {
                last_bid = bid;
                bt.on_tick(bid, ask);
            }
        } catch (...) {
            continue;
        }
    }

    auto r = bt.get_result(last_bid);
    tr.final_equity = r.final_equity;
    tr.max_dd = r.max_drawdown_pct;
    tr.trades = r.total_trades;
    tr.profit_takes = r.profit_takes;
    tr.crash_exits = r.crash_exits;
    tr.regime_blocks = r.regime_blocks;
    tr.margin_call = r.margin_call_occurred;
    return tr;
}

void print_result(const TestResult& r) {
    std::cout << std::left << std::setw(35) << r.name
              << std::right << "$" << std::setw(10) << std::fixed << std::setprecision(2) << r.final_equity
              << std::setw(8) << r.max_dd << "%"
              << std::setw(7) << r.trades;

    if (r.profit_takes > 0) std::cout << " PT:" << r.profit_takes;
    if (r.crash_exits > 0) std::cout << " CE:" << r.crash_exits;
    if (r.regime_blocks > 0) std::cout << " RB:" << r.regime_blocks;
    if (r.margin_call) std::cout << " MARGIN_CALL";

    std::cout << "\n";
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "GRID STRATEGY IMPROVEMENTS TEST\n";
    std::cout << "================================================================\n\n";

    const char* nas_file = "NAS100/NAS100_TICKS_2025.csv";

    std::cout << "Testing on NAS100 (full year 2025)...\n\n";
    std::cout << std::left << std::setw(35) << "Strategy"
              << std::right << std::setw(12) << "Final$"
              << std::setw(9) << "MaxDD"
              << std::setw(7) << "Trades"
              << " Notes\n";
    std::cout << std::string(80, '-') << "\n";

    // Baseline: Original Grid (survive=30%)
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        auto r = run_test(nas_file, cfg, "Baseline (survive=30%, no improvements)");
        print_result(r);
    }

    std::cout << "\n--- IMPROVEMENT 1: REGIME FILTER ---\n";

    // Regime filter with SMA 50
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_regime_filter = true;
        cfg.sma_period = 50;
        cfg.sma_buffer_pct = 1.0;
        auto r = run_test(nas_file, cfg, "Regime SMA(50), 1% buffer");
        print_result(r);
    }

    // Regime filter with SMA 200
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_regime_filter = true;
        cfg.sma_period = 200;
        cfg.sma_buffer_pct = 0.5;
        auto r = run_test(nas_file, cfg, "Regime SMA(200), 0.5% buffer");
        print_result(r);
    }

    std::cout << "\n--- IMPROVEMENT 2: PROFIT TAKING ---\n";

    // Profit taking at 20%
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_profit_taking = true;
        cfg.profit_take_pct = 20.0;
        cfg.profit_take_amount = 0.5;
        auto r = run_test(nas_file, cfg, "Take 50% profit at +20%");
        print_result(r);
    }

    // Profit taking at 50%
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_profit_taking = true;
        cfg.profit_take_pct = 50.0;
        cfg.profit_take_amount = 0.5;
        auto r = run_test(nas_file, cfg, "Take 50% profit at +50%");
        print_result(r);
    }

    // Profit taking at 10%, take all
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_profit_taking = true;
        cfg.profit_take_pct = 10.0;
        cfg.profit_take_amount = 1.0;
        auto r = run_test(nas_file, cfg, "Take 100% profit at +10%");
        print_result(r);
    }

    std::cout << "\n--- IMPROVEMENT 3: CRASH DETECTION ---\n";

    // Crash detection -1% per 1000 ticks
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_crash_detection = true;
        cfg.crash_velocity_threshold = -1.0;
        cfg.crash_lookback = 1000;
        cfg.crash_exit_pct = 0.5;
        auto r = run_test(nas_file, cfg, "Exit 50% on -1% velocity");
        print_result(r);
    }

    // Crash detection -2% per 1000 ticks
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_crash_detection = true;
        cfg.crash_velocity_threshold = -2.0;
        cfg.crash_lookback = 1000;
        cfg.crash_exit_pct = 1.0;
        auto r = run_test(nas_file, cfg, "Exit 100% on -2% velocity");
        print_result(r);
    }

    std::cout << "\n--- IMPROVEMENT 4: VOLATILITY SIZING ---\n";

    // Volatility adjustment x1
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_volatility_sizing = true;
        cfg.volatility_period = 100;
        cfg.volatility_multiplier = 1.0;
        auto r = run_test(nas_file, cfg, "Volatility sizing (mult=1.0)");
        print_result(r);
    }

    // Volatility adjustment x2
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_volatility_sizing = true;
        cfg.volatility_period = 100;
        cfg.volatility_multiplier = 2.0;
        auto r = run_test(nas_file, cfg, "Volatility sizing (mult=2.0)");
        print_result(r);
    }

    std::cout << "\n--- IMPROVEMENT 5: EQUITY CURVE FILTER ---\n";

    // Equity curve filter
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_equity_filter = true;
        cfg.equity_sma_period = 50;
        auto r = run_test(nas_file, cfg, "Equity curve filter (SMA 50)");
        print_result(r);
    }

    std::cout << "\n--- COMBINATIONS ---\n";

    // Best combination 1: Regime + Profit Taking
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_regime_filter = true;
        cfg.sma_period = 200;
        cfg.sma_buffer_pct = 0.5;
        cfg.enable_profit_taking = true;
        cfg.profit_take_pct = 30.0;
        cfg.profit_take_amount = 0.5;
        auto r = run_test(nas_file, cfg, "Regime + Profit Take @30%");
        print_result(r);
    }

    // Best combination 2: Crash + Volatility
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_crash_detection = true;
        cfg.crash_velocity_threshold = -1.5;
        cfg.crash_lookback = 1000;
        cfg.crash_exit_pct = 0.5;
        cfg.enable_volatility_sizing = true;
        cfg.volatility_multiplier = 1.5;
        auto r = run_test(nas_file, cfg, "Crash detect + Vol sizing");
        print_result(r);
    }

    // All improvements combined
    {
        ImprovedConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_regime_filter = true;
        cfg.sma_period = 200;
        cfg.enable_profit_taking = true;
        cfg.profit_take_pct = 30.0;
        cfg.profit_take_amount = 0.3;
        cfg.enable_crash_detection = true;
        cfg.crash_velocity_threshold = -1.5;
        cfg.crash_exit_pct = 0.5;
        cfg.enable_volatility_sizing = true;
        cfg.volatility_multiplier = 1.0;
        auto r = run_test(nas_file, cfg, "ALL IMPROVEMENTS COMBINED");
        print_result(r);
    }

    std::cout << "\n================================================================\n";
    std::cout << "Legend: PT=Profit Takes, CE=Crash Exits, RB=Regime Blocks\n";
    std::cout << "================================================================\n";

    return 0;
}
