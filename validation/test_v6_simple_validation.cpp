/**
 * V6 Simple Validation Test
 *
 * Simplified test comparing V5 baseline vs V6 with wider TP
 * Uses direct implementation without TickBasedEngine for reliability
 */

#include "../include/fill_up_strategy_v5.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <deque>
#include <map>
#include <cmath>
#include <algorithm>

using namespace backtest;

struct TestPeriod {
    std::string name;
    size_t start_line;
    size_t num_lines;
};

struct SMA {
    std::deque<double> prices;
    int period;
    double sum;

    SMA(int p) : period(p), sum(0.0) {}

    void Add(double price) {
        prices.push_back(price);
        sum += price;

        if ((int)prices.size() > period) {
            sum -= prices.front();
            prices.pop_front();
        }
    }

    double Get() const {
        if (prices.size() < (size_t)period) return 0.0;
        return sum / period;
    }

    bool IsReady() const {
        return prices.size() >= (size_t)period;
    }
};

struct SimpleResult {
    double return_pct;
    double max_dd;
    int total_trades;
    bool stop_out;
};

SimpleResult RunSimpleTest(const std::vector<Tick>& ticks,
                           int sma_period,
                           double tp_multiplier,
                           double initial_balance = 10000.0) {
    SimpleResult result = {0, 0, 0, false};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    SMA sma(sma_period);

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Update peak equity
        if (positions.empty()) {
            if (peak_equity != balance) {
                peak_equity = balance;
                partial_done = false;
                all_closed = false;
            }
        }
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_dd = std::max(result.max_dd, dd_pct);

        // V3 Protection: Close ALL at 25% DD
        if (dd_pct > 25.0 && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at 8% DD
        if (dd_pct > 8.0 && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Stop out check
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            result.stop_out = true;
            break;
        }

        // TP check
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                delete t;
                it = positions.erase(it);
                result.total_trades++;
            } else {
                ++it;
            }
        }

        // Open new position logic
        if (dd_pct < 5.0 && (int)positions.size() < 20 && sma.IsReady()) {
            // V5: SMA filter - only open if price > SMA
            if (tick.bid > sma.Get()) {
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (Trade* t : positions) {
                    lowest = std::min(lowest, t->entry_price);
                    highest = std::max(highest, t->entry_price);
                }

                bool should_open = positions.empty() ||
                                   (lowest >= tick.ask + spacing) ||
                                   (highest <= tick.ask - spacing);

                if (should_open) {
                    double lot = 0.01;
                    double margin_needed = lot * contract_size * tick.ask / leverage;
                    if (equity - used_margin > margin_needed * 2) {
                        Trade* t = new Trade();
                        t->id = next_id++;
                        t->entry_price = tick.ask;
                        t->lot_size = lot;
                        // V6: Apply TP multiplier
                        t->take_profit = tick.ask + (tick.spread() + spacing) * tp_multiplier;
                        positions.push_back(t);
                    }
                }
            }
        }
    }

    // Close remaining
    if (!ticks.empty() && !positions.empty()) {
        const Tick& last_tick = ticks.back();
        for (Trade* t : positions) {
            balance += (last_tick.bid - t->entry_price) * t->lot_size * contract_size;
            delete t;
        }
        positions.clear();
    }

    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    return result;
}

