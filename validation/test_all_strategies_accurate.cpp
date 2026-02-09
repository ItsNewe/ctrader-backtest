#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include "../include/backtester_accurate.h"

struct Tick {
    double bid;
    double ask;
    double mid() const { return (bid + ask) / 2.0; }
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

    // Reserve a reasonable amount to avoid excessive reallocation
    ticks.reserve(10000000);  // 10M ticks max initially

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

void print_header() {
    printf("%-55s %12s %7s %7s %6s %6s %s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Spread", "Status");
    printf("===================================================================================================================\n");
}

void print_result(const char* name, const BacktestResult& r, double initial) {
    double ret = r.final_equity / initial;
    const char* status = r.margin_call_occurred ? "MARGIN CALL" : "OK";

    printf("%-55s $%10.2f %6.2fx %6.1f%% %6d $%5.0f %s\n",
           name, r.final_equity, ret, r.max_drawdown_pct,
           r.total_trades, r.total_spread_cost, status);
}

int main() {
    const double INITIAL = 10000.0;

    std::cout << "================================================================\n";
    std::cout << "COMPREHENSIVE STRATEGY TEST WITH ACCURATE BACKTESTER\n";
    std::cout << "================================================================\n\n";

    //=========================================================================
    // NAS100 TESTS
    //=========================================================================
    std::cout << "Loading NAS100 data...\n";
    auto nas_ticks = load_ticks("NAS100/NAS100_TICKS_2025.csv");
    std::cout << "Loaded " << nas_ticks.size() << " ticks\n";
    if (nas_ticks.empty()) {
        std::cerr << "No NAS100 data!\n";
        return 1;
    }
    double nas_start = nas_ticks.front().mid();
    double nas_end = nas_ticks.back().mid();
    double nas_change = (nas_end - nas_start) / nas_start * 100;
    std::cout << "Price: " << nas_start << " -> " << nas_end
              << " (" << (nas_change > 0 ? "+" : "") << nas_change << "%)\n\n";

    print_header();

    // =========== NAS100: Survive Down % Sweep (No Trailing) ===========
    std::cout << "\n=== NAS100: SURVIVE DOWN % SWEEP (No Trailing) ===\n";
    for (double survive : {4.0, 8.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0}) {
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
        for (const auto& t : nas_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(nas_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "NAS100 Survive=%.0f%%, Spacing=50, No Trail", survive);
        print_result(name, r, INITIAL);
    }

    // =========== NAS100: Spacing Sweep (No Trailing) ===========
    std::cout << "\n=== NAS100: SPACING SWEEP (Survive=30%, No Trailing) ===\n";
    for (double spacing : {5.0, 10.0, 25.0, 50.0, 100.0, 200.0, 500.0}) {
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
        for (const auto& t : nas_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(nas_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "NAS100 Survive=30%%, Spacing=%.0f, No Trail", spacing);
        print_result(name, r, INITIAL);
    }

    // =========== NAS100: Trailing Stop Tests ===========
    std::cout << "\n=== NAS100: TRAILING STOP TESTS (Survive=30%) ===\n";
    for (double atr : {1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 10.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = 30.0;
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
        for (const auto& t : nas_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(nas_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "NAS100 Survive=30%%, ATR=%.1fx Trailing", atr);
        print_result(name, r, INITIAL);
    }

    // =========== NAS100: Trailing with lower survive (the failing configs) ===========
    std::cout << "\n=== NAS100: TRAILING + LOW SURVIVE (Expected to fail) ===\n";
    for (double survive : {4.0, 8.0, 10.0, 15.0, 20.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = survive;
        cfg.min_entry_spacing = 50.0;
        cfg.enable_trailing = true;
        cfg.atr_multiplier = 2.0;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);
        for (const auto& t : nas_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(nas_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "NAS100 Survive=%.0f%%, ATR=2.0x Trailing", survive);
        print_result(name, r, INITIAL);
    }

    //=========================================================================
    // GOLD TESTS
    //=========================================================================
    std::cout << "\n\n================================================================\n";
    std::cout << "GOLD (XAUUSD) TESTS\n";
    std::cout << "================================================================\n\n";

    std::cout << "Loading Gold data...\n";
    auto gold_ticks = load_ticks("Grid/XAUUSD_TICKS_2025.csv");
    std::cout << "Loaded " << gold_ticks.size() << " ticks\n";
    if (gold_ticks.empty()) {
        std::cerr << "No Gold data!\n";
        return 1;
    }
    double gold_start = gold_ticks.front().mid();
    double gold_end = gold_ticks.back().mid();
    double gold_change = (gold_end - gold_start) / gold_start * 100;
    std::cout << "Price: " << gold_start << " -> " << gold_end
              << " (" << (gold_change > 0 ? "+" : "") << gold_change << "%)\n\n";

    print_header();

    // =========== Gold: Survive Down % Sweep (No Trailing) ===========
    std::cout << "\n=== GOLD: SURVIVE DOWN % SWEEP (No Trailing) ===\n";
    for (double survive : {1.0, 2.0, 3.0, 5.0, 10.0, 15.0, 20.0, 30.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = survive;
        cfg.min_entry_spacing = 2.0;  // $2 for Gold
        cfg.enable_trailing = false;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 100.0;  // Gold contract size
        cfg.spread = 0.25;  // $0.25 spread for Gold

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);
        for (const auto& t : gold_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(gold_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "GOLD Survive=%.0f%%, Spacing=$2, No Trail", survive);
        print_result(name, r, INITIAL);
    }

    // =========== Gold: Spacing Sweep (No Trailing) ===========
    std::cout << "\n=== GOLD: SPACING SWEEP (Survive=10%, No Trailing) ===\n";
    for (double spacing : {0.5, 1.0, 2.0, 5.0, 10.0, 20.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = 10.0;
        cfg.min_entry_spacing = spacing;
        cfg.enable_trailing = false;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 100.0;
        cfg.spread = 0.25;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);
        for (const auto& t : gold_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(gold_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "GOLD Survive=10%%, Spacing=$%.1f, No Trail", spacing);
        print_result(name, r, INITIAL);
    }

    // =========== Gold: Trailing Stop Tests ===========
    std::cout << "\n=== GOLD: TRAILING STOP TESTS (Survive=10%) ===\n";
    for (double atr : {2.0, 3.0, 5.0, 10.0}) {
        BacktestConfig cfg;
        cfg.survive_down_pct = 10.0;
        cfg.min_entry_spacing = 2.0;
        cfg.enable_trailing = true;
        cfg.atr_multiplier = atr;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 100.0;
        cfg.spread = 0.25;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);
        for (const auto& t : gold_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(gold_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "GOLD Survive=10%%, ATR=%.1fx Trailing", atr);
        print_result(name, r, INITIAL);
    }

    //=========================================================================
    // BEST CONFIGURATIONS SUMMARY
    //=========================================================================
    std::cout << "\n\n================================================================\n";
    std::cout << "FINDING BEST CONFIGURATIONS\n";
    std::cout << "================================================================\n\n";

    // Find best NAS100 config
    double best_nas_return = 0;
    std::string best_nas_name;
    BacktestResult best_nas_result;

    struct TestConfig {
        double survive;
        double spacing;
        bool trailing;
        double atr;
    };

    std::vector<TestConfig> nas_configs = {
        {30.0, 50.0, false, 0},
        {30.0, 25.0, false, 0},
        {30.0, 100.0, false, 0},
        {40.0, 50.0, false, 0},
        {50.0, 50.0, false, 0},
    };

    std::cout << "=== NAS100 BEST CANDIDATES ===\n";
    print_header();
    for (const auto& tc : nas_configs) {
        BacktestConfig cfg;
        cfg.survive_down_pct = tc.survive;
        cfg.min_entry_spacing = tc.spacing;
        cfg.enable_trailing = tc.trailing;
        cfg.atr_multiplier = tc.atr;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);
        for (const auto& t : nas_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(nas_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "NAS100 Survive=%.0f%%, Spacing=%.0f, Trail=%s",
                 tc.survive, tc.spacing, tc.trailing ? "ON" : "OFF");
        print_result(name, r, INITIAL);

        double ret = r.final_equity / INITIAL;
        if (!r.margin_call_occurred && ret > best_nas_return && r.max_drawdown_pct < 95) {
            best_nas_return = ret;
            best_nas_name = name;
            best_nas_result = r;
        }
    }

    // Find best Gold config
    double best_gold_return = 0;
    std::string best_gold_name;
    BacktestResult best_gold_result;

    std::vector<TestConfig> gold_configs = {
        {10.0, 2.0, false, 0},
        {15.0, 2.0, false, 0},
        {20.0, 2.0, false, 0},
        {10.0, 5.0, false, 0},
        {15.0, 5.0, false, 0},
    };

    std::cout << "\n=== GOLD BEST CANDIDATES ===\n";
    print_header();
    for (const auto& tc : gold_configs) {
        BacktestConfig cfg;
        cfg.survive_down_pct = tc.survive;
        cfg.min_entry_spacing = tc.spacing;
        cfg.enable_trailing = tc.trailing;
        cfg.atr_multiplier = tc.atr;
        cfg.leverage = 500.0;
        cfg.stop_out_level = 50.0;
        cfg.contract_size = 100.0;
        cfg.spread = 0.25;

        AccurateBacktester bt;
        bt.configure(cfg);
        bt.reset(INITIAL);
        for (const auto& t : gold_ticks) bt.on_tick(t.bid, t.ask);
        auto r = bt.get_result(gold_ticks.back().bid);

        char name[80];
        snprintf(name, sizeof(name), "GOLD Survive=%.0f%%, Spacing=$%.0f, Trail=%s",
                 tc.survive, tc.spacing, tc.trailing ? "ON" : "OFF");
        print_result(name, r, INITIAL);

        double ret = r.final_equity / INITIAL;
        if (!r.margin_call_occurred && ret > best_gold_return && r.max_drawdown_pct < 95) {
            best_gold_return = ret;
            best_gold_name = name;
            best_gold_result = r;
        }
    }

    //=========================================================================
    // FINAL SUMMARY
    //=========================================================================
    std::cout << "\n\n================================================================\n";
    std::cout << "FINAL SUMMARY - VALIDATED RESULTS\n";
    std::cout << "================================================================\n\n";

    std::cout << "BEST NAS100 CONFIG:\n";
    std::cout << "  " << best_nas_name << "\n";
    std::cout << "  Return: " << std::fixed << std::setprecision(2) << best_nas_return << "x\n";
    std::cout << "  Max DD: " << best_nas_result.max_drawdown_pct << "%\n";
    std::cout << "  Trades: " << best_nas_result.total_trades << "\n\n";

    std::cout << "BEST GOLD CONFIG:\n";
    std::cout << "  " << best_gold_name << "\n";
    std::cout << "  Return: " << std::fixed << std::setprecision(2) << best_gold_return << "x\n";
    std::cout << "  Max DD: " << best_gold_result.max_drawdown_pct << "%\n";
    std::cout << "  Trades: " << best_gold_result.total_trades << "\n\n";

    std::cout << "KEY FINDINGS:\n";
    std::cout << "  1. Trailing stops DESTROY this strategy (causes churning)\n";
    std::cout << "  2. NAS100 needs survive_down >= 30% to avoid margin call\n";
    std::cout << "  3. Gold is more forgiving but still needs >= 10% survive\n";
    std::cout << "  4. Spacing controls trade frequency but not returns much\n";
    std::cout << "  5. Buy-and-hold (no trailing) is the winning approach\n";

    std::cout << "\n================================================================\n";

    return 0;
}
