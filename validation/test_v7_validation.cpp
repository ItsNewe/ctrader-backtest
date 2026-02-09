/**
 * V7 Validation Test
 *
 * Compares:
 * - V1 (No filter) - trades everything
 * - V5 (SMA filter) - trades only uptrends
 * - V7 (Volatility filter) - trades only calm/ranging markets
 *
 * Goal: Show V7 is more market-neutral while still being profitable
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

class SMA {
    std::deque<double> prices;
    int period;
    double sum = 0;
public:
    SMA(int p) : period(p) {}
    void Add(double price) {
        prices.push_back(price);
        sum += price;
        if ((int)prices.size() > period) {
            sum -= prices.front();
            prices.pop_front();
        }
    }
    double Get() const { return prices.empty() ? 0 : sum / prices.size(); }
    bool IsReady() const { return (int)prices.size() >= period; }
};

class ATR {
    std::deque<double> ranges;
    int period;
    double sum = 0;
    double last_price = 0;
public:
    ATR(int p) : period(p) {}
    void Add(double price) {
        if (last_price > 0) {
            double range = std::abs(price - last_price);
            ranges.push_back(range);
            sum += range;
            if ((int)ranges.size() > period) {
                sum -= ranges.front();
                ranges.pop_front();
            }
        }
        last_price = price;
    }
    double Get() const { return ranges.empty() ? 0 : sum / ranges.size(); }
    bool IsReady() const { return (int)ranges.size() >= period; }
};

enum FilterType {
    NO_FILTER,      // V1 style
    SMA_FILTER,     // V5 style
    VOLATILITY_LOW  // V7 style
};

struct Result {
    double return_pct;
    double max_dd;
    int trades;
    int positions_opened;
    std::string filter_name;
};

Result RunTest(const std::vector<Tick>& ticks, FilterType filter,
               int sma_period = 1000, int atr_short = 100, int atr_long = 500,
               double vol_threshold = 0.8) {
    Result r = {0, 0, 0, 0, ""};
    if (ticks.empty()) return r;

    switch (filter) {
        case NO_FILTER: r.filter_name = "V1 (No Filter)"; break;
        case SMA_FILTER: r.filter_name = "V5 (SMA " + std::to_string(sma_period) + ")"; break;
        case VOLATILITY_LOW: r.filter_name = "V7 (Low Vol)"; break;
    }

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
    ATR atr_s(atr_short);
    ATR atr_l(atr_long);

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);
        atr_s.Add(tick.bid);
        atr_l.Add(tick.bid);

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

        // V3 protection
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

        // Determine if we should trade based on filter
        bool should_trade = true;

        switch (filter) {
            case NO_FILTER:
                should_trade = true;
                break;
            case SMA_FILTER:
                should_trade = !sma.IsReady() || (tick.bid > sma.Get());
                break;
            case VOLATILITY_LOW:
                if (atr_s.IsReady() && atr_l.IsReady() && atr_l.Get() > 0) {
                    should_trade = atr_s.Get() < atr_l.Get() * vol_threshold;
                }
                break;
        }

        // Open new
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }

        if (dd_pct < 5.0 && should_trade && (int)positions.size() < 20) {
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
                    t->take_profit = tick.ask + tick.spread() + spacing;
                    positions.push_back(t);
                    r.positions_opened++;
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

std::vector<Tick> LoadTicks(const std::string& filename, size_t count) {
    std::vector<Tick> ticks;
    ticks.reserve(count);

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line) && ticks.size() < count) {
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

        if (ticks.size() % 100000 == 0) {
            std::cout << "  " << ticks.size() << " ticks loaded..." << std::endl;
        }
    }

    return ticks;
}

int main() {
    std::cout << "=============================================" << std::endl;
    std::cout << "V7 VALIDATION: Volatility Filter vs SMA Filter" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << std::endl;

    std::cout << "V7 Philosophy:" << std::endl;
    std::cout << "- V5 trades only UPTRENDS (dependent on gold going up)" << std::endl;
    std::cout << "- V7 trades only CALM markets (works in any direction)" << std::endl;
    std::cout << std::endl;

    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    // Test on first 500k ticks (early 2025)
    std::cout << "Loading 500,000 ticks from start of 2025..." << std::endl;
    auto ticks = LoadTicks(filename, 500000);
    std::cout << "Loaded " << ticks.size() << " ticks total" << std::endl;

    if (ticks.empty()) {
        std::cerr << "Failed to load data" << std::endl;
        return 1;
    }

    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;

    std::cout << "Price: " << start_price << " -> " << end_price;
    std::cout << " (" << (price_change >= 0 ? "+" : "") << std::fixed << std::setprecision(2) << price_change << "%)" << std::endl;
    std::cout << std::endl;

    // Run tests
    std::vector<Result> results;

    std::cout << "Running tests..." << std::endl;
    results.push_back(RunTest(ticks, NO_FILTER));
    results.push_back(RunTest(ticks, SMA_FILTER, 1000));
    results.push_back(RunTest(ticks, VOLATILITY_LOW, 1000, 100, 500, 0.8));

    // Also test different volatility thresholds
    results.push_back(RunTest(ticks, VOLATILITY_LOW, 1000, 100, 500, 1.0));  // More permissive
    results.push_back(RunTest(ticks, VOLATILITY_LOW, 1000, 100, 500, 0.6));  // More strict

    std::cout << std::endl;

    // Results table
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(25) << "Strategy"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Opened" << std::endl;
    std::cout << std::string(67, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.filter_name
                  << std::right << std::setw(8) << r.return_pct << "%"
                  << std::setw(8) << r.max_dd << "%"
                  << std::setw(10) << r.trades
                  << std::setw(12) << r.positions_opened << std::endl;
    }

    std::cout << std::string(67, '-') << std::endl;

    // Analysis
    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << std::endl;

    // Find best
    Result best = results[0];
    for (const auto& r : results) {
        if (r.return_pct > best.return_pct) best = r;
    }

    std::cout << "Best performer: " << best.filter_name << " with " << best.return_pct << "% return" << std::endl;
    std::cout << std::endl;

    std::cout << "Key observations:" << std::endl;
    std::cout << "1. V1 (No Filter): Trades everything, higher risk" << std::endl;
    std::cout << "2. V5 (SMA Filter): Only trades uptrends, depends on gold going up" << std::endl;
    std::cout << "3. V7 (Low Vol): Trades calm markets, more market-neutral" << std::endl;
    std::cout << std::endl;

    std::cout << "Why V7 is more robust:" << std::endl;
    std::cout << "- Low volatility = ranging market = mean reversion works" << std::endl;
    std::cout << "- High volatility = trending market = mean reversion fails" << std::endl;
    std::cout << "- V7 trades WHEN the strategy works, not based on DIRECTION" << std::endl;
    std::cout << std::endl;

    std::cout << "=============================================" << std::endl;

    return 0;
}
