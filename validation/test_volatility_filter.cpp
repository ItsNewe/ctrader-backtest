/**
 * Test: Volatility-based trading vs Trend-based trading
 *
 * Goal: Show that volatility filter is less directionally dependent
 * than trend filter (SMA)
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

// Simple ATR calculation
class ATR {
    std::deque<double> ranges;
    int period;
    double last_price = 0;
public:
    ATR(int p) : period(p) {}

    void Add(double price) {
        if (last_price > 0) {
            double range = std::abs(price - last_price);
            ranges.push_back(range);
            if ((int)ranges.size() > period) ranges.pop_front();
        }
        last_price = price;
    }

    double Get() const {
        if (ranges.empty()) return 0;
        double sum = 0;
        for (double r : ranges) sum += r;
        return sum / ranges.size();
    }

    bool IsReady() const { return (int)ranges.size() >= period; }
};

// Simple SMA
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

    double Get() const {
        if (prices.empty()) return 0;
        return sum / prices.size();
    }

    bool IsReady() const { return (int)prices.size() >= period; }
};

struct Result {
    double return_pct;
    double max_dd;
    int trades;
    int positions_opened;
};

enum FilterType {
    NONE,           // Original V1 - no filter
    SMA_FILTER,     // V5 style - only trade when price > SMA
    VOLATILITY_HI,  // Only trade when volatility is HIGH
    VOLATILITY_LO   // Only trade when volatility is LOW
};

Result RunTest(const std::vector<Tick>& ticks, FilterType filter, int filter_period = 1000) {
    Result r = {0, 0, 0, 0};
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

    SMA sma(filter_period);
    ATR atr_short(100);   // Short-term ATR
    ATR atr_long(500);    // Long-term ATR for comparison

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

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

        // V3 protection: Close ALL at 25% DD
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

        // V3 protection: Partial close at 8% DD
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
            case NONE:
                should_trade = true;
                break;
            case SMA_FILTER:
                should_trade = !sma.IsReady() || (tick.bid > sma.Get());
                break;
            case VOLATILITY_HI:
                if (atr_short.IsReady() && atr_long.IsReady()) {
                    should_trade = atr_short.Get() > atr_long.Get() * 1.2;  // 20% above average
                }
                break;
            case VOLATILITY_LO:
                if (atr_short.IsReady() && atr_long.IsReady()) {
                    should_trade = atr_short.Get() < atr_long.Get() * 0.8;  // 20% below average
                }
                break;
        }

        // Open new (with filter and DD < 5%)
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

    // Close remaining
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.trades++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    return r;
}

int main() {
    std::cout << "=============================================" << std::endl;
    std::cout << "FILTER COMPARISON: Trend vs Volatility" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << std::endl;

    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    std::cout << "Loading first 500,000 ticks..." << std::endl;

    std::vector<Tick> ticks;
    ticks.reserve(500000);

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line);

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
    }
    file.close();

    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    std::cout << "Price range: " << ticks.front().bid << " to " << ticks.back().bid << std::endl;
    std::cout << std::endl;

    std::cout << "Running tests..." << std::endl << std::endl;

    // Test all filter types
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(20) << "Filter"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Opened" << std::endl;
    std::cout << std::string(62, '-') << std::endl;

    struct TestCase {
        std::string name;
        FilterType filter;
        int period;
    };

    std::vector<TestCase> tests = {
        {"No Filter (V1)", NONE, 1000},
        {"SMA 1000 (V5)", SMA_FILTER, 1000},
        {"V7: Low Vol 0.8", VOLATILITY_LO, 1000},
        {"High Volatility", VOLATILITY_HI, 1000}
    };

    for (const auto& tc : tests) {
        Result r = RunTest(ticks, tc.filter, 1000);
        std::cout << std::left << std::setw(20) << tc.name
                  << std::right << std::setw(8) << r.return_pct << "%"
                  << std::setw(8) << r.max_dd << "%"
                  << std::setw(10) << r.trades
                  << std::setw(12) << r.positions_opened << std::endl;
    }

    std::cout << std::string(62, '-') << std::endl;

    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Key insight:" << std::endl;
    std::cout << "- SMA filter trades only in UPTRENDS (price > SMA)" << std::endl;
    std::cout << "- Volatility HI filter trades during HIGH VOLATILITY" << std::endl;
    std::cout << "- Volatility LO filter trades during LOW VOLATILITY" << std::endl;
    std::cout << std::endl;
    std::cout << "The volatility filter is more MARKET-NEUTRAL because:" << std::endl;
    std::cout << "- High volatility can occur in BOTH up and down markets" << std::endl;
    std::cout << "- It exploits the SIZE of moves, not the DIRECTION" << std::endl;
    std::cout << std::endl;
    std::cout << "To truly test market neutrality, we need data from:" << std::endl;
    std::cout << "- Uptrending periods (gold going up)" << std::endl;
    std::cout << "- Downtrending periods (gold going down)" << std::endl;
    std::cout << "- Ranging periods (gold going sideways)" << std::endl;
    std::cout << "=============================================" << std::endl;

    return 0;
}
