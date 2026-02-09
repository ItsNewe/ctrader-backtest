#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <algorithm>

// Precise replication of FillUpAdaptive_v3.mq5 for XAGUSD
// Goal: Match MT5 result of $263,923.73 final balance (26.4x) from $10,000

struct Tick {
    std::string timestamp;
    double bid;
    double ask;
    long seconds; // parsed timestamp in seconds from epoch
    int day_of_year; // for day-change detection
    int day_of_week; // 0=Sun, 1=Mon, ..., 6=Sat (for triple swap)
};

struct Position {
    double entry_price;
    double tp_price;
    double lots;
};

struct BacktestResult {
    double final_equity;
    double final_balance;
    double max_dd_pct;
    int total_trades;
    int trades_closed;
    int max_positions;
    int spacing_changes;
    double total_swap;
    bool stopped_out;
};

// Parse "YYYY.MM.DD HH:MM:SS.mmm" to seconds since a reference point
long ParseTimestamp(const std::string& ts) {
    if (ts.size() < 19) return 0;
    int year = std::stoi(ts.substr(0, 4));
    int month = std::stoi(ts.substr(5, 2));
    int day = std::stoi(ts.substr(8, 2));
    int hour = std::stoi(ts.substr(11, 2));
    int minute = std::stoi(ts.substr(14, 2));
    int second = std::stoi(ts.substr(17, 2));

    // Days since reference (accurate for 2024-2026)
    long days = (year - 2020) * 365 + (year - 2020) / 4;
    int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    days += month_days[month - 1] + day;
    if (month > 2 && year % 4 == 0) days++;

    return days * 86400L + hour * 3600L + minute * 60L + second;
}

// Get day-of-year for day-change detection
int GetDayOfYear(const std::string& ts) {
    if (ts.size() < 10) return 0;
    int month = std::stoi(ts.substr(5, 2));
    int day = std::stoi(ts.substr(8, 2));
    int year = std::stoi(ts.substr(0, 4));
    int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int doy = month_days[month - 1] + day;
    if (month > 2 && year % 4 == 0) doy++;
    return (year - 2020) * 366 + doy; // unique day identifier
}

// Get day of week (Zeller's formula - 0=Sun, 1=Mon, ..., 6=Sat)
int GetDayOfWeek(const std::string& ts) {
    if (ts.size() < 10) return 0;
    int year = std::stoi(ts.substr(0, 4));
    int month = std::stoi(ts.substr(5, 2));
    int day = std::stoi(ts.substr(8, 2));

    if (month < 3) { month += 12; year--; }
    int k = year % 100;
    int j = year / 100;
    int dow = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    dow = ((dow + 7) % 7); // 0=Sat, 1=Sun, 2=Mon, ...
    // Convert to: 0=Sun, 1=Mon, ..., 6=Sat
    int standard_dow = (dow + 6) % 7;
    return standard_dow;
}

