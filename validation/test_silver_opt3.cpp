#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <algorithm>

using namespace backtest;

struct SilverResult {
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    std::string period;
    double final_equity;
    double return_x;
    double max_dd_pct;
    int trades_opened;
    int trades_closed;
    int max_positions;
    bool stopped_out;
    bool adaptive; // true=adaptive, false=fixed spacing
};

SilverResult RunFillUpSilver(
    const std::vector<Tick>& ticks,
    double survive_pct,
    double base_spacing,
    double lookback_hours,
    double initial_balance,
    const std::string& start_date,
    const std::string& end_date,
    bool use_adaptive,
    const std::string& period_label)
{
    SilverResult result = {};
    result.survive_pct = survive_pct;
    result.base_spacing = base_spacing;
    result.lookback_hours = lookback_hours;
    result.period = period_label;
    result.adaptive = use_adaptive;

    const double contract_size = 5000.0;
    const double leverage = 500.0;
    const double min_volume = 0.01;
    const double max_volume = 10.0;

    double balance = initial_balance;
    double peak_equity = initial_balance;
    double max_dd_pct = 0;

    struct Position {
        double entry_price;
        double tp_price;
        double lots;
    };
    std::vector<Position> positions;

    double recent_high = 0, recent_low = DBL_MAX;
    long vol_reset_tick = 0;
    double current_spacing = base_spacing;
    long tick_count = 0;

    long ticks_per_hour = 12000;
    long lookback_tick_count = (long)(lookback_hours * ticks_per_hour);

    int trades_opened = 0;
    int trades_closed = 0;
    int max_positions = 0;
    bool stopped_out = false;

    for (const auto& tick : ticks) {
        if (!start_date.empty() && tick.timestamp < start_date) continue;
        if (!end_date.empty() && tick.timestamp >= end_date) break;

        double ask = tick.ask;
        double bid = tick.bid;
        double spread = ask - bid;
        tick_count++;

        if (tick_count == 1) {
            recent_high = bid;
            recent_low = bid;
        }

        // Volatility tracking (even for fixed, to compare)
        if (tick_count - vol_reset_tick >= lookback_tick_count) {
            recent_high = bid;
            recent_low = bid;
            vol_reset_tick = tick_count;
        }
        recent_high = std::max(recent_high, bid);
        recent_low = std::min(recent_low, bid);

        // Adaptive or fixed spacing
        if (use_adaptive) {
            double range = recent_high - recent_low;
            if (range > 0) {
                double typical_vol = bid * 0.005;
                double vol_ratio = range / typical_vol;
                vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));
                current_spacing = base_spacing * vol_ratio;
                current_spacing = std::max(0.02, std::min(5.0, current_spacing));
            }
        } else {
            current_spacing = base_spacing; // Fixed
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

        if (equity > peak_equity) peak_equity = equity;
        if (peak_equity > 0) {
            double dd = (peak_equity - equity) / peak_equity * 100.0;
            if (dd > max_dd_pct) max_dd_pct = dd;
        }

        // Margin stop-out
        double total_margin = 0;
        for (const auto& pos : positions) {
            total_margin += pos.lots * contract_size * pos.entry_price / leverage;
        }
        if (!positions.empty() && total_margin > 0 && equity < 0.20 * total_margin) {
            balance = std::max(0.0, equity);
            positions.clear();
            stopped_out = true;
            break;
        }

        // Open new position
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
            double highest_entry = ask;
            for (const auto& pos : positions) {
                highest_entry = std::max(highest_entry, pos.entry_price);
            }

            double end_price = highest_entry * (1.0 - survive_pct / 100.0);
            double distance = ask - end_price;
            if (distance <= 0) continue;

            double equity_at_bottom = balance;
            double existing_margin = 0;
            for (const auto& pos : positions) {
                equity_at_bottom += (end_price - pos.entry_price) * pos.lots * contract_size;
                existing_margin += pos.lots * contract_size * pos.entry_price / leverage;
            }

            double new_loss_per_lot = (ask - end_price) * contract_size;
            double new_margin_per_lot = contract_size * ask / leverage;

            double available = equity_at_bottom - 0.20 * existing_margin;
            double cost_per_lot = new_loss_per_lot + 0.20 * new_margin_per_lot;

            if (cost_per_lot <= 0) continue;
            double lots = available / cost_per_lot;
            lots = std::floor(lots * 100.0) / 100.0;
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
    std::cout << " SILVER OPTIMIZATION #3: Fixed vs Adaptive + H1/H2" << std::endl;
    std::cout << " Balance=$10000" << std::endl;
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
        std::getline(file, line);
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

    // Part 1: Fixed vs Adaptive comparison
    std::cout << "\n============ PART 1: Fixed vs Adaptive Spacing ============" << std::endl;
    std::cout << std::setw(6) << "Surv%"
              << std::setw(8) << "Space"
              << std::setw(7) << "Mode"
              << std::setw(12) << "FinalEq"
              << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Opens"
              << std::setw(8) << "TPs"
              << std::setw(6) << "SO?" << std::endl;
    std::cout << std::string(71, '-') << std::endl;

    std::vector<double> survive_vals = {19, 25, 30, 40};
    std::vector<double> spacing_vals = {0.10, 0.20, 0.30, 0.50, 1.00};

    for (double survive : survive_vals) {
        for (double spacing : spacing_vals) {
            // Fixed
            auto rf = RunFillUpSilver(ticks, survive, spacing, 4.0,
                                       initial_balance, "2025.01.02", "2026.01.23",
                                       false, "Full");
            // Adaptive
            auto ra = RunFillUpSilver(ticks, survive, spacing, 4.0,
                                       initial_balance, "2025.01.02", "2026.01.23",
                                       true, "Full");

            std::cout << std::setw(5) << std::fixed << std::setprecision(0) << survive << "%"
                      << std::setw(7) << std::setprecision(2) << spacing << "$"
                      << "  FIX"
                      << std::setw(11) << std::setprecision(0) << rf.final_equity << "$"
                      << std::setw(7) << std::setprecision(1) << rf.return_x << "x"
                      << std::setw(7) << rf.max_dd_pct << "%"
                      << std::setw(8) << rf.trades_opened
                      << std::setw(8) << rf.trades_closed
                      << std::setw(5) << (rf.stopped_out ? "YES" : "no") << std::endl;

            std::cout << std::setw(5) << "" << " "
                      << std::setw(7) << "" << " "
                      << " ADP"
                      << std::setw(11) << std::setprecision(0) << ra.final_equity << "$"
                      << std::setw(7) << std::setprecision(1) << ra.return_x << "x"
                      << std::setw(7) << ra.max_dd_pct << "%"
                      << std::setw(8) << ra.trades_opened
                      << std::setw(8) << ra.trades_closed
                      << std::setw(5) << (ra.stopped_out ? "YES" : "no") << std::endl;
        }
        std::cout << std::endl;
    }

    // Part 2: H1/H2 validation for promising configs
    std::cout << "\n============ PART 2: H1/H2 Out-of-Sample Validation ============" << std::endl;
    std::cout << std::setw(6) << "Surv%"
              << std::setw(8) << "Space"
              << std::setw(7) << "Mode"
              << std::setw(8) << "Period"
              << std::setw(10) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Opens"
              << std::setw(8) << "TPs"
              << std::setw(6) << "SO?" << std::endl;
    std::cout << std::string(69, '-') << std::endl;

    struct TestConfig {
        double survive;
        double spacing;
        bool adaptive;
    };

    std::vector<TestConfig> configs = {
        {19, 0.10, true}, {19, 0.20, true}, {19, 0.30, true}, {19, 0.50, true},
        {25, 0.10, true}, {25, 0.20, true}, {25, 0.30, true}, {25, 0.50, true},
        {30, 0.10, true}, {30, 0.20, true}, {30, 0.30, true}, {30, 0.50, true},
        {19, 0.10, false}, {19, 0.20, false}, {19, 0.30, false},
        {25, 0.10, false}, {25, 0.20, false}, {25, 0.30, false},
        {30, 0.10, false}, {30, 0.20, false}, {30, 0.30, false},
    };

    // H1: Jan-Jun, H2: Jul-Jan
    std::string h1_start = "2025.01.02";
    std::string h1_end = "2025.07.01";
    std::string h2_start = "2025.07.01";
    std::string h2_end = "2026.01.23";

    for (const auto& cfg : configs) {
        auto rh1 = RunFillUpSilver(ticks, cfg.survive, cfg.spacing, 4.0,
                                    initial_balance, h1_start, h1_end,
                                    cfg.adaptive, "H1");
        auto rh2 = RunFillUpSilver(ticks, cfg.survive, cfg.spacing, 4.0,
                                    initial_balance, h2_start, h2_end,
                                    cfg.adaptive, "H2");

        std::string mode = cfg.adaptive ? "ADP" : "FIX";

        std::cout << std::setw(5) << std::fixed << std::setprecision(0) << cfg.survive << "%"
                  << std::setw(7) << std::setprecision(2) << cfg.spacing << "$"
                  << std::setw(6) << mode
                  << "   H1"
                  << std::setw(9) << std::setprecision(1) << rh1.return_x << "x"
                  << std::setw(7) << rh1.max_dd_pct << "%"
                  << std::setw(8) << rh1.trades_opened
                  << std::setw(8) << rh1.trades_closed
                  << std::setw(5) << (rh1.stopped_out ? "YES" : "no") << std::endl;

        std::cout << std::setw(5) << "" << " "
                  << std::setw(7) << "" << " "
                  << std::setw(6) << ""
                  << "   H2"
                  << std::setw(9) << std::setprecision(1) << rh2.return_x << "x"
                  << std::setw(7) << rh2.max_dd_pct << "%"
                  << std::setw(8) << rh2.trades_opened
                  << std::setw(8) << rh2.trades_closed
                  << std::setw(5) << (rh2.stopped_out ? "YES" : "no") << std::endl;

        // H1/H2 ratio
        if (!rh1.stopped_out && !rh2.stopped_out && rh1.return_x > 0) {
            double ratio = rh2.return_x / rh1.return_x;
            std::cout << std::setw(5) << "" << " "
                      << std::setw(7) << "" << " "
                      << std::setw(6) << ""
                      << " ratio"
                      << std::setw(8) << std::setprecision(2) << ratio << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
