#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <set>

using namespace backtest;

struct InstrumentSpec {
    std::string name;
    double contract_size;
    double leverage;
    double lot_size;
    double spacing;
};

double RequiredFunds(const InstrumentSpec& spec, double price, double survive_pct) {
    double drop = price * survive_pct / 100.0;
    int N = (int)std::floor(drop / spec.spacing);
    if (N <= 0) return 0;

    double per_dollar = spec.lot_size * spec.contract_size;
    double floating_loss = per_dollar * spec.spacing * (double)N * (N + 1) / 2.0;

    double sum_prices = (double)N * price - spec.spacing * (double)N * (N - 1) / 2.0;
    double total_margin = spec.lot_size * spec.contract_size / spec.leverage * sum_prices;

    return floating_loss + 0.20 * total_margin;
}

struct StrategyResult {
    double survive_pct;
    double initial_deposit;
    double total_deposits;
    double total_withdrawals;
    double net_profit;
    int stop_out_count;
    int max_positions;
    int final_positions;
    double max_equity;
    double final_equity;
    double roi_on_deposits;
};

StrategyResult RunNeverClose(
    const InstrumentSpec& spec,
    const std::vector<Tick>& ticks,
    double survive_pct,
    const std::string& start_date,
    const std::string& end_date)
{
    StrategyResult result = {};
    result.survive_pct = survive_pct;

    double start_price = 0;
    for (const auto& t : ticks) {
        if (!start_date.empty() && t.timestamp < start_date) continue;
        start_price = t.ask;
        break;
    }
    if (start_price == 0) return result;

    double ath = start_price;
    double balance = RequiredFunds(spec, start_price, survive_pct);
    result.initial_deposit = balance;
    result.total_deposits = balance;
    double total_withdrawals = 0;

    struct Position {
        double entry_price;
    };
    std::vector<Position> positions;
    double per_dollar = spec.lot_size * spec.contract_size;

    // Track which $1 levels have been filled (to avoid duplicates)
    // Use integer cents to avoid floating point issues
    std::set<int> filled_levels;

    int stop_out_count = 0;
    int max_positions = 0;
    double max_equity = balance;

    for (const auto& tick : ticks) {
        if (!start_date.empty() && tick.timestamp < start_date) continue;
        if (!end_date.empty() && tick.timestamp >= end_date) break;

        double ask = tick.ask;
        double bid = tick.bid;

        // Update ATH
        if (bid > ath) {
            ath = bid;
        }

        // Calculate floating P&L
        double floating_pnl = 0;
        for (const auto& pos : positions) {
            floating_pnl += (bid - pos.entry_price) * per_dollar;
        }
        double equity = balance + floating_pnl;

        // Track max equity
        if (equity > max_equity) max_equity = equity;

        // Calculate margin
        double total_margin = 0;
        for (const auto& pos : positions) {
            total_margin += spec.lot_size * spec.contract_size / spec.leverage * pos.entry_price;
        }

        // Check broker stop-out: equity < 20% of margin
        if (!positions.empty() && total_margin > 0 && equity < 0.20 * total_margin) {
            // Broker closes everything
            balance = std::max(0.0, equity);
            positions.clear();
            filled_levels.clear();
            stop_out_count++;

            // Restart: deposit required funds at current price
            double new_deposit = RequiredFunds(spec, bid, survive_pct);
            result.total_deposits += new_deposit;
            balance = new_deposit;
            ath = bid;
            continue;
        }

        // Withdraw excess equity above required for current ATH
        double required = RequiredFunds(spec, ath, survive_pct);
        if (equity > required * 1.05 && floating_pnl >= 0) {
            // Only withdraw from balance (can't withdraw floating P&L directly)
            // Withdrawable = equity - required, but limited to balance
            double withdrawable = std::min(balance, equity - required);
            if (withdrawable > 0) {
                total_withdrawals += withdrawable;
                balance -= withdrawable;
            }
        }

        // Open new positions: fill levels within survive% below ATH
        // Only open at $1 levels we haven't filled yet
        double floor_price = ath * (1.0 - survive_pct / 100.0);

        // Check if current ask is at a new $1 level below ATH
        int current_level = (int)std::floor(ask / spec.spacing);
        int ath_level = (int)std::floor(ath / spec.spacing);
        int floor_level = (int)std::ceil(floor_price / spec.spacing);

        // Only open if price is below ATH and above floor
        if (current_level < ath_level && current_level >= floor_level) {
            if (filled_levels.find(current_level) == filled_levels.end()) {
                double entry = current_level * spec.spacing;
                positions.push_back({ask});
                filled_levels.insert(current_level);

                if ((int)positions.size() > max_positions) {
                    max_positions = (int)positions.size();
                }
            }
        }

        result.final_equity = equity;
    }

    // Final: close all positions at market (accounting)
    double final_floating = 0;
    for (const auto& pos : positions) {
        final_floating += (result.final_equity > 0 ?
            (ticks.back().bid - pos.entry_price) * per_dollar : 0);
    }
    // The final equity already includes floating P&L
    total_withdrawals += std::max(0.0, result.final_equity);

    result.total_withdrawals = total_withdrawals;
    result.net_profit = total_withdrawals - result.total_deposits;
    result.stop_out_count = stop_out_count;
    result.max_positions = max_positions;
    result.final_positions = (int)positions.size();
    result.max_equity = max_equity;
    result.roi_on_deposits = (result.total_deposits > 0) ?
        (result.net_profit / result.total_deposits * 100.0) : 0;

    return result;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " NEVER-CLOSE STRATEGY: Buy the dip, hold forever" << std::endl;
    std::cout << " Open within X% of ATH, never close, withdraw excess" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::string silver_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";
    std::string gold_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    auto load_ticks = [](const std::string& path) -> std::vector<Tick> {
        std::vector<Tick> ticks;
        std::ifstream file(path);
        if (!file.is_open()) return ticks;

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
        return ticks;
    };

    // ===== SILVER =====
    std::cout << "\nLoading XAGUSD ticks..." << std::endl;
    auto silver_ticks = load_ticks(silver_path);
    std::cout << "Loaded " << silver_ticks.size() << " ticks" << std::endl;

    InstrumentSpec silver = {"XAGUSD", 5000.0, 500.0, 0.01, 1.0};

    std::cout << "\n--- SILVER (XAGUSD, $1 spacing, 0.01 lot, NEVER CLOSE) ---" << std::endl;
    std::cout << std::setw(4) << "X%"
              << std::setw(10) << "InitDep"
              << std::setw(10) << "TotDep"
              << std::setw(10) << "TotWith"
              << std::setw(10) << "NetProf"
              << std::setw(8) << "ROI%"
              << std::setw(5) << "SO"
              << std::setw(7) << "MaxPos"
              << std::setw(7) << "EndPos"
              << std::setw(10) << "MaxEq" << std::endl;
    std::cout << std::string(81, '-') << std::endl;

    for (int X = 1; X <= 20; X++) {
        auto r = RunNeverClose(silver, silver_ticks, (double)X,
                               "2025.01.02", "2026.01.23");

        std::cout << std::setw(3) << X << "%"
                  << std::setw(9) << std::fixed << std::setprecision(0) << r.initial_deposit << "$"
                  << std::setw(9) << r.total_deposits << "$"
                  << std::setw(9) << r.total_withdrawals << "$"
                  << std::setw(9) << r.net_profit << "$"
                  << std::setw(7) << std::setprecision(0) << r.roi_on_deposits << "%"
                  << std::setw(5) << r.stop_out_count
                  << std::setw(7) << r.max_positions
                  << std::setw(7) << r.final_positions
                  << std::setw(9) << r.max_equity << "$"
                  << std::endl;
    }

    // ===== GOLD =====
    std::cout << "\nLoading XAUUSD ticks..." << std::endl;
    auto gold_ticks = load_ticks(gold_path);
    std::cout << "Loaded " << gold_ticks.size() << " ticks" << std::endl;

    InstrumentSpec gold = {"XAUUSD", 100.0, 500.0, 0.01, 1.0};

    std::cout << "\n--- GOLD (XAUUSD, $1 spacing, 0.01 lot, NEVER CLOSE) ---" << std::endl;
    std::cout << std::setw(4) << "X%"
              << std::setw(10) << "InitDep"
              << std::setw(12) << "TotDep"
              << std::setw(12) << "TotWith"
              << std::setw(12) << "NetProf"
              << std::setw(8) << "ROI%"
              << std::setw(5) << "SO"
              << std::setw(7) << "MaxPos"
              << std::setw(7) << "EndPos"
              << std::setw(12) << "MaxEq" << std::endl;
    std::cout << std::string(93, '-') << std::endl;

    for (int X = 1; X <= 20; X++) {
        auto r = RunNeverClose(gold, gold_ticks, (double)X,
                               "2025.01.01", "2025.12.29");

        std::cout << std::setw(3) << X << "%"
                  << std::setw(9) << std::fixed << std::setprecision(0) << r.initial_deposit << "$"
                  << std::setw(11) << r.total_deposits << "$"
                  << std::setw(11) << r.total_withdrawals << "$"
                  << std::setw(11) << r.net_profit << "$"
                  << std::setw(7) << std::setprecision(0) << r.roi_on_deposits << "%"
                  << std::setw(5) << r.stop_out_count
                  << std::setw(7) << r.max_positions
                  << std::setw(7) << r.final_positions
                  << std::setw(11) << r.max_equity << "$"
                  << std::endl;
    }

    std::cout << "\n--- INTERPRETATION ---" << std::endl;
    std::cout << "Strategy: Open 1 microlot at each $1 level within X% of ATH" << std::endl;
    std::cout << "Never close positions. Withdraw excess equity as ATH rises." << std::endl;
    std::cout << "Profit = accumulated floating P&L from price appreciation" << std::endl;
    std::cout << "MaxPos: Peak number of simultaneous open positions" << std::endl;
    std::cout << "EndPos: Positions still open at end of test" << std::endl;

    std::cout << "\nDone." << std::endl;
    return 0;
}