BacktestResult RunPreciseBacktest(
    const std::vector<Tick>& ticks,
    double swap_per_lot_per_day,  // Negative for longs (cost)
    bool verbose)
{
    // === Parameters from test.ini ===
    const double SurvivePct = 19.0;
    const double BaseSpacing = 0.75;
    const double MinVolume = 0.01;
    const double MaxVolume = 100.0;
    const double VolatilityLookbackHours = 1.0;
    const double TypicalVolPct = 0.5;
    const double MinSpacingMult = 0.5;
    const double MaxSpacingMult = 3.0;
    const double MinSpacingAbs = 0.02;
    const double MaxSpacingAbs = 5.0;

    // === Symbol specifications ===
    const double contract_size = 5000.0;
    const double leverage = 500.0;
    const double lot_step = 0.01;
    const double min_lot = 0.01;
    const double max_lot = 100.0;
    const double margin_stop_out_pct = 20.0;

    const double initial_balance = 10000.0;

    // === Strategy State ===
    double g_currentSpacing = BaseSpacing;
    double g_recentHigh = 0;
    double g_recentLow = DBL_MAX;
    long g_lastVolatilityReset = 0;
    int g_spacingChanges = 0;
    double g_typicalVol = 0;

    double balance = initial_balance;
    double peak_equity = initial_balance;
    double max_drawdown = 0;
    double max_drawdown_pct = 0;

    std::vector<Position> positions;
    int total_trades = 0;
    int trades_closed = 0;
    int max_positions = 0;
    bool stopped_out = false;
    double total_swap = 0;

    std::string start_date = "2025.01.02";
    std::string end_date = "2026.01.24";

    int lookback_seconds = (int)(VolatilityLookbackHours * 3600);

    int last_day = -1;
    long ticks_processed = 0;

    for (const auto& tick : ticks) {
        if (tick.timestamp < start_date) continue;
        if (tick.timestamp >= end_date) break;

        ticks_processed++;
        double ask = tick.ask;
        double bid = tick.bid;
        double spread = ask - bid;
        long current_time = tick.seconds;

        // === Swap Calculation (charged on day change, triple on Thursday) ===
        if (last_day >= 0 && tick.day_of_year != last_day && !positions.empty()) {
            // Day changed - charge swap for all open positions
            double total_lots = 0;
            for (const auto& pos : positions) {
                total_lots += pos.lots;
            }

            // Triple swap on Thursday (for Wednesday rollover)
            // MT5 typically uses swap_3days=3 (Wednesday), charged Thursday
            int swap_multiplier = 1;
            if (tick.day_of_week == 4) { // Thursday
                swap_multiplier = 3; // Triple swap (Sat+Sun+Wed)
            }

            double daily_swap = swap_per_lot_per_day * total_lots * swap_multiplier;
            balance += daily_swap; // swap_per_lot_per_day is negative for cost
            total_swap += daily_swap;
        }
        last_day = tick.day_of_year;

        // === UpdateVolatility() ===
        if (g_lastVolatilityReset == 0 || current_time - g_lastVolatilityReset >= lookback_seconds) {
            g_recentHigh = bid;
            g_recentLow = bid;
            g_lastVolatilityReset = current_time;
        }
        g_recentHigh = std::max(g_recentHigh, bid);
        g_recentLow = std::min(g_recentLow, bid);

        // === UpdateAdaptiveSpacing() ===
        double range = g_recentHigh - g_recentLow;
        if (range > 0 && g_recentHigh > 0 && bid > 0) {
            g_typicalVol = bid * (TypicalVolPct / 100.0);
            double volRatio = range / g_typicalVol;
            volRatio = std::max(MinSpacingMult, std::min(MaxSpacingMult, volRatio));
            double newSpacing = BaseSpacing * volRatio;
            newSpacing = std::max(MinSpacingAbs, std::min(MaxSpacingAbs, newSpacing));

            if (std::abs(newSpacing - g_currentSpacing) > 0.1) {
                g_currentSpacing = newSpacing;
                g_spacingChanges++;
            }
        }

        // === Check TPs ===
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            if (bid >= positions[i].tp_price) {
                double profit = (positions[i].tp_price - positions[i].entry_price) *
                               positions[i].lots * contract_size;
                balance += profit;
                trades_closed++;
                positions.erase(positions.begin() + i);
            }
        }

        // === Calculate equity ===
        double floating_pnl = 0;
        for (const auto& pos : positions) {
            floating_pnl += (bid - pos.entry_price) * pos.lots * contract_size;
        }
        double equity = balance + floating_pnl;

        // === UpdateStatistics() ===
        if (equity > peak_equity) peak_equity = equity;
        double drawdown = peak_equity - equity;
        double drawdown_pct = (peak_equity > 0) ? (drawdown / peak_equity * 100.0) : 0;
        if (drawdown > max_drawdown) {
            max_drawdown = drawdown;
            max_drawdown_pct = drawdown_pct;
        }

        // === Margin Stop-Out Check (20% level) ===
        double total_margin = 0;
        for (const auto& pos : positions) {
            total_margin += pos.lots * contract_size * pos.entry_price / leverage;
        }
        if (!positions.empty() && total_margin > 0 && (equity / total_margin * 100.0) < margin_stop_out_pct) {
            for (const auto& pos : positions) {
                double profit = (bid - pos.entry_price) * pos.lots * contract_size;
                balance += profit;
            }
            positions.clear();
            stopped_out = true;
            if (verbose) {
                std::cout << "!!! STOP-OUT at " << tick.timestamp << " Equity=$" << equity << std::endl;
            }
            break;
        }

        // === Entry Logic ===
        double lowestBuy = DBL_MAX;
        double highestBuy = -DBL_MAX;
        double totalVolume = 0;
        int positionCount = (int)positions.size();

        for (const auto& pos : positions) {
            lowestBuy = std::min(lowestBuy, pos.entry_price);
            highestBuy = std::max(highestBuy, pos.entry_price);
            totalVolume += pos.lots;
        }

        bool shouldOpen = false;
        if (positionCount == 0) {
            shouldOpen = true;
        } else if (lowestBuy >= ask + g_currentSpacing) {
            shouldOpen = true;
        } else if (highestBuy <= ask - g_currentSpacing) {
            shouldOpen = true;
        }

        if (shouldOpen) {
            // === CalculateLotSize() ===
            double usedMargin = 0;
            for (const auto& pos : positions) {
                usedMargin += pos.lots * contract_size * pos.entry_price / leverage;
            }

            double endPrice = (positionCount == 0)
                ? ask * ((100.0 - SurvivePct) / 100.0)
                : highestBuy * ((100.0 - SurvivePct) / 100.0);

            double distance = ask - endPrice;
            double numberOfTrades = std::floor(distance / g_currentSpacing);
            if (numberOfTrades <= 0) numberOfTrades = 1;

            double equityAtTarget = equity - totalVolume * distance * contract_size;

            if (usedMargin > 0 && (equityAtTarget / usedMargin * 100.0) < margin_stop_out_pct) {
                goto next_tick;
            }

            {
                double tradeSize = MinVolume;
                double dEquity = contract_size * tradeSize * g_currentSpacing *
                               (numberOfTrades * (numberOfTrades + 1) / 2.0);
                double dMargin = numberOfTrades * tradeSize * contract_size / leverage;

                double maxMult = MaxVolume / MinVolume;
                bool found = false;
                for (double mult = maxMult; mult >= 1.0; mult -= 0.1) {
                    double testEquity = equityAtTarget - mult * dEquity;
                    double testMargin = usedMargin + mult * dMargin;

                    if (testMargin > 0 && (testEquity / testMargin * 100.0) > margin_stop_out_pct) {
                        tradeSize = mult * MinVolume;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    double testEquity = equityAtTarget - 1.0 * dEquity;
                    double testMargin = usedMargin + 1.0 * dMargin;
                    if (testMargin <= 0 || (testEquity / testMargin * 100.0) <= margin_stop_out_pct) {
                        goto next_tick;
                    }
                    tradeSize = MinVolume;
                }

                tradeSize = std::floor(tradeSize / lot_step) * lot_step;
                tradeSize = std::max(min_lot, std::min(max_lot, tradeSize));
                tradeSize = std::min(tradeSize, MaxVolume);

                if (tradeSize >= min_lot) {
                    double tp = ask + spread + g_currentSpacing;
                    positions.push_back({ask, tp, tradeSize});
                    total_trades++;
                    if ((int)positions.size() > max_positions) {
                        max_positions = (int)positions.size();
                    }
                }
            }
        }

        next_tick:;

        if (verbose && ticks_processed % 5000000 == 0) {
            std::cout << "  Tick " << (ticks_processed / 1000000) << "M: " << tick.timestamp
                      << " Bal=$" << std::fixed << std::setprecision(0) << balance
                      << " Eq=$" << (balance + floating_pnl)
                      << " Pos=" << positions.size()
                      << " Trades=" << total_trades
                      << " Swap=$" << std::setprecision(0) << total_swap << std::endl;
        }
    }

    // Final equity
    double final_floating = 0;
    if (!ticks.empty()) {
        double last_bid = ticks.back().bid;
        for (const auto& pos : positions) {
            final_floating += (last_bid - pos.entry_price) * pos.lots * contract_size;
        }
    }

    BacktestResult result;
    result.final_equity = balance + final_floating;
    result.final_balance = balance;
    result.max_dd_pct = max_drawdown_pct;
    result.total_trades = total_trades;
    result.trades_closed = trades_closed;
    result.max_positions = max_positions;
    result.spacing_changes = g_spacingChanges;
    result.total_swap = total_swap;
    result.stopped_out = stopped_out;
    return result;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " PRECISE FillUpAdaptive_v3 Replication for XAGUSD" << std::endl;
    std::cout << " Matching MT5 result: $263,923.73 (26.4x from $10,000)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // === Load tick data ===
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    std::cout << "\nLoading XAGUSD ticks..." << std::endl;
    std::vector<Tick> ticks;
    {
        std::ifstream file(tick_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open " << tick_path << std::endl;
            return 1;
        }
        std::string line;
        std::getline(file, line); // skip header
        ticks.reserve(30000000);
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            Tick tick;
            size_t pos1 = line.find('\t');
            if (pos1 == std::string::npos) continue;
            tick.timestamp = line.substr(0, pos1);
            size_t pos2 = line.find('\t', pos1 + 1);
            if (pos2 == std::string::npos) continue;
            tick.bid = std::stod(line.substr(pos1 + 1, pos2 - pos1 - 1));
            size_t pos3 = line.find('\t', pos2 + 1);
            if (pos3 == std::string::npos) continue;
            tick.ask = std::stod(line.substr(pos2 + 1, pos3 - pos2 - 1));
            tick.seconds = ParseTimestamp(tick.timestamp);
            tick.day_of_year = GetDayOfYear(tick.timestamp);
            tick.day_of_week = GetDayOfWeek(tick.timestamp);
            ticks.push_back(tick);
        }
    }
    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    if (!ticks.empty()) {
        std::cout << "First: " << ticks.front().timestamp
                  << " Bid=" << ticks.front().bid << " Ask=" << ticks.front().ask << std::endl;
        std::cout << "Last:  " << ticks.back().timestamp
                  << " Bid=" << ticks.back().bid << " Ask=" << ticks.back().ask << std::endl;
    }

    // === Run with no swap first (baseline) ===
    std::cout << "\n=== BASELINE (No Swap) ===" << std::endl;
    auto baseline = RunPreciseBacktest(ticks, 0.0, true);
    std::cout << "\nResult: Equity=$" << std::fixed << std::setprecision(0) << baseline.final_equity
              << " (" << std::setprecision(2) << (baseline.final_equity / 10000.0) << "x)"
              << " MaxDD=" << std::setprecision(1) << baseline.max_dd_pct << "%"
              << " Trades=" << baseline.total_trades
              << " SpacingChanges=" << baseline.spacing_changes << std::endl;

    // === Sweep swap rates to find matching value ===
    std::cout << "\n================================================================" << std::endl;
    std::cout << " SWAP RATE SWEEP (finding value that matches MT5)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << " MT5 Target: $263,924 equity (26.4x)" << std::endl;
    std::cout << std::endl;

    std::cout << std::setw(12) << "Swap/lot/d"
              << std::setw(12) << "Equity"
              << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "TotSwap"
              << std::setw(8) << "vs MT5"
              << std::setw(6) << "SO?" << std::endl;
    std::cout << std::string(72, '-') << std::endl;

    // Try different swap rates (negative = cost for longs)
    // For silver: typical swap_long is around -$5 to -$50 per lot per day
    std::vector<double> swap_rates = {0, -5, -10, -15, -20, -25, -30, -40, -50, -75, -100};

    double best_match = 999;
    double best_swap_rate = 0;

    for (double swap_rate : swap_rates) {
        auto r = RunPreciseBacktest(ticks, swap_rate, false);

        double ratio = r.final_equity / 263923.73;
        double match_pct = std::abs(ratio - 1.0) * 100.0;

        std::cout << std::setw(10) << std::fixed << std::setprecision(1) << swap_rate << "$/d"
                  << std::setw(11) << std::setprecision(0) << r.final_equity << "$"
                  << std::setw(7) << std::setprecision(1) << (r.final_equity / 10000.0) << "x"
                  << std::setw(7) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(9) << std::setprecision(0) << r.total_swap << "$"
                  << std::setw(7) << std::setprecision(2) << ratio << "x"
                  << std::setw(5) << (r.stopped_out ? "YES" : "no") << std::endl;

        if (match_pct < best_match && !r.stopped_out) {
            best_match = match_pct;
            best_swap_rate = swap_rate;
        }
    }

    std::cout << "\nBest match: swap_rate=$" << std::setprecision(1) << best_swap_rate
              << "/lot/day (within " << std::setprecision(1) << best_match << "% of MT5)" << std::endl;

    // === Run detailed backtest with best swap rate ===
    if (best_swap_rate != 0) {
        std::cout << "\n================================================================" << std::endl;
        std::cout << " DETAILED RUN WITH SWAP=$" << best_swap_rate << "/lot/day" << std::endl;
        std::cout << "================================================================" << std::endl;
        auto detailed = RunPreciseBacktest(ticks, best_swap_rate, true);

        std::cout << "\n=== FINAL COMPARISON ===" << std::endl;
        std::cout << std::setw(25) << "Metric" << std::setw(15) << "C++ Backtest" << std::setw(15) << "MT5" << std::setw(10) << "Ratio" << std::endl;
        std::cout << std::string(65, '-') << std::endl;
        std::cout << std::setw(25) << "Final Equity" << std::setw(14) << std::setprecision(0) << detailed.final_equity << "$"
                  << std::setw(14) << 263924 << "$"
                  << std::setw(9) << std::setprecision(2) << (detailed.final_equity / 263924.0) << "x" << std::endl;
        std::cout << std::setw(25) << "Return" << std::setw(13) << std::setprecision(1) << (detailed.final_equity / 10000.0) << "x"
                  << std::setw(14) << "26.4x"
                  << std::setw(9) << std::setprecision(2) << ((detailed.final_equity / 10000.0) / 26.4) << "x" << std::endl;
        std::cout << std::setw(25) << "Max DD %" << std::setw(13) << std::setprecision(1) << detailed.max_dd_pct << "%"
                  << std::setw(14) << "68.6%"
                  << std::setw(9) << std::setprecision(2) << (detailed.max_dd_pct / 68.6) << "x" << std::endl;
        std::cout << std::setw(25) << "Total Trades" << std::setw(14) << detailed.total_trades
                  << std::setw(14) << 522
                  << std::setw(9) << std::setprecision(1) << ((double)detailed.total_trades / 522.0) << "x" << std::endl;
        std::cout << std::setw(25) << "Spacing Changes" << std::setw(14) << detailed.spacing_changes
                  << std::setw(14) << 28931
                  << std::setw(9) << std::setprecision(1) << ((double)detailed.spacing_changes / 28931.0) << "x" << std::endl;
        std::cout << std::setw(25) << "Total Swap" << std::setw(13) << std::setprecision(0) << detailed.total_swap << "$"
                  << std::setw(14) << "unknown"
                  << std::setw(9) << "" << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
