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

std::vector<Tick> load_ticks(const std::string& filename, size_t max_ticks = 60000000) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // Skip header

    ticks.reserve(std::min(max_ticks, (size_t)60000000));

    while (std::getline(file, line) && ticks.size() < max_ticks) {
        try {
            std::stringstream ss(line);
            std::string token;
            Tick t;

            std::getline(ss, token, '\t'); // timestamp
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
    return ticks;
}

void print_result(const char* name, const BacktestResult& r, double initial) {
    double ret = r.final_equity / initial;
    printf("%-50s $%10.2f  %5.2fx  %6.1f%%  %5d  %s\n",
           name, r.final_equity, ret, r.max_drawdown_pct,
           r.total_trades,
           r.margin_call_occurred ? "MARGIN CALL" : "OK");
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "BACKTESTER VALIDATION vs MT5 RESULTS\n";
    std::cout << "================================================================\n\n";

    // Load NAS100 data
    std::cout << "Loading NAS100 data...\n";
    auto ticks = load_ticks("NAS100/NAS100_TICKS_2025.csv");
    std::cout << "Loaded " << ticks.size() << " ticks\n";
    if (ticks.empty()) {
        std::cerr << "No data loaded!\n";
        return 1;
    }
    std::cout << "Price range: " << ticks.front().bid << " -> " << ticks.back().bid << "\n\n";

    const double INITIAL = 10000.0;

    printf("%-50s %12s  %6s  %7s  %5s  %s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Status");
    printf("================================================================================\n");

    // Test 1: GridOptimized settings that FAILED in MT5 (margin call at 3%)
    // SurviveDown=10%, Spacing=50, Trailing=true, ATR=2.0
    std::cout << "\n--- TEST 1: GridOptimized (should show margin call like MT5) ---\n";
    {
        BacktestConfig cfg;
        cfg.survive_down_pct = 10.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = true;
        cfg.atr_multiplier = 2.0;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;
        cfg.max_portfolio_dd = 100.0;  // Disabled

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);

        for (const auto& t : ticks) {
            bt.on_tick(t.bid, t.ask);
        }

        auto r = bt.get_result(ticks.back().bid);
        print_result("Survive=10%, Spacing=50, Trailing=ON (MT5: FAIL)", r, INITIAL);
        if (!r.stop_reason.empty()) {
            std::cout << "  Stop reason: " << r.stop_reason << "\n";
        }
        std::cout << "  Spread costs: $" << r.total_spread_cost << "\n";
    }

    // Test 2: GridSimple settings that WORKED in MT5 (+95%)
    // SurviveDown=30%, Spacing=50, Trailing=false
    std::cout << "\n--- TEST 2: GridSimple (should show ~+95% like MT5) ---\n";
    {
        BacktestConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = false;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;
        cfg.max_portfolio_dd = 100.0;  // Disabled

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);

        for (const auto& t : ticks) {
            bt.on_tick(t.bid, t.ask);
        }

        auto r = bt.get_result(ticks.back().bid);
        print_result("Survive=30%, Spacing=50, Trailing=OFF (MT5: +95%)", r, INITIAL);
        if (!r.stop_reason.empty()) {
            std::cout << "  Stop reason: " << r.stop_reason << "\n";
        }
    }

    // Test 3: Various survive_down percentages
    std::cout << "\n--- TEST 3: Survive Down % Sweep (no trailing) ---\n";
    for (double survive : {4.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = survive;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = false;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);

        for (const auto& t : ticks) {
            bt.on_tick(t.bid, t.ask);
        }

        auto r = bt.get_result(ticks.back().bid);

        char name[64];
        snprintf(name, sizeof(name), "Survive=%.0f%%, Spacing=50, No Trail", survive);
        print_result(name, r, INITIAL);
    }

    // Test 4: With trailing at various ATR multipliers
    std::cout << "\n--- TEST 4: Trailing Stop ATR Multiplier Sweep ---\n";
    for (double atr : {1.5, 2.0, 3.0, 5.0, 10.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = 30.0;  // Use safe survive
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = true;
        cfg.atr_multiplier = atr;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);

        for (const auto& t : ticks) {
            bt.on_tick(t.bid, t.ask);
        }

        auto r = bt.get_result(ticks.back().bid);

        char name[64];
        snprintf(name, sizeof(name), "Survive=30%%, ATR=%.1fx Trailing", atr);
        print_result(name, r, INITIAL);
        std::cout << "  Trailing exits: " << r.trailing_exits
                  << ", Spread cost: $" << std::fixed << std::setprecision(0) << r.total_spread_cost << "\n";
    }

    // Test 5: Spacing sweep
    std::cout << "\n--- TEST 5: Spacing Sweep (no trailing) ---\n";
    for (double spacing : {10.0, 25.0, 50.0, 100.0, 200.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = 30.0;
        cfg.min_entry_spacing = spacing;
        cfg.enable_trailing = false;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);

        for (const auto& t : ticks) {
            bt.on_tick(t.bid, t.ask);
        }

        auto r = bt.get_result(ticks.back().bid);

        char name[64];
        snprintf(name, sizeof(name), "Survive=30%%, Spacing=%.0f, No Trail", spacing);
        print_result(name, r, INITIAL);
    }

    std::cout << "\n================================================================\n";
    std::cout << "VALIDATION SUMMARY\n";
    std::cout << "================================================================\n";
    std::cout << "MT5 Results to match:\n";
    std::cout << "  - GridOptimized (survive=10%, trailing=ON): MARGIN CALL at 3%\n";
    std::cout << "  - GridSimple (survive=30%, trailing=OFF):   +95% return\n";
    std::cout << "\nIf the backtester results match these, it's validated!\n";
    std::cout << "================================================================\n";

    return 0;
}
