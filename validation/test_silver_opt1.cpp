#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <deque>
#include <algorithm>

using namespace backtest;

struct SilverResult {
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    double final_equity;
    double return_x;
    double max_dd_pct;
    int trades_opened;
    int trades_closed;
    int max_positions;
    bool stopped_out;
};

SilverResult RunFillUpAdaptive(
    const std::vector<Tick>& ticks,
    double survive_pct,
    double base_spacing,
    double lookback_hours,
    double initial_balance,
    const std::string& start_date,
    const std::string& end_date)
{
    SilverResult result = {};
    result.survive_pct = survive_pct;
    result.base_spacing = base_spacing;
    result.lookback_hours = lookback_hours;

    // Silver specs
    const double contract_size = 5000.0;
    const double leverage = 500.0;
    const double min_volume = 0.01;
    const double max_volume = 10.0;
    const double per_lot_per_dollar = contract_size; // $5000 per lot per $1 move

    double balance = initial_balance;
    double peak_equity = initial_balance;
    double max_dd_pct = 0;

    struct Position {
        double entry_price;
        double tp_price;
        double lots;
    };
    std::vector<Position> positions;

    // Volatility tracking
    double recent_high = 0, recent_low = DBL_MAX;
    long vol_reset_tick = 0;
    long lookback_ticks = 0; // Will calibrate from data
    double current_spacing = base_spacing;

    // Tick timing estimation
    long tick_count = 0;
    double first_price = 0;
    std::string first_time = "";

    int trades_opened = 0;
    int trades_closed = 0;
    int max_positions = 0;
    bool stopped_out = false;

    // Estimate ticks per hour from data (silver ~29M ticks over ~250 trading days × ~10h = 11,680 ticks/hour)
    // More accurate: we'll use a fixed estimate
    long ticks_per_hour = 12000; // Approximate for silver
    long lookback_tick_count = (long)(lookback_hours * ticks_per_hour);

    for (const auto& tick : ticks) {
        if (!start_date.empty() && tick.timestamp < start_date) continue;
        if (!end_date.empty() && tick.timestamp >= end_date) break;

        double ask = tick.ask;
        double bid = tick.bid;
        double spread = ask - bid;
        tick_count++;

        if (first_price == 0) {
            first_price = bid;
            recent_high = bid;
            recent_low = bid;
        }

        // Update volatility tracking (reset every lookback period)
        if (tick_count - vol_reset_tick >= lookback_tick_count) {
            recent_high = bid;
            recent_low = bid;
            vol_reset_tick = tick_count;
        }
        recent_high = std::max(recent_high, bid);
        recent_low = std::min(recent_low, bid);

        // Update adaptive spacing
        double range = recent_high - recent_low;
        if (range > 0) {
            double typical_vol = bid * 0.005; // 0.5% of price
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));
            current_spacing = base_spacing * vol_ratio;
            current_spacing = std::max(0.02, std::min(5.0, current_spacing));
        }

        // Check TPs
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            if (bid >= positions[i].tp_price) {
                double profit = (positions[i].tp_price - positions[i].entry_price) *
                               positions[i].lots * contract_size;
                balance += profit;
                trades_closed++;
                positions.erase(positions.begin() + i);
            }
        }

        // Calculate equity
        double floating_pnl = 0;
        for (const auto& pos : positions) {
            floating_pnl += (bid - pos.entry_price) * pos.lots * contract_size;
        }
        double equity = balance + floating_pnl;

        // Track drawdown
        if (equity > peak_equity) peak_equity = equity;
        if (peak_equity > 0) {
            double dd = (peak_equity - equity) / peak_equity * 100.0;
            if (dd > max_dd_pct) max_dd_pct = dd;
        }

        // Check margin stop-out (20% level)
        double total_margin = 0;
        for (const auto& pos : positions) {
            total_margin += pos.lots * contract_size * pos.entry_price / leverage;
        }
        if (!positions.empty() && total_margin > 0 && equity < 0.20 * total_margin) {
            // Stop-out: close all
            balance = std::max(0.0, equity);
            positions.clear();
            stopped_out = true;
            break;
        }

        // Determine if we should open
        bool should_open = false;
        if (positions.empty()) {
            should_open = true;
        } else {
            double lowest = DBL_MAX, highest = -DBL_MAX;
            for (const auto& pos : positions) {
                lowest = std::min(lowest, pos.entry_price);
                highest = std::max(highest, pos.entry_price);
            }
            if (ask <= lowest - current_spacing || ask >= highest + current_spacing) {
                should_open = true;
            }
        }

        if (should_open) {
            // Calculate lot size to survive X% drop
            double highest_entry = ask;
            double total_volume = 0;
            for (const auto& pos : positions) {
                highest_entry = std::max(highest_entry, pos.entry_price);
                total_volume += pos.lots;
            }

            double end_price = highest_entry * (1.0 - survive_pct / 100.0);
            double distance = ask - end_price;
            if (distance <= 0) continue;

            // Equity at worst case from existing positions
            double existing_loss = 0;
            double existing_margin = 0;
            for (const auto& pos : positions) {
                existing_loss += (pos.entry_price - end_price) * pos.lots * contract_size;
                existing_margin += pos.lots * contract_size * pos.entry_price / leverage;
            }
            double equity_at_bottom = equity - existing_loss - floating_pnl +
                                      (floating_pnl < 0 ? floating_pnl : 0);
            // Simpler: equity if price drops to end_price
            equity_at_bottom = balance;
            for (const auto& pos : positions) {
                equity_at_bottom += (end_price - pos.entry_price) * pos.lots * contract_size;
            }

            // New position loss at end_price
            double new_loss_per_lot = (ask - end_price) * contract_size;
            double new_margin_per_lot = contract_size * ask / leverage;

            // margin_so = 0.20
            // equity_at_bottom - lots * new_loss_per_lot >= 0.20 * (existing_margin + lots * new_margin_per_lot)
            double available = equity_at_bottom - 0.20 * existing_margin;
            double cost_per_lot = new_loss_per_lot + 0.20 * new_margin_per_lot;

            double lots = available / cost_per_lot;
            lots = std::floor(lots * 100.0) / 100.0; // Round to 0.01
            lots = std::max(min_volume, std::min(max_volume, lots));

            if (lots >= min_volume && available > cost_per_lot * min_volume) {
                double tp = ask + spread + current_spacing;
                positions.push_back({ask, tp, lots});
                trades_opened++;
                if ((int)positions.size() > max_positions) {
                    max_positions = (int)positions.size();
                }
            }
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
    result.final_equity = balance + final_floating;
    result.return_x = result.final_equity / initial_balance;
    result.max_dd_pct = max_dd_pct;
    result.trades_opened = trades_opened;
    result.trades_closed = trades_closed;
    result.max_positions = max_positions;
    result.stopped_out = stopped_out;

    return result;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " SILVER OPTIMIZATION #1: Survive% x Spacing (coarse)" << std::endl;
    std::cout << " Lookback fixed at 4h, Balance=$10000" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::string silver_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    std::cout << "\nLoading XAGUSD ticks..." << std::endl;
    std::vector<Tick> ticks;
    {
        std::ifstream file(silver_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open " << silver_path << std::endl;
            return 1;
        }
        std::string line;
        std::getline(file, line); // header
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
            tick.volume = 0;
            ticks.push_back(tick);
        }
    }
    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;

    double initial_balance = 10000.0;
    double lookback = 4.0;

    std::vector<double> survive_vals = {10, 15, 19, 25, 30, 35, 40, 50};
    std::vector<double> spacing_vals = {0.05, 0.10, 0.15, 0.20, 0.30, 0.50, 0.75, 1.00};

    std::cout << "\n" << std::setw(6) << "Surv%"
              << std::setw(8) << "Space"
              << std::setw(12) << "FinalEq"
              << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Opens"
              << std::setw(8) << "TPs"
              << std::setw(7) << "MaxP"
              << std::setw(6) << "SO?" << std::endl;
    std::cout << std::string(71, '-') << std::endl;

    for (double survive : survive_vals) {
        for (double spacing : spacing_vals) {
            auto r = RunFillUpAdaptive(ticks, survive, spacing, lookback,
                                        initial_balance, "2025.01.02", "2026.01.23");

            std::cout << std::setw(5) << std::fixed << std::setprecision(0) << survive << "%"
                      << std::setw(7) << std::setprecision(2) << spacing << "$"
                      << std::setw(11) << std::setprecision(0) << r.final_equity << "$"
                      << std::setw(7) << std::setprecision(1) << r.return_x << "x"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::setw(8) << r.trades_opened
                      << std::setw(8) << r.trades_closed
                      << std::setw(7) << r.max_positions
                      << std::setw(5) << (r.stopped_out ? "YES" : "no") << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
