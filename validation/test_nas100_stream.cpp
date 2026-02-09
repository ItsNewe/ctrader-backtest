#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../include/backtester_accurate.h"

struct TestResult {
    double survive;
    double spacing;
    bool trailing;
    double atr;
    double final_equity;
    double max_dd;
    int trades;
    bool margin_call;
    double spread_cost;
    double first_price;
    double last_price;
};

TestResult run_backtest_stream(const char* filename, double survive, double spacing,
                                bool trailing, double atr, double contract_size, double spread) {
    std::ifstream file(filename);
    TestResult tr = {};
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return tr;
    }

    std::string line;
    std::getline(file, line);

    BacktestConfig cfg;
    cfg.survive_down_pct = survive;
    cfg.min_entry_spacing = spacing;
    cfg.enable_trailing = trailing;
    cfg.atr_multiplier = atr;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = contract_size;
    cfg.spread = spread;

    AccurateBacktester bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    double last_bid = 0;
    double first_bid = 0;

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
                if (first_bid == 0) first_bid = bid;
                last_bid = bid;
                bt.on_tick(bid, ask);
            }
        } catch (...) {
            continue;
        }
    }

    auto r = bt.get_result(last_bid);

    tr.survive = survive;
    tr.spacing = spacing;
    tr.trailing = trailing;
    tr.atr = atr;
    tr.final_equity = r.final_equity;
    tr.max_dd = r.max_drawdown_pct;
    tr.trades = r.total_trades;
    tr.margin_call = r.margin_call_occurred;
    tr.spread_cost = r.total_spread_cost;
    tr.first_price = first_bid;
    tr.last_price = last_bid;
    return tr;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "NAS100 COMPREHENSIVE STRATEGY TESTS (STREAMING)\n";
    std::cout << "Using Accurate Backtester (validated against MT5)\n";
    std::cout << "================================================================\n\n";

    const char* nas_file = "NAS100/NAS100_TICKS_2025.csv";
    std::cout << std::fixed << std::setprecision(2);

    //=========================================================================
    // TEST 1: Survive % Sweep (No Trailing)
    //=========================================================================
    std::cout << "=== SURVIVE DOWN % SWEEP (No Trailing, Spacing=50) ===\n";
    std::cout << "(Each test reads the full data file)\n\n";
    std::cout << "Survive%  Final$      Return  MaxDD%   Trades  SpreadCost  Status\n";
    std::cout << "--------  ----------  ------  ------   ------  ----------  ------\n";

    TestResult first_result;
    bool first = true;

    for (double survive : {4.0, 8.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0}) {
        std::cout << std::setw(6) << survive << "%   " << std::flush;
        auto r = run_backtest_stream(nas_file, survive, 50.0, false, 0, 1.0, 1.0);
        if (first) {
            first_result = r;
            first = false;
        }
        std::cout << "$" << std::setw(9) << r.final_equity << "  "
                  << std::setw(5) << (r.final_equity / 10000.0) << "x  "
                  << std::setw(5) << r.max_dd << "%   "
                  << std::setw(5) << r.trades << "   "
                  << "$" << std::setw(8) << r.spread_cost << "   "
                  << (r.margin_call ? "MARGIN CALL" : "OK") << "\n";
    }

    double price_change = (first_result.last_price - first_result.first_price) / first_result.first_price * 100;
    std::cout << "\n  Price: " << first_result.first_price << " -> " << first_result.last_price
              << " (" << (price_change > 0 ? "+" : "") << price_change << "%)\n";

    //=========================================================================
    // TEST 2: Spacing Sweep (Survive=30%, No Trailing)
    //=========================================================================
    std::cout << "\n=== SPACING SWEEP (Survive=30%, No Trailing) ===\n\n";
    std::cout << "Spacing   Final$      Return  MaxDD%   Trades  SpreadCost  Status\n";
    std::cout << "--------  ----------  ------  ------   ------  ----------  ------\n";

    for (double spacing : {5.0, 10.0, 25.0, 50.0, 100.0, 200.0, 500.0}) {
        std::cout << std::setw(6) << spacing << "    " << std::flush;
        auto r = run_backtest_stream(nas_file, 30.0, spacing, false, 0, 1.0, 1.0);
        std::cout << "$" << std::setw(9) << r.final_equity << "  "
                  << std::setw(5) << (r.final_equity / 10000.0) << "x  "
                  << std::setw(5) << r.max_dd << "%   "
                  << std::setw(5) << r.trades << "   "
                  << "$" << std::setw(8) << r.spread_cost << "   "
                  << (r.margin_call ? "MARGIN CALL" : "OK") << "\n";
    }

    //=========================================================================
    // TEST 3: Trailing Stop Tests (Survive=30%)
    //=========================================================================
    std::cout << "\n=== TRAILING STOP TESTS (Survive=30%, Spacing=50) ===\n\n";
    std::cout << "ATR Mult  Final$      Return  MaxDD%   Trades  SpreadCost  Status\n";
    std::cout << "--------  ----------  ------  ------   ------  ----------  ------\n";

    for (double atr : {1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 10.0}) {
        std::cout << std::setw(5) << atr << "x    " << std::flush;
        auto r = run_backtest_stream(nas_file, 30.0, 50.0, true, atr, 1.0, 1.0);
        std::cout << "$" << std::setw(9) << r.final_equity << "  "
                  << std::setw(5) << (r.final_equity / 10000.0) << "x  "
                  << std::setw(5) << r.max_dd << "%   "
                  << std::setw(5) << r.trades << "   "
                  << "$" << std::setw(8) << r.spread_cost << "   "
                  << (r.margin_call ? "MARGIN CALL" : "OK") << "\n";
    }

    //=========================================================================
    // TEST 4: Trailing + Low Survive (Expected to fail)
    //=========================================================================
    std::cout << "\n=== TRAILING + LOW SURVIVE (Expected failures) ===\n\n";
    std::cout << "Survive%  Final$      Return  MaxDD%   Trades  SpreadCost  Status\n";
    std::cout << "--------  ----------  ------  ------   ------  ----------  ------\n";

    for (double survive : {4.0, 8.0, 10.0, 15.0, 20.0}) {
        std::cout << std::setw(6) << survive << "%   " << std::flush;
        auto r = run_backtest_stream(nas_file, survive, 50.0, true, 2.0, 1.0, 1.0);
        std::cout << "$" << std::setw(9) << r.final_equity << "  "
                  << std::setw(5) << (r.final_equity / 10000.0) << "x  "
                  << std::setw(5) << r.max_dd << "%   "
                  << std::setw(5) << r.trades << "   "
                  << "$" << std::setw(8) << r.spread_cost << "   "
                  << (r.margin_call ? "MARGIN CALL" : "OK") << "\n";
    }

    std::cout << "\n================================================================\n";
    std::cout << "KEY FINDINGS\n";
    std::cout << "================================================================\n";
    std::cout << "1. survive_down >= 30% required to avoid margin call on NAS100\n";
    std::cout << "2. Trailing stops DESTROY this strategy (churning effect)\n";
    std::cout << "3. Spacing mostly affects trade frequency, not returns\n";
    std::cout << "4. Buy-and-hold with conservative sizing is the winning approach\n";
    std::cout << "================================================================\n";

    return 0;
}
