#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include "../include/strategy_fillup_hedged.h"

struct TestConfig {
    std::string name;
    double survive_pct;
    double spacing;
    double hedge_ratio;
    double hedge_trigger;
    bool dd_protection;
    double dd_threshold;
    bool velocity_filter;
    double velocity_threshold;
    int velocity_window;
};

struct TestResult {
    std::string name;
    double return_x;
    double max_dd;
    int long_entries;
    int short_entries;
    int dd_triggers;
    int vel_pauses;
    bool margin_call;
};

std::vector<double> load_prices(const std::string& filename) {
    std::vector<double> bids, asks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << "\n";
        return {};
    }

    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line)) {
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
            bids.push_back(bid);
            asks.push_back(ask);
        }
    }

    // Return interleaved bid/ask
    std::vector<double> result;
    result.reserve(bids.size() * 2);
    for (size_t i = 0; i < bids.size(); i++) {
        result.push_back(bids[i]);
        result.push_back(asks[i]);
    }
    return result;
}

TestResult run_test(const std::vector<double>& prices, const TestConfig& tc) {
    HedgedFillUpConfig cfg;
    cfg.contract_size = 100.0;
    cfg.spread = 0.25;
    cfg.spacing = tc.spacing;
    cfg.hedge_spacing = tc.spacing;
    cfg.survive_down_pct = tc.survive_pct;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 20.0;

    cfg.enable_hedging = (tc.hedge_ratio > 0);
    cfg.hedge_ratio = tc.hedge_ratio;
    cfg.hedge_trigger_pct = tc.hedge_trigger;

    cfg.enable_dd_protection = tc.dd_protection;
    cfg.close_all_dd_pct = tc.dd_threshold;

    cfg.enable_velocity_filter = tc.velocity_filter;
    cfg.crash_velocity_pct = tc.velocity_threshold;
    cfg.velocity_window = tc.velocity_window;

    HedgedFillUpStrategy bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    double last_bid = 0, last_ask = 0;
    for (size_t i = 0; i < prices.size(); i += 2) {
        double bid = prices[i];
        double ask = prices[i + 1];
        last_bid = bid;
        last_ask = ask;
        bt.on_tick(bid, ask);
    }

    auto r = bt.get_result(last_bid, last_ask);

    TestResult tr;
    tr.name = tc.name;
    tr.return_x = r.final_equity / 10000.0;
    tr.max_dd = r.max_drawdown_pct;
    tr.long_entries = r.long_entries;
    tr.short_entries = r.short_entries;
    tr.dd_triggers = r.dd_protection_triggers;
    tr.vel_pauses = r.velocity_pauses;
    tr.margin_call = r.margin_call_occurred;

    return tr;
}

