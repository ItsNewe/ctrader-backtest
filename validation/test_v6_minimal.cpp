/**
 * Minimal V6 Validation Test
 * Tests V5 vs V6 on a small subset of data to validate the improvement
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cfloat>

struct Tick {
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
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

struct Result {
    double return_pct;
    double max_dd;
    int trades;
};

Result RunTest(const std::vector<Tick>& ticks, int sma_period, double tp_mult) {
    Result r = {0, 0, 0};
    if (ticks.empty()) return r;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
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

        // Peak equity reset when no positions
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        r.max_dd = std::max(r.max_dd, dd_pct);

        // Close ALL at 25% DD
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

        // Partial close at 8% DD
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
            break;
        }

        // TP check
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                r.trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Open new (V5: only when price > SMA, and DD < 5%)
        bool trend_ok = !sma.IsReady() || (tick.bid > sma.Get());
        if (dd_pct < 5.0 && trend_ok && (int)positions.size() < 20) {
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
                    t->take_profit = tick.ask + tick.spread() + (spacing * tp_mult);
                    positions.push_back(t);
                }
            }
        }
    }

    // Close remaining
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.trades++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    return r;
}

std::vector<Tick> LoadTicks(const std::string& filename, size_t start, size_t count) {
    std::vector<Tick> ticks;
    ticks.reserve(count);

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // skip header

    size_t current = 0;
    while (std::getline(file, line) && ticks.size() < count) {
        current++;
        if (current < start) continue;

        std::stringstream ss(line);
        std::string ts, bid_s, ask_s;
        std::getline(ss, ts, '\t');
        std::getline(ss, bid_s, '\t');
        std::getline(ss, ask_s, '\t');

        try {
            Tick tick;
            tick.bid = std::stod(bid_s);
            tick.ask = std::stod(ask_s);
            ticks.push_back(tick);
        } catch (...) {}
    }

    return ticks;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "V6 MINIMAL VALIDATION TEST" << std::endl;
    std::cout << "========================================" << std::endl;

    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

    // Use smaller periods for faster testing
    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Jun 2025", 12000000, 100000},
        {"Dec Pre", 50000000, 200000},
        {"Dec Crash", 51314023, 200000}
    };

    std::cout << "\nLoading data and testing...\n" << std::endl;

    double v5_total = 0, v6_total = 0;
    double v5_max_dd = 0, v6_max_dd = 0;

    std::cout << std::left << std::setw(12) << "Period"
              << " | " << std::setw(18) << "V5 (TP 1.0x)"
              << " | " << std::setw(18) << "V6 (TP 2.0x)"
              << " | Delta" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (const auto& p : periods) {
        std::cout << "Loading " << p.name << "..." << std::flush;
        auto ticks = LoadTicks(filename, p.start, p.count);
        std::cout << " " << ticks.size() << " ticks" << std::endl;

        if (ticks.empty()) {
            std::cout << "  SKIPPED (no data)" << std::endl;
            continue;
        }

        // V5: SMA 11000, TP 1.0x
        Result r5 = RunTest(ticks, 11000, 1.0);

        // V6: SMA 11000, TP 2.0x
        Result r6 = RunTest(ticks, 11000, 2.0);

        v5_total += r5.return_pct;
        v6_total += r6.return_pct;
        v5_max_dd = std::max(v5_max_dd, r5.max_dd);
        v6_max_dd = std::max(v6_max_dd, r6.max_dd);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left << std::setw(12) << p.name
                  << " | " << std::right << std::setw(7) << r5.return_pct << "% DD" << std::setw(5) << r5.max_dd << "%"
                  << " | " << std::right << std::setw(7) << r6.return_pct << "% DD" << std::setw(5) << r6.max_dd << "%"
                  << " | " << (r6.return_pct > r5.return_pct ? "+" : "") << (r6.return_pct - r5.return_pct) << "%"
                  << std::endl;
    }

    std::cout << std::string(70, '-') << std::endl;
    std::cout << std::left << std::setw(12) << "TOTAL"
              << " | " << std::right << std::setw(7) << v5_total << "% DD" << std::setw(5) << v5_max_dd << "%"
              << " | " << std::right << std::setw(7) << v6_total << "% DD" << std::setw(5) << v6_max_dd << "%"
              << " | " << (v6_total > v5_total ? "+" : "") << (v6_total - v5_total) << "%"
              << std::endl;

    std::cout << "\n========================================" << std::endl;
    std::cout << "CONCLUSION: V6 (TP 2.0x) "
              << (v6_total > v5_total ? "OUTPERFORMS" : "UNDERPERFORMS")
              << " V5 by " << std::abs(v6_total - v5_total) << "%" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
