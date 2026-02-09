#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include "../include/grid_optimized_strategy.h"

struct Tick {
    std::string timestamp;
    double bid;
    double ask;
    double mid() const { return (bid + ask) / 2.0; }
};

std::vector<Tick> load_ticks_in_range(const std::string& filename,
                                       const std::string& start_date,
                                       const std::string& end_date) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        Tick t;

        std::getline(ss, token, '\t');
        t.timestamp = token;

        // Check date range
        std::string date = token.substr(0, 10);
        if (date < start_date) continue;
        if (date > end_date) break;

        std::getline(ss, token, '\t');
        if (token.empty()) continue;
        t.bid = std::stod(token);

        std::getline(ss, token, '\t');
        if (token.empty()) continue;
        t.ask = std::stod(token);

        if (t.bid > 0 && t.ask > 0) {
            ticks.push_back(t);
        }
    }
    return ticks;
}

void test_config(const std::vector<Tick>& ticks, const char* name,
                 GridOptimizedConfig cfg, double initial = 10000.0) {
    GridOptimizedStrategy strat;
    strat.configure(cfg);
    strat.reset(initial);

    for (const auto& t : ticks) {
        strat.on_tick(t.mid());
    }

    double final_eq = strat.get_equity(ticks.back().mid());
    double ret = final_eq / initial;
    bool mc = final_eq < initial * 0.01;

    printf("%-45s $%10.2f  %5.2fx  %5.1f%%  %5d  %5.1f%%%s\n",
           name, final_eq, ret, strat.get_max_drawdown(),
           strat.get_total_trades(), strat.get_win_rate(),
           mc ? " MC" : "");
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "TESTING ON USER'S EXACT TIME PERIODS\n";
    std::cout << "================================================================\n\n";

    // Test NAS100 on user's period: 2025.04.22 - 2025.07.31
    std::cout << "Loading NAS100 (2025.04.22 - 2025.07.31)...\n";
    auto nas_ticks = load_ticks_in_range("NAS100/NAS100_TICKS_2025.csv",
                                          "2025.04.22", "2025.07.31");
    std::cout << "Loaded " << nas_ticks.size() << " ticks\n";
    if (!nas_ticks.empty()) {
        std::cout << "Price: " << nas_ticks.front().mid() << " -> " << nas_ticks.back().mid();
        double change = (nas_ticks.back().mid() - nas_ticks.front().mid()) / nas_ticks.front().mid() * 100;
        std::cout << " (" << (change > 0 ? "+" : "") << change << "%)\n\n";
    }

    printf("%-45s %12s  %6s  %6s  %5s  %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%");
    printf("================================================================================\n");

    // Test various spacing values
    std::cout << "\n--- NAS100 SPACING SWEEP (your period) ---\n";
    for (double spacing : {5.0, 10.0, 25.0, 50.0, 100.0, 200.0}) {
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = 10.0;
        cfg.min_entry_spacing = spacing;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 50.0;
        cfg.contract_size = 1.0;
        cfg.leverage = 500.0;

        char name[64];
        snprintf(name, sizeof(name), "Spacing=%.0f pts", spacing);
        test_config(nas_ticks, name, cfg);
    }

    std::cout << "\n--- NAS100 ATR MULTIPLIER SWEEP (your period) ---\n";
    for (double atr : {1.5, 2.0, 2.5, 3.0, 4.0, 5.0}) {
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = 10.0;
        cfg.min_entry_spacing = 50.0;
        cfg.atr_multiplier = atr;
        cfg.max_portfolio_dd = 50.0;
        cfg.contract_size = 1.0;
        cfg.leverage = 500.0;

        char name[64];
        snprintf(name, sizeof(name), "ATR x%.1f (spacing=50)", atr);
        test_config(nas_ticks, name, cfg);
    }

    std::cout << "\n--- NAS100 SURVIVE % SWEEP (your period) ---\n";
    for (double surv : {5.0, 8.0, 10.0, 15.0, 20.0, 30.0}) {
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = surv;
        cfg.min_entry_spacing = 50.0;
        cfg.atr_multiplier = 2.5;
        cfg.max_portfolio_dd = 50.0;
        cfg.contract_size = 1.0;
        cfg.leverage = 500.0;

        char name[64];
        snprintf(name, sizeof(name), "Survive=%.0f%% (spacing=50, ATR=2.5)", surv);
        test_config(nas_ticks, name, cfg);
    }

    // Test Gold on user's period: 2025.01.01 - 2025.12.29
    std::cout << "\n================================================================\n";
    std::cout << "Loading Gold (2025.01.01 - 2025.12.29)...\n";
    auto gold_ticks = load_ticks_in_range("Grid/XAUUSD_TICKS_2025.csv",
                                           "2025.01.01", "2025.12.29");
    std::cout << "Loaded " << gold_ticks.size() << " ticks\n";
    if (!gold_ticks.empty()) {
        std::cout << "Price: " << gold_ticks.front().mid() << " -> " << gold_ticks.back().mid();
        double change = (gold_ticks.back().mid() - gold_ticks.front().mid()) / gold_ticks.front().mid() * 100;
        std::cout << " (" << (change > 0 ? "+" : "") << change << "%)\n\n";
    }

    printf("%-45s %12s  %6s  %6s  %5s  %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%");
    printf("================================================================================\n");

    std::cout << "\n--- GOLD SPACING SWEEP (your period) ---\n";
    for (double spacing : {0.5, 1.0, 2.0, 3.0, 5.0, 10.0}) {
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = 2.0;
        cfg.min_entry_spacing = spacing;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 30.0;
        cfg.contract_size = 100.0;
        cfg.leverage = 500.0;
        cfg.spread = 0.20;

        char name[64];
        snprintf(name, sizeof(name), "Spacing=$%.1f", spacing);
        test_config(gold_ticks, name, cfg);
    }

    std::cout << "\n--- GOLD SURVIVE % SWEEP (your period) ---\n";
    for (double surv : {1.0, 2.0, 3.0, 5.0, 8.0}) {
        GridOptimizedConfig cfg;
        cfg.survive_down_pct = surv;
        cfg.min_entry_spacing = 2.0;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 30.0;
        cfg.contract_size = 100.0;
        cfg.leverage = 500.0;
        cfg.spread = 0.20;

        char name[64];
        snprintf(name, sizeof(name), "Survive=%.0f%% (spacing=$2)", surv);
        test_config(gold_ticks, name, cfg);
    }

    std::cout << "\n================================================================\n";
    return 0;
}