int main() {
    std::cout << "Loading Gold tick data...\n";
    std::cout.flush();
    auto prices = load_prices("validation/Grid/XAUUSD_TICKS_2025.csv");
    if (prices.empty()) {
        std::cerr << "Failed to load data\n";
        return 1;
    }
    std::cout << "Loaded " << (prices.size() / 2) << " ticks\n\n";
    std::cout.flush();

    // Define test configurations
    std::vector<TestConfig> tests = {
        // Baseline tests
        {"BASE_13", 13, 1.0, 0, 1.0, false, 70, false, -1.0, 500},
        {"BASE_10", 10, 1.0, 0, 1.0, false, 70, false, -1.0, 500},
        {"BASE_8", 8, 1.0, 0, 1.0, false, 70, false, -1.0, 500},
        {"BASE_6", 6, 1.0, 0, 1.0, false, 70, false, -1.0, 500},
        {"BASE_5", 5, 1.0, 0, 1.0, false, 70, false, -1.0, 500},

        // Hedge only tests
        {"H_6_01", 6, 1.0, 0.1, 1.0, false, 70, false, -1.0, 500},
        {"H_6_02", 6, 1.0, 0.2, 1.0, false, 70, false, -1.0, 500},
        {"H_6_03", 6, 1.0, 0.3, 1.0, false, 70, false, -1.0, 500},
        {"H_8_01", 8, 1.0, 0.1, 1.0, false, 70, false, -1.0, 500},
        {"H_8_02", 8, 1.0, 0.2, 1.0, false, 70, false, -1.0, 500},

        // DD Protection only tests
        {"D_13_50", 13, 1.0, 0, 1.0, true, 50, false, -1.0, 500},
        {"D_13_60", 13, 1.0, 0, 1.0, true, 60, false, -1.0, 500},
        {"D_13_70", 13, 1.0, 0, 1.0, true, 70, false, -1.0, 500},
        {"D_6_50", 6, 1.0, 0, 1.0, true, 50, false, -1.0, 500},
        {"D_6_60", 6, 1.0, 0, 1.0, true, 60, false, -1.0, 500},
        {"D_6_70", 6, 1.0, 0, 1.0, true, 70, false, -1.0, 500},
        {"D_6_80", 6, 1.0, 0, 1.0, true, 80, false, -1.0, 500},

        // Velocity Filter only tests
        {"V_13_05", 13, 1.0, 0, 1.0, false, 70, true, -0.5, 500},
        {"V_13_10", 13, 1.0, 0, 1.0, false, 70, true, -1.0, 500},
        {"V_13_15", 13, 1.0, 0, 1.0, false, 70, true, -1.5, 500},
        {"V_6_10", 6, 1.0, 0, 1.0, false, 70, true, -1.0, 500},

        // Hedge + DD Protection
        {"HD_6_01_70", 6, 1.0, 0.1, 1.0, true, 70, false, -1.0, 500},
        {"HD_6_02_60", 6, 1.0, 0.2, 1.0, true, 60, false, -1.0, 500},
        {"HD_6_01_60", 6, 1.0, 0.1, 1.0, true, 60, false, -1.0, 500},
        {"HD_8_01_60", 8, 1.0, 0.1, 1.0, true, 60, false, -1.0, 500},

        // Hedge + Velocity
        {"HV_6_01_10", 6, 1.0, 0.1, 1.0, false, 70, true, -1.0, 500},
        {"HV_6_02_10", 6, 1.0, 0.2, 1.0, false, 70, true, -1.0, 500},

        // DD + Velocity
        {"DV_13_60_10", 13, 1.0, 0, 1.0, true, 60, true, -1.0, 500},
        {"DV_6_60_10", 6, 1.0, 0, 1.0, true, 60, true, -1.0, 500},

        // Triple combo: Hedge + DD + Velocity
        {"HDV_6_01_60_10", 6, 1.0, 0.1, 1.0, true, 60, true, -1.0, 500},
        {"HDV_6_02_60_10", 6, 1.0, 0.2, 1.0, true, 60, true, -1.0, 500},
        {"HDV_8_01_60_10", 8, 1.0, 0.1, 1.0, true, 60, true, -1.0, 500},

        // Aggressive combos (survive=5% - normally margin calls)
        {"H_5_02", 5, 1.0, 0.2, 1.0, false, 70, false, -1.0, 500},
        {"HD_5_02_60", 5, 1.0, 0.2, 1.0, true, 60, false, -1.0, 500},
        {"HDV_5_02_60_10", 5, 1.0, 0.2, 1.0, true, 60, true, -1.0, 500},
    };

    // Run all tests
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "================================================================================\n";
    std::cout << "COMBINATORIC TEST RESULTS - Gold 2025\n";
    std::cout << "================================================================================\n\n";

    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Longs"
              << std::setw(10) << "Shorts"
              << std::setw(10) << "DD_Trig"
              << std::setw(10) << "Vel_Pause"
              << std::setw(12) << "Margin"
              << "\n";
    std::cout << std::string(90, '-') << "\n";

    std::vector<TestResult> results;
    for (const auto& tc : tests) {
        auto r = run_test(prices, tc);
        results.push_back(r);

        std::cout << std::left << std::setw(18) << r.name
                  << std::right << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd << "%"
                  << std::setw(10) << r.long_entries
                  << std::setw(10) << r.short_entries
                  << std::setw(10) << r.dd_triggers
                  << std::setw(10) << r.vel_pauses
                  << std::setw(12) << (r.margin_call ? "YES" : "NO")
                  << "\n";
    }

    std::cout << std::string(90, '-') << "\n\n";

    // Find best configurations
    std::cout << "================================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "================================================================================\n\n";

    // Best return (no margin call)
    double best_return = 0;
    std::string best_return_name;
    for (const auto& r : results) {
        if (!r.margin_call && r.return_x > best_return) {
            best_return = r.return_x;
            best_return_name = r.name;
        }
    }
    std::cout << "Best Return (no margin call): " << best_return_name << " = " << best_return << "x\n";

    // Best risk-adjusted (return / max_dd)
    double best_ratio = 0;
    std::string best_ratio_name;
    for (const auto& r : results) {
        if (!r.margin_call && r.max_dd > 0) {
            double ratio = r.return_x / r.max_dd * 100;
            if (ratio > best_ratio) {
                best_ratio = ratio;
                best_ratio_name = r.name;
            }
        }
    }
    std::cout << "Best Risk-Adjusted (return/DD): " << best_ratio_name << "\n";

    // Best with DD < 50%
    best_return = 0;
    best_return_name = "";
    for (const auto& r : results) {
        if (!r.margin_call && r.max_dd < 50 && r.return_x > best_return) {
            best_return = r.return_x;
            best_return_name = r.name;
        }
    }
    if (!best_return_name.empty()) {
        std::cout << "Best Return (DD < 50%): " << best_return_name << " = " << best_return << "x\n";
    } else {
        std::cout << "Best Return (DD < 50%): None achieved\n";
    }

    // Did any survive=5 config survive?
    std::cout << "\nSurvive=5% configurations that survived:\n";
    for (const auto& r : results) {
        if (r.name.find("_5_") != std::string::npos && !r.margin_call) {
            std::cout << "  " << r.name << ": " << r.return_x << "x, DD=" << r.max_dd << "%\n";
        }
    }

    return 0;
}
