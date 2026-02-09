#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include "../include/backtester_accurate.h"

struct Tick {
    double bid;
    double ask;
};

std::vector<Tick> load_ticks(const std::string& filename, size_t max_ticks) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line);

    while (std::getline(file, line) && ticks.size() < max_ticks) {
        try {
            std::stringstream ss(line);
            std::string token;
            Tick t;

            std::getline(ss, token, '\t');
            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            t.bid = std::stod(token);

            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            t.ask = std::stod(token);

            if (t.bid > 0 && t.ask > 0) {
                ticks.push_back(t);
            }
        } catch (...) {
            continue;
        }
    }
    std::cout << "Loaded " << ticks.size() << " ticks from " << filename << "\n";
    return ticks;
}

void run_test(const std::vector<Tick>& ticks, const BacktestConfig& cfg, const char* name) {
    AccurateBacktester bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    for (const auto& t : ticks) {
        bt.on_tick(t.bid, t.ask);
    }

    auto r = bt.get_result(ticks.back().bid);
    printf("%-50s $%10.2f %5.2fx %5.1f%% %5d %s\n",
           name, r.final_equity, r.final_equity/10000.0, r.max_drawdown_pct,
           r.total_trades, r.margin_call_occurred ? "MARGIN CALL" : "OK");
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "COMPREHENSIVE STRATEGY TEST (LIMITED DATA FOR TESTING)\n";
    std::cout << "================================================================\n\n";

    // Load limited data first
    auto nas_ticks = load_ticks("NAS100/NAS100_TICKS_2025.csv", 5000000);
    if (nas_ticks.empty()) {
        std::cerr << "No NAS100 data!\n";
        return 1;
    }

    std::cout << "\nPrice range: " << nas_ticks.front().bid << " -> " << nas_ticks.back().bid << "\n\n";
    printf("%-50s %12s %6s %6s %5s %s\n", "Config", "Final", "Ret", "MaxDD", "Trades", "Status");
    printf("===================================================================================\n");

    // Test 1: Various survive percentages (no trailing)
    std::cout << "\n=== SURVIVE DOWN % SWEEP (No Trailing) ===\n";
    for (double survive : {4.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = survive;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = false;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        char name[64];
        snprintf(name, sizeof(name), "Survive=%.0f%%, Spacing=50, No Trail", survive);
        run_test(nas_ticks, cfg, name);
    }

    // Test 2: Spacing sweep
    std::cout << "\n=== SPACING SWEEP (Survive=30%, No Trailing) ===\n";
    for (double spacing : {10.0, 25.0, 50.0, 100.0, 200.0, 500.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = spacing;
        cfg.enable_trailing = false;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        char name[64];
        snprintf(name, sizeof(name), "Survive=30%%, Spacing=%.0f, No Trail", spacing);
        run_test(nas_ticks, cfg, name);
    }

    // Test 3: Trailing stop tests
    std::cout << "\n=== TRAILING STOP TESTS (Survive=30%) ===\n";
    for (double atr : {1.5, 2.0, 3.0, 5.0, 10.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = true;
        cfg.atr_multiplier = atr;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        char name[64];
        snprintf(name, sizeof(name), "Survive=30%%, ATR=%.1fx Trailing", atr);
        run_test(nas_ticks, cfg, name);
    }

    // Test 4: Low survive with trailing (expected to fail)
    std::cout << "\n=== LOW SURVIVE + TRAILING (Expected to fail) ===\n";
    for (double survive : {4.0, 10.0, 15.0, 20.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = survive;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = true;
        cfg.atr_multiplier = 2.0;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        char name[64];
        snprintf(name, sizeof(name), "Survive=%.0f%%, ATR=2.0x Trailing", survive);
        run_test(nas_ticks, cfg, name);
    }

    std::cout << "\n================================================================\n";
    std::cout << "Tests complete\n";
    return 0;
}
