#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <fstream>

using namespace backtest;

// Calculate required funds for X% survive at a given price
// with 1 microlot per $1 spacing
struct InstrumentSpec {
    std::string name;
    double contract_size;
    double leverage;
    double lot_size;      // 0.01 = microlot
    double spacing;       // $1
};

double RequiredFunds(const InstrumentSpec& spec, double price, double survive_pct) {
    double drop = price * survive_pct / 100.0;
    int N = (int)std::floor(drop / spec.spacing);
    if (N <= 0) return 0;

    // Floating loss at bottom
    double per_dollar = spec.lot_size * spec.contract_size;
    double floating_loss = per_dollar * spec.spacing * (double)N * (N + 1) / 2.0;

    // Margin (CFD_LEVERAGE mode)
    double sum_prices = (double)N * price - spec.spacing * (double)N * (N - 1) / 2.0;
    double total_margin = spec.lot_size * spec.contract_size / spec.leverage * sum_prices;

    return floating_loss + 0.20 * total_margin;
}

struct StrategyResult {
    double survive_pct;
    double initial_deposit;
    double total_deposits;    // Sum of all deposits (including initial)
    double total_withdrawals;
    double net_profit;        // withdrawals - deposits (excl initial)
    int stop_out_count;
    int tp_count;
    double roi_on_deposits;   // net_profit / total_deposits
    double max_ath;
    double final_price;
};