std::vector<Tick> LoadTicks(const std::string& filename, size_t start_line, size_t num_lines) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open file: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    size_t current_line = 0;

    // Skip header
    std::getline(file, line);

    while (std::getline(file, line) && ticks.size() < num_lines) {
        current_line++;
        if (current_line < start_line) continue;

        std::stringstream ss(line);
        std::string timestamp, bid_str, ask_str;

        std::getline(ss, timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        try {
            if (bid_str.empty() || ask_str.empty()) continue;

            Tick tick;
            tick.timestamp = timestamp;
            tick.bid = std::stod(bid_str);
            tick.ask = std::stod(ask_str);

            if (tick.bid <= 0 || tick.ask <= 0 || tick.ask < tick.bid) {
                continue;
            }

            ticks.push_back(tick);
        } catch (...) {
            continue;
        }
    }

    file.close();
    return ticks;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "V6 FINAL VALIDATION TEST" << std::endl;
    std::cout << "Comparing V5 Baseline (TP 1.0x) vs V6 Optimal (TP 2.0x)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::string data_file = "Grid/XAUUSD_TICKS_2025.csv";

    // Define all 6 original test periods
    std::vector<TestPeriod> periods = {
        {"Jan 2025",      1,        500000},
        {"Apr 2025",      8000000,  500000},
        {"Jun 2025",      12000000, 500000},
        {"Oct 2025",      20000000, 500000},
        {"Dec Pre-Crash", 50000000, 1500000},
        {"Dec Crash",     51314023, 2000000},
    };

    std::cout << "\nTest Periods:" << std::endl;
    for (const auto& period : periods) {
        std::cout << "  - " << std::left << std::setw(15) << period.name << ": "
                  << period.num_lines << " ticks starting at line "
                  << period.start_line << std::endl;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "LOADING DATA AND RUNNING TESTS..." << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Store results
    struct TestResult {
        std::string name;
        std::vector<SimpleResult> period_results;
        double total_return;
        double max_dd;
        int total_trades;
    };

    std::vector<TestResult> results;

    // Test V5 Baseline (TP 1.0x)
    {
        TestResult result;
        result.name = "V5 Baseline (TP 1.0x)";
        result.total_return = 0.0;
        result.max_dd = 0.0;
        result.total_trades = 0;

        std::cout << "\nTesting " << result.name << "..." << std::endl;

        for (const auto& period : periods) {
            std::cout << "  Loading " << period.name << "... " << std::flush;
            auto ticks = LoadTicks(data_file, period.start_line, period.num_lines);
            std::cout << ticks.size() << " ticks loaded... " << std::flush;

            SimpleResult r = RunSimpleTest(ticks, 11000, 1.0);
            result.period_results.push_back(r);
            result.total_return += r.return_pct;
            result.max_dd = std::max(result.max_dd, r.max_dd);
            result.total_trades += r.total_trades;

            std::cout << "Return: " << r.return_pct << "%" << std::endl;
        }

        results.push_back(result);
    }

    // Test V6 Optimal (TP 2.0x)
    {
        TestResult result;
        result.name = "V6 Optimal (TP 2.0x)";
        result.total_return = 0.0;
        result.max_dd = 0.0;
        result.total_trades = 0;

        std::cout << "\nTesting " << result.name << "..." << std::endl;

        for (const auto& period : periods) {
            std::cout << "  Loading " << period.name << "... " << std::flush;
            auto ticks = LoadTicks(data_file, period.start_line, period.num_lines);
            std::cout << ticks.size() << " ticks loaded... " << std::flush;

            SimpleResult r = RunSimpleTest(ticks, 11000, 2.0);
            result.period_results.push_back(r);
            result.total_return += r.return_pct;
            result.max_dd = std::max(result.max_dd, r.max_dd);
            result.total_trades += r.total_trades;

            std::cout << "Return: " << r.return_pct << "%" << std::endl;
        }

        results.push_back(result);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

    // Print results table
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Header
    std::cout << "\n" << std::left << std::setw(25) << "Configuration";
    for (const auto& period : periods) {
        std::cout << " | " << std::setw(14) << period.name;
    }
    std::cout << " | " << std::setw(12) << "TOTAL"
              << " | " << std::setw(10) << "MaxDD"
              << " | " << std::setw(10) << "Trades" << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    // Results
    for (const auto& result : results) {
        std::cout << std::left << std::setw(25) << result.name;

        for (size_t i = 0; i < result.period_results.size(); i++) {
            std::cout << " | " << std::right << std::setw(13) << result.period_results[i].return_pct << "%";
        }

        std::cout << " | " << std::right << std::setw(11) << result.total_return << "%"
                  << " | " << std::setw(9) << result.max_dd << "%"
                  << " | " << std::setw(10) << result.total_trades << std::endl;
    }

    std::cout << std::string(120, '-') << std::endl;

    // Analysis
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "IMPROVEMENT ANALYSIS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    double baseline_return = results[0].total_return;
    double v6_return = results[1].total_return;
    double improvement = v6_return - baseline_return;
    double improvement_pct = (baseline_return != 0) ? (improvement / std::abs(baseline_return)) * 100.0 : 0.0;

    std::cout << "\nV5 Baseline (SMA 11000, TP 1.0x):" << std::endl;
    std::cout << "  Total Return:  " << baseline_return << "%" << std::endl;
    std::cout << "  Max Drawdown:  " << results[0].max_dd << "%" << std::endl;
    std::cout << "  Total Trades:  " << results[0].total_trades << std::endl;

    std::cout << "\nV6 Optimal (SMA 11000, TP 2.0x):" << std::endl;
    std::cout << "  Total Return:  " << v6_return << "%" << std::endl;
    std::cout << "  Max Drawdown:  " << results[1].max_dd << "%" << std::endl;
    std::cout << "  Total Trades:  " << results[1].total_trades << std::endl;

    std::cout << "\nImprovement:" << std::endl;
    std::cout << "  Absolute:      " << std::showpos << improvement << "%" << std::noshowpos << std::endl;
    std::cout << "  Relative:      " << std::showpos << improvement_pct << "%" << std::noshowpos << std::endl;

    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "Test completed in " << duration.count() << " seconds" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Conclusion
    std::cout << "\nCONCLUSION:" << std::endl;
    if (improvement > 0) {
        std::cout << "  SUCCESS: V6 (TP 2.0x) outperforms V5 baseline" << std::endl;
        std::cout << "  RECOMMENDATION: Deploy V6 with tp_multiplier = 2.0" << std::endl;
    } else {
        std::cout << "  V6 does not improve over V5 baseline" << std::endl;
        std::cout << "  RECOMMENDATION: Stick with V5 (tp_multiplier = 1.0)" << std::endl;
    }

    std::cout << std::endl;

    return 0;
}
