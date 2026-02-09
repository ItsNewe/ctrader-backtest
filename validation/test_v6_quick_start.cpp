/**
 * V6 Quick Validation Test - Uses data from start of file
 * Avoids seeking issues with large file
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

        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

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

    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.trades++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    return r;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "V6 VALIDATION TEST" << std::endl;
    std::cout << "========================================" << std::endl;

    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    std::cout << "\nLoading first 500,000 ticks from file..." << std::endl;

    std::vector<Tick> ticks;
    ticks.reserve(500000);

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line); // skip header

    size_t count = 0;
    while (std::getline(file, line) && count < 500000) {
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
            count++;
        } catch (...) {}

        if (count % 100000 == 0) {
            std::cout << "  Loaded " << count << " ticks..." << std::endl;
        }
    }
    file.close();

    std::cout << "Loaded " << ticks.size() << " ticks total" << std::endl;

    if (ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return 1;
    }

    std::cout << "\nRunning tests with SMA period 1000 (scaled from 11000)..." << std::endl;
    std::cout << "(Using smaller SMA for this data subset)" << std::endl;

    // Test V5 baseline (TP 1.0x)
    std::cout << "\nV5 Baseline (TP 1.0x)..." << std::flush;
    Result r5 = RunTest(ticks, 1000, 1.0);
    std::cout << " Done" << std::endl;

    // Test V6 (TP 2.0x)
    std::cout << "V6 Optimal (TP 2.0x)..." << std::flush;
    Result r6 = RunTest(ticks, 1000, 2.0);
    std::cout << " Done" << std::endl;

    std::cout << "\n========================================" << std::endl;
    std::cout << "RESULTS" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(20) << "Configuration"
              << std::setw(12) << "Return"
              << std::setw(12) << "Max DD"
              << std::setw(10) << "Trades" << std::endl;
    std::cout << std::string(54, '-') << std::endl;

    std::cout << std::left << std::setw(20) << "V5 (TP 1.0x)"
              << std::right << std::setw(8) << r5.return_pct << "%   "
              << std::setw(8) << r5.max_dd << "%   "
              << std::setw(6) << r5.trades << std::endl;

    std::cout << std::left << std::setw(20) << "V6 (TP 2.0x)"
              << std::right << std::setw(8) << r6.return_pct << "%   "
              << std::setw(8) << r6.max_dd << "%   "
              << std::setw(6) << r6.trades << std::endl;

    std::cout << std::string(54, '-') << std::endl;

    double improvement = r6.return_pct - r5.return_pct;
    std::cout << "\nV6 vs V5 Delta: " << (improvement >= 0 ? "+" : "") << improvement << "%" << std::endl;

    std::cout << "\n========================================" << std::endl;
    if (r6.return_pct > r5.return_pct) {
        std::cout << "CONCLUSION: V6 (TP 2.0x) OUTPERFORMS V5" << std::endl;
    } else {
        std::cout << "CONCLUSION: V6 (TP 2.0x) UNDERPERFORMS V5" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return 0;
}
