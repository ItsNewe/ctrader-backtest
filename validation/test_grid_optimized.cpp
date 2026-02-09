#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include "../include/grid_optimized_strategy.h"

struct Tick {
    double bid;
    double ask;
    double mid() const { return (bid + ask) / 2.0; }
};

std::vector<Tick> load_ticks(const std::string& filename, size_t max_ticks = 60000000) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file: " << filename << std::endl;
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

            // Format: timestamp<tab>bid<tab>ask (tab-separated)
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
            continue;  // Skip malformed lines
        }
    }
    return ticks;
}

void print_result(const char* name, double final_eq, double init_eq, double max_dd,
                  int trades, int wins, int trail_exits, bool margin_call = false) {
    double ret = final_eq / init_eq;
    double win_rate = trades > 0 ? 100.0 * wins / trades : 0;
    double trail_pct = trades > 0 ? 100.0 * trail_exits / trades : 0;

    printf("%-40s $%10.2f  %5.2fx  %5.1f%%  %5d  %5.1f%%  %5.1f%%%s\n",
           name, final_eq, ret, max_dd, trades, win_rate, trail_pct,
           margin_call ? " MC" : "");
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "GRID OPTIMIZED STRATEGY VALIDATION\n";
    std::cout << "================================================================\n\n";

    // Load data
    std::cout << "Loading NAS100 data...\n";
    auto nas_ticks = load_ticks("NAS100/NAS100_TICKS_2025.csv");
    std::cout << "Loaded " << nas_ticks.size() << " ticks\n";
    if (nas_ticks.empty()) {
        std::cerr << "ERROR: No NAS100 data loaded!\n";
        return 1;
    }
    std::cout << "Price range: " << nas_ticks.front().mid() << " -> " << nas_ticks.back().mid() << "\n\n";

    const double INITIAL = 10000.0;

    printf("%-40s %12s  %6s  %6s  %5s  %6s  %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%", "Trail%");
    printf("================================================================================\n");

    // Test NAS100 presets
    std::cout << "\n--- NAS100 PRESETS ---\n";

    {
        GridOptimizedStrategy strat;
        strat.configure(GridOptimizedConfig::NAS100_Default());
        strat.reset(INITIAL);
        for (const auto& t : nas_ticks) strat.on_tick(t.mid());
        double final_eq = strat.get_equity(nas_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;
        print_result("NAS100 Default", final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    {
        GridOptimizedStrategy strat;
        strat.configure(GridOptimizedConfig::NAS100_Conservative());
        strat.reset(INITIAL);
        for (const auto& t : nas_ticks) strat.on_tick(t.mid());
        double final_eq = strat.get_equity(nas_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;
        print_result("NAS100 Conservative", final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    {
        GridOptimizedStrategy strat;
        strat.configure(GridOptimizedConfig::NAS100_Aggressive());
        strat.reset(INITIAL);
        for (const auto& t : nas_ticks) strat.on_tick(t.mid());
        double final_eq = strat.get_equity(nas_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;
        print_result("NAS100 Aggressive", final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    // Custom parameter sweeps
    std::cout << "\n--- NAS100 SPACING SWEEP ---\n";
    for (double spacing : {5.0, 10.0, 25.0, 50.0, 100.0}) {
        GridOptimizedStrategy strat;
        auto cfg = GridOptimizedConfig::NAS100_Default();
        cfg.min_entry_spacing = spacing;
        strat.configure(cfg);
        strat.reset(INITIAL);

        for (const auto& t : nas_ticks) strat.on_tick(t.mid());

        double final_eq = strat.get_equity(nas_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;

        char name[64];
        snprintf(name, sizeof(name), "Spacing=%.0f pts", spacing);
        print_result(name, final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    std::cout << "\n--- NAS100 ATR MULTIPLIER SWEEP ---\n";
    for (double mult : {1.5, 2.0, 2.5, 3.0, 4.0}) {
        GridOptimizedStrategy strat;
        auto cfg = GridOptimizedConfig::NAS100_Default();
        cfg.atr_multiplier = mult;
        strat.configure(cfg);
        strat.reset(INITIAL);

        for (const auto& t : nas_ticks) strat.on_tick(t.mid());

        double final_eq = strat.get_equity(nas_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;

        char name[64];
        snprintf(name, sizeof(name), "ATR x%.1f", mult);
        print_result(name, final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    std::cout << "\n--- NAS100 SURVIVE % SWEEP ---\n";
    for (double surv : {5.0, 8.0, 10.0, 15.0, 20.0}) {
        GridOptimizedStrategy strat;
        auto cfg = GridOptimizedConfig::NAS100_Default();
        cfg.survive_down_pct = surv;
        strat.configure(cfg);
        strat.reset(INITIAL);

        for (const auto& t : nas_ticks) strat.on_tick(t.mid());

        double final_eq = strat.get_equity(nas_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;

        char name[64];
        snprintf(name, sizeof(name), "Survive=%.0f%%", surv);
        print_result(name, final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    // Load and test Gold
    std::cout << "\n================================================================\n";
    std::cout << "TESTING ON GOLD (XAUUSD)\n";
    std::cout << "================================================================\n\n";

    std::cout << "Loading Gold data...\n";
    auto gold_ticks = load_ticks("Grid/XAUUSD_TICKS_2025.csv");
    std::cout << "Loaded " << gold_ticks.size() << " ticks\n";
    if (gold_ticks.empty()) {
        std::cerr << "ERROR: No Gold data loaded!\n";
        return 1;
    }
    std::cout << "Price range: " << gold_ticks.front().mid() << " -> " << gold_ticks.back().mid() << "\n\n";

    printf("%-40s %12s  %6s  %6s  %5s  %6s  %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%", "Trail%");
    printf("================================================================================\n");

    std::cout << "\n--- GOLD PRESETS ---\n";

    {
        GridOptimizedStrategy strat;
        strat.configure(GridOptimizedConfig::Gold_Default());
        strat.reset(INITIAL);
        for (const auto& t : gold_ticks) strat.on_tick(t.mid());
        double final_eq = strat.get_equity(gold_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;
        print_result("Gold Default", final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    {
        GridOptimizedStrategy strat;
        strat.configure(GridOptimizedConfig::Gold_Conservative());
        strat.reset(INITIAL);
        for (const auto& t : gold_ticks) strat.on_tick(t.mid());
        double final_eq = strat.get_equity(gold_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;
        print_result("Gold Conservative", final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    std::cout << "\n--- GOLD SPACING SWEEP ---\n";
    for (double spacing : {0.5, 1.0, 2.0, 3.0, 5.0}) {
        GridOptimizedStrategy strat;
        auto cfg = GridOptimizedConfig::Gold_Default();
        cfg.min_entry_spacing = spacing;
        strat.configure(cfg);
        strat.reset(INITIAL);

        for (const auto& t : gold_ticks) strat.on_tick(t.mid());

        double final_eq = strat.get_equity(gold_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;

        char name[64];
        snprintf(name, sizeof(name), "Spacing=$%.1f", spacing);
        print_result(name, final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    std::cout << "\n--- GOLD SURVIVE % SWEEP ---\n";
    for (double surv : {1.0, 2.0, 3.0, 5.0, 8.0}) {
        GridOptimizedStrategy strat;
        auto cfg = GridOptimizedConfig::Gold_Default();
        cfg.survive_down_pct = surv;
        strat.configure(cfg);
        strat.reset(INITIAL);

        for (const auto& t : gold_ticks) strat.on_tick(t.mid());

        double final_eq = strat.get_equity(gold_ticks.back().mid());
        bool mc = final_eq < INITIAL * 0.01;

        char name[64];
        snprintf(name, sizeof(name), "Survive=%.0f%%", surv);
        print_result(name, final_eq, INITIAL, strat.get_max_drawdown(),
                     strat.get_total_trades(), strat.get_winning_trades(),
                     strat.get_trailing_exits(), mc);
    }

    std::cout << "\n================================================================\n";
    std::cout << "BEST CONFIGURATIONS FOUND\n";
    std::cout << "================================================================\n\n";

    // Find best for each symbol by testing a grid
    double best_nas_return = 0;
    GridOptimizedConfig best_nas_cfg;

    for (double spacing : {10.0, 25.0, 50.0}) {
        for (double atr : {1.5, 2.0, 2.5}) {
            for (double surv : {8.0, 10.0, 15.0}) {
                GridOptimizedStrategy strat;
                auto cfg = GridOptimizedConfig::NAS100_Default();
                cfg.min_entry_spacing = spacing;
                cfg.atr_multiplier = atr;
                cfg.survive_down_pct = surv;
                strat.configure(cfg);
                strat.reset(INITIAL);

                for (const auto& t : nas_ticks) strat.on_tick(t.mid());

                double final_eq = strat.get_equity(nas_ticks.back().mid());
                double ret = final_eq / INITIAL;

                if (ret > best_nas_return && strat.get_max_drawdown() < 60) {
                    best_nas_return = ret;
                    best_nas_cfg = cfg;
                }
            }
        }
    }

    double best_gold_return = 0;
    GridOptimizedConfig best_gold_cfg;

    for (double spacing : {0.5, 1.0, 2.0}) {
        for (double atr : {1.5, 2.0, 2.5}) {
            for (double surv : {1.0, 2.0, 3.0}) {
                GridOptimizedStrategy strat;
                auto cfg = GridOptimizedConfig::Gold_Default();
                cfg.min_entry_spacing = spacing;
                cfg.atr_multiplier = atr;
                cfg.survive_down_pct = surv;
                strat.configure(cfg);
                strat.reset(INITIAL);

                for (const auto& t : gold_ticks) strat.on_tick(t.mid());

                double final_eq = strat.get_equity(gold_ticks.back().mid());
                double ret = final_eq / INITIAL;

                if (ret > best_gold_return && strat.get_max_drawdown() < 60) {
                    best_gold_return = ret;
                    best_gold_cfg = cfg;
                }
            }
        }
    }

    std::cout << "BEST NAS100 CONFIG:\n";
    std::cout << "  survive_down_pct = " << best_nas_cfg.survive_down_pct << "%\n";
    std::cout << "  min_entry_spacing = " << best_nas_cfg.min_entry_spacing << " pts\n";
    std::cout << "  atr_multiplier = " << best_nas_cfg.atr_multiplier << "x\n";
    std::cout << "  max_portfolio_dd = " << best_nas_cfg.max_portfolio_dd << "%\n";
    std::cout << "  Expected return: " << std::fixed << std::setprecision(2) << best_nas_return << "x\n\n";

    std::cout << "BEST GOLD CONFIG:\n";
    std::cout << "  survive_down_pct = " << best_gold_cfg.survive_down_pct << "%\n";
    std::cout << "  min_entry_spacing = $" << best_gold_cfg.min_entry_spacing << "\n";
    std::cout << "  atr_multiplier = " << best_gold_cfg.atr_multiplier << "x\n";
    std::cout << "  max_portfolio_dd = " << best_gold_cfg.max_portfolio_dd << "%\n";
    std::cout << "  Expected return: " << std::fixed << std::setprecision(2) << best_gold_return << "x\n";

    std::cout << "\n================================================================\n";

    return 0;
}