StrategyResult RunWithdrawStrategy(
    const InstrumentSpec& spec,
    const std::vector<Tick>& ticks,
    double survive_pct,
    const std::string& start_date,
    const std::string& end_date)
{
    StrategyResult result = {};
    result.survive_pct = survive_pct;

    // Find starting price
    double start_price = 0;
    for (const auto& t : ticks) {
        if (!start_date.empty() && t.timestamp < start_date) continue;
        start_price = t.ask;
        break;
    }
    if (start_price == 0) return result;

    // Strategy state
    double ath = start_price;
    double balance = RequiredFunds(spec, start_price, survive_pct);
    result.initial_deposit = balance;
    result.total_deposits = balance;
    double total_withdrawals = 0;

    // Position tracking
    struct Position {
        double entry_price;
        double tp_price;
    };
    std::vector<Position> positions;
    double lowest_entry = DBL_MAX;
    double per_dollar = spec.lot_size * spec.contract_size;
    double spread = 0;
    int tp_count = 0;
    int stop_out_count = 0;

    for (const auto& tick : ticks) {
        if (!start_date.empty() && tick.timestamp < start_date) continue;
        if (!end_date.empty() && tick.timestamp >= end_date) break;

        spread = tick.ask - tick.bid;
        double ask = tick.ask;
        double bid = tick.bid;

        // Check TPs (price went up, bid hits TP)
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            if (bid >= positions[i].tp_price) {
                // TP hit - profit = (tp_price - entry_price) * per_dollar - spread_cost
                double profit = (positions[i].tp_price - positions[i].entry_price) * per_dollar;
                balance += profit;
                tp_count++;
                positions.erase(positions.begin() + i);
            }
        }

        // Update ATH
        if (bid > ath) {
            ath = bid;
        }

        // Check if we need to withdraw or if we CAN withdraw
        // Required = enough to survive X% from current ATH with current positions
        // But we simplify: required = RequiredFunds(spec, ath, survive_pct)
        // This is the amount needed if all positions were fresh from ATH
        // With existing underwater positions, we actually need MORE
        // For safety: required = RequiredFunds from ATH (covers worst case of re-entering from ATH)
        double required = RequiredFunds(spec, ath, survive_pct);

        // Account for existing position floating P&L
        double floating_pnl = 0;
        for (const auto& pos : positions) {
            floating_pnl += (bid - pos.entry_price) * per_dollar;
        }
        double equity = balance + floating_pnl;

        // Withdraw excess (equity above required)
        if (positions.empty() && balance > required * 1.01) {
            double withdrawal = balance - required;
            total_withdrawals += withdrawal;
            balance = required;
        }

        // Check stop-out: equity drops to X% below ATH entry zone
        double stop_out_price = ath * (1.0 - survive_pct / 100.0);
        if (bid <= stop_out_price && !positions.empty()) {
            // Stop out - lose everything (balance goes to ~0 from floating loss)
            // Close all positions at current bid
            double total_loss = 0;
            for (const auto& pos : positions) {
                total_loss += (pos.entry_price - bid) * per_dollar;
            }
            balance = std::max(0.0, balance - total_loss);
            positions.clear();
            lowest_entry = DBL_MAX;
            stop_out_count++;

            // Restart: deposit fresh capital for X% survive at current price
            double new_deposit = RequiredFunds(spec, bid, survive_pct);
            result.total_deposits += new_deposit;
            balance = new_deposit;
            ath = bid;  // Reset ATH to current price after restart
            continue;
        }

        // Open new positions (fill-up downward from ATH)
        if (positions.empty()) {
            // First entry at current ask
            double tp = ask + spread + spec.spacing;
            positions.push_back({ask, tp});
            lowest_entry = ask;
        } else {
            // Open below lowest if spacing met
            if (lowest_entry > ask + spec.spacing) {
                double tp = ask + spread + spec.spacing;
                positions.push_back({ask, tp});
                lowest_entry = ask;
            }
        }

        result.final_price = bid;
    }

    // Close remaining positions at market (final accounting)
    double final_floating = 0;
    for (const auto& pos : positions) {
        final_floating += (result.final_price - pos.entry_price) * per_dollar;
    }
    balance += final_floating;

    // Final withdrawal of remaining balance
    total_withdrawals += std::max(0.0, balance);

    result.total_withdrawals = total_withdrawals;
    result.net_profit = total_withdrawals - result.total_deposits;
    result.stop_out_count = stop_out_count;
    result.tp_count = tp_count;
    result.roi_on_deposits = (result.total_deposits > 0) ?
        (result.net_profit / result.total_deposits * 100.0) : 0;
    result.max_ath = ath;

    return result;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " WITHDRAW STRATEGY: Fixed lot, take profits out" << std::endl;
    std::cout << " Deposit required(X%) at start, withdraw excess," << std::endl;
    std::cout << " if stopped out deposit required(X%) at new price." << std::endl;
    std::cout << "========================================================" << std::endl;

    // Load XAGUSD ticks
    std::string silver_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";
    std::string gold_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    auto load_ticks = [](const std::string& path) -> std::vector<Tick> {
        std::vector<Tick> ticks;
        std::ifstream file(path);
        if (!file.is_open()) return ticks;

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
        return ticks;
    };

    // Silver
    std::cout << "\nLoading XAGUSD ticks..." << std::endl;
    auto silver_ticks = load_ticks(silver_path);
    std::cout << "Loaded " << silver_ticks.size() << " ticks" << std::endl;

    InstrumentSpec silver = {"XAGUSD", 5000.0, 500.0, 0.01, 1.0};

    std::cout << "\n--- SILVER (XAGUSD, $1 spacing, 0.01 lot) ---" << std::endl;
    std::cout << std::setw(4) << "X%"
              << std::setw(10) << "InitDep"
              << std::setw(10) << "TotDep"
              << std::setw(10) << "TotWith"
              << std::setw(10) << "NetProf"
              << std::setw(8) << "ROI%"
              << std::setw(6) << "StopO"
              << std::setw(7) << "TPs"
              << std::setw(11) << "Profit/SO" << std::endl;
    std::cout << std::string(76, '-') << std::endl;

    for (int X = 1; X <= 20; X++) {
        auto r = RunWithdrawStrategy(silver, silver_ticks, (double)X,
                                     "2025.01.02", "2026.01.23");
        double profit_per_stopout = (r.stop_out_count > 0) ?
            r.net_profit / r.stop_out_count : r.net_profit;

        std::cout << std::setw(3) << X << "%"
                  << std::setw(9) << std::fixed << std::setprecision(0) << r.initial_deposit << "$"
                  << std::setw(9) << r.total_deposits << "$"
                  << std::setw(9) << r.total_withdrawals << "$"
                  << std::setw(9) << r.net_profit << "$"
                  << std::setw(7) << std::setprecision(0) << r.roi_on_deposits << "%"
                  << std::setw(5) << r.stop_out_count
                  << std::setw(7) << r.tp_count
                  << std::setw(9) << std::setprecision(0) << profit_per_stopout << "$"
                  << std::endl;
    }

    // Gold
    std::cout << "\nLoading XAUUSD ticks..." << std::endl;
    auto gold_ticks = load_ticks(gold_path);
    std::cout << "Loaded " << gold_ticks.size() << " ticks" << std::endl;

    InstrumentSpec gold = {"XAUUSD", 100.0, 500.0, 0.01, 1.0};

    std::cout << "\n--- GOLD (XAUUSD, $1 spacing, 0.01 lot) ---" << std::endl;
    std::cout << std::setw(4) << "X%"
              << std::setw(10) << "InitDep"
              << std::setw(10) << "TotDep"
              << std::setw(10) << "TotWith"
              << std::setw(10) << "NetProf"
              << std::setw(8) << "ROI%"
              << std::setw(6) << "StopO"
              << std::setw(7) << "TPs"
              << std::setw(11) << "Profit/SO" << std::endl;
    std::cout << std::string(76, '-') << std::endl;

    for (int X = 1; X <= 20; X++) {
        auto r = RunWithdrawStrategy(gold, gold_ticks, (double)X,
                                     "2025.01.01", "2025.12.29");
        double profit_per_stopout = (r.stop_out_count > 0) ?
            r.net_profit / r.stop_out_count : r.net_profit;

        std::cout << std::setw(3) << X << "%"
                  << std::setw(9) << std::fixed << std::setprecision(0) << r.initial_deposit << "$"
                  << std::setw(9) << r.total_deposits << "$"
                  << std::setw(9) << r.total_withdrawals << "$"
                  << std::setw(9) << r.net_profit << "$"
                  << std::setw(7) << std::setprecision(0) << r.roi_on_deposits << "%"
                  << std::setw(5) << r.stop_out_count
                  << std::setw(7) << r.tp_count
                  << std::setw(9) << std::setprecision(0) << profit_per_stopout << "$"
                  << std::endl;
    }

    std::cout << "\n--- INTERPRETATION ---" << std::endl;
    std::cout << "NetProf > 0: Strategy is profitable after all stop-outs" << std::endl;
    std::cout << "ROI%: Net profit as % of total capital deployed" << std::endl;
    std::cout << "Profit/SO: How much net profit per stop-out event" << std::endl;
    std::cout << "  (If negative: each stop-out costs more than TPs earned)" << std::endl;

    std::cout << "\nDone." << std::endl;
    return 0;
}
