#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../include/backtester_accurate.h"

struct TestResult {
    double final_equity;
    double max_dd;
    int trades;
    bool margin_call;
    double spread_cost;
};

TestResult run_test(double survive, double spacing, bool trailing, double atr) {
    std::ifstream file("NAS100/NAS100_TICKS_2025.csv");
    TestResult tr = {};
    if (!file.is_open()) return tr;

    std::string line;
    std::getline(file, line);

    BacktestConfig cfg;
    cfg.survive_down_pct = survive;
    cfg.min_entry_spacing = spacing;
    cfg.enable_trailing = trailing;
    cfg.atr_multiplier = atr;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 1.0;
    cfg.spread = 1.0;

    AccurateBacktester bt;
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
    tr.margin_call = r.margin_call_occurred;
    tr.spread_cost = r.total_spread_cost;
    return tr;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "NAS100 SEQUENTIAL TESTS\n";
    std::cout << "================================================================\n\n";
    std::cout << std::fixed << std::setprecision(2);

    std::cout << "Survive%  Final$      Return  MaxDD%   Trades  SpreadCost  Status\n";
    std::cout << "--------  ----------  ------  ------   ------  ----------  ------\n";

    // Test 1
    std::cout << "   4.00%  " << std::flush;
    auto r1 = run_test(4.0, 50.0, false, 0);
    std::cout << "$" << std::setw(9) << r1.final_equity << "  "
              << std::setw(5) << (r1.final_equity / 10000.0) << "x  "
              << std::setw(5) << r1.max_dd << "%   "
              << std::setw(5) << r1.trades << "   "
              << "$" << std::setw(8) << r1.spread_cost << "   "
              << (r1.margin_call ? "MARGIN CALL" : "OK") << "\n" << std::flush;

    // Test 2
    std::cout << "  10.00%  " << std::flush;
    auto r2 = run_test(10.0, 50.0, false, 0);
    std::cout << "$" << std::setw(9) << r2.final_equity << "  "
              << std::setw(5) << (r2.final_equity / 10000.0) << "x  "
              << std::setw(5) << r2.max_dd << "%   "
              << std::setw(5) << r2.trades << "   "
              << "$" << std::setw(8) << r2.spread_cost << "   "
              << (r2.margin_call ? "MARGIN CALL" : "OK") << "\n" << std::flush;

    // Test 3
    std::cout << "  20.00%  " << std::flush;
    auto r3 = run_test(20.0, 50.0, false, 0);
    std::cout << "$" << std::setw(9) << r3.final_equity << "  "
              << std::setw(5) << (r3.final_equity / 10000.0) << "x  "
              << std::setw(5) << r3.max_dd << "%   "
              << std::setw(5) << r3.trades << "   "
              << "$" << std::setw(8) << r3.spread_cost << "   "
              << (r3.margin_call ? "MARGIN CALL" : "OK") << "\n" << std::flush;

    // Test 4
    std::cout << "  30.00%  " << std::flush;
    auto r4 = run_test(30.0, 50.0, false, 0);
    std::cout << "$" << std::setw(9) << r4.final_equity << "  "
              << std::setw(5) << (r4.final_equity / 10000.0) << "x  "
              << std::setw(5) << r4.max_dd << "%   "
              << std::setw(5) << r4.trades << "   "
              << "$" << std::setw(8) << r4.spread_cost << "   "
              << (r4.margin_call ? "MARGIN CALL" : "OK") << "\n" << std::flush;

    // Test 5
    std::cout << "  40.00%  " << std::flush;
    auto r5 = run_test(40.0, 50.0, false, 0);
    std::cout << "$" << std::setw(9) << r5.final_equity << "  "
              << std::setw(5) << (r5.final_equity / 10000.0) << "x  "
              << std::setw(5) << r5.max_dd << "%   "
              << std::setw(5) << r5.trades << "   "
              << "$" << std::setw(8) << r5.spread_cost << "   "
              << (r5.margin_call ? "MARGIN CALL" : "OK") << "\n" << std::flush;

    // Test 6
    std::cout << "  50.00%  " << std::flush;
    auto r6 = run_test(50.0, 50.0, false, 0);
    std::cout << "$" << std::setw(9) << r6.final_equity << "  "
              << std::setw(5) << (r6.final_equity / 10000.0) << "x  "
              << std::setw(5) << r6.max_dd << "%   "
              << std::setw(5) << r6.trades << "   "
              << "$" << std::setw(8) << r6.spread_cost << "   "
              << (r6.margin_call ? "MARGIN CALL" : "OK") << "\n";

    std::cout << "\n================================================================\n";
    return 0;
}
