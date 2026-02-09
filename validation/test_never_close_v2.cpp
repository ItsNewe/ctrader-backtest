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

struct StrategyResult {
    double survive_pct;
    double initial_deposit;
    double total_deposits;
    double total_withdrawals;
    double net_profit;
    int stop_out_count;
    int max_positions;
    int final_positions;
    double peak_equity;
    double final_equity;
    double roi_on_deposits;
    double max_dd_pct;       // Worst equity drawdown %
};

StrategyResult RunNeverCloseV2(
    const InstrumentSpec& spec,
    const std::vector<Tick>& ticks,
    double survive_pct,        // X% equity drawdown triggers stop-out
    double initial_deposit,    // Fixed starting capital
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
    double balance = initial_deposit;
    result.initial_deposit = initial_deposit;
    result.total_deposits = initial_deposit;
    double total_withdrawals = 0;

    struct Position {
        double entry_price;
    };
    std::vector<Position> positions;
    double per_dollar = spec.lot_size * spec.contract_size;

    // Track filled levels (avoid duplicates)
    std::set<int> filled_levels;

    int stop_out_count = 0;
    int max_positions = 0;
    double peak_equity = initial_deposit;
    double max_dd_pct = 0;

    for (const auto& tick : ticks) {
        if (!start_date.empty() && tick.timestamp < start_date) continue;
        if (!end_date.empty() && tick.timestamp >= end_date) break;

        double ask = tick.ask;
        double bid = tick.bid;

        // Update ATH
        if (bid > ath) {
            ath = bid;
        }

        // Calculate floating P&L and equity
        double floating_pnl = 0;
        for (const auto& pos : positions) {
            floating_pnl += (bid - pos.entry_price) * per_dollar;
        }
        double equity = balance + floating_pnl;

        // Update peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
        }

        // Calculate equity drawdown from peak
        double dd_pct = 0;
        if (peak_equity > 0) {
            dd_pct = (peak_equity - equity) / peak_equity * 100.0;
        }
        if (dd_pct > max_dd_pct) {
            max_dd_pct = dd_pct;
        }

        // STOP-OUT: equity drops X% from peak equity
        if (dd_pct >= survive_pct && !positions.empty()) {
            // Close all positions (equity is already calculated)
            balance = std::max(0.0, equity);
            positions.clear();
            filled_levels.clear();
            stop_out_count++;

            // Restart: deposit same initial amount
            result.total_deposits += initial_deposit;
            balance = initial_deposit;
            ath = bid;  // Reset ATH
            peak_equity = initial_deposit;  // Reset peak
            continue;
        }

        // Also check broker stop-out (20% margin level)
        double total_margin = 0;
        for (const auto& pos : positions) {
            total_margin += spec.lot_size * spec.contract_size / spec.leverage * pos.entry_price;
        }
        if (!positions.empty() && total_margin > 0 && equity < 0.20 * total_margin) {
            balance = std::max(0.0, equity);
            positions.clear();
            filled_levels.clear();
            stop_out_count++;

            result.total_deposits += initial_deposit;
            balance = initial_deposit;
            ath = bid;
            peak_equity = initial_deposit;
            continue;
        }

        // Withdraw: if equity exceeds peak by enough, take some out
        // Only withdraw when no positions are underwater (floating >= 0)
        // Withdraw amount that keeps equity at previous peak level
        if (floating_pnl >= 0 && equity > peak_equity * 1.01) {
            double withdrawable = std::min(balance, equity - peak_equity);
            if (withdrawable > 0) {
                total_withdrawals += withdrawable;
                balance -= withdrawable;
            }
        }

        // Open new positions: fill $1 levels within survive% of ATH (price-based band)
        // The band defines WHERE we open, equity DD defines WHEN we exit
        double floor_price = ath * (1.0 - survive_pct / 100.0);
        int current_level = (int)std::floor(ask / spec.spacing);
        int ath_level = (int)std::floor(ath / spec.spacing);
        int floor_level = (int)std::ceil(floor_price / spec.spacing);

        if (current_level < ath_level && current_level >= floor_level) {
            if (filled_levels.find(current_level) == filled_levels.end()) {
                positions.push_back({ask});
                filled_levels.insert(current_level);

                if ((int)positions.size() > max_positions) {
                    max_positions = (int)positions.size();
                }
            }
        }
    }

    // Final equity
    double final_floating = 0;
    double last_bid = ticks.back().bid;
    for (const auto& pos : positions) {
        final_floating += (last_bid - pos.entry_price) * per_dollar;
    }
    result.final_equity = balance + final_floating;

    // Add final equity as last withdrawal (closing everything)
    total_withdrawals += std::max(0.0, result.final_equity);

    result.total_withdrawals = total_withdrawals;
    result.net_profit = total_withdrawals - result.total_deposits;
    result.stop_out_count = stop_out_count;
    result.max_positions = max_positions;
    result.final_positions = (int)positions.size();
    result.peak_equity = peak_equity;
    result.max_dd_pct = max_dd_pct;
    result.roi_on_deposits = (result.total_deposits > 0) ?
        (result.net_profit / result.total_deposits * 100.0) : 0;

    return result;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " NEVER-CLOSE V2: Equity-based drawdown stop-out" << std::endl;
    std::cout << " Stop-out when equity drops X% from peak equity" << std::endl;
    std::cout << " (not price-based)" << std::endl;
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

    // Test with different initial deposits and X% values
    std::vector<double> silver_deposits = {100, 500, 1000, 2000};
    std::vector<double> gold_deposits = {1000, 5000, 10000, 20000};

    // ===== SILVER =====
    std::cout << "\nLoading XAGUSD ticks..." << std::endl;
    auto silver_ticks = load_ticks(silver_path);
    std::cout << "Loaded " << silver_ticks.size() << " ticks" << std::endl;

    InstrumentSpec silver = {"XAGUSD", 5000.0, 500.0, 0.01, 1.0};

    std::cout << "\n============ SILVER (XAGUSD) ============" << std::endl;

    for (double deposit : silver_deposits) {
        std::cout << "\n--- Initial Deposit: $" << (int)deposit << " ---" << std::endl;
        std::cout << std::setw(4) << "X%"
                  << std::setw(10) << "TotDep"
                  << std::setw(10) << "TotWith"
                  << std::setw(10) << "NetProf"
                  << std::setw(8) << "ROI%"
                  << std::setw(5) << "SO"
                  << std::setw(7) << "MaxPos"
                  << std::setw(7) << "EndPos"
                  << std::setw(10) << "PeakEq"
                  << std::setw(8) << "MaxDD%" << std::endl;
        std::cout << std::string(79, '-') << std::endl;

        for (int X = 5; X <= 50; X += 5) {
            auto r = RunNeverCloseV2(silver, silver_ticks, (double)X, deposit,
                                     "2025.01.02", "2026.01.23");

            std::cout << std::setw(3) << X << "%"
                      << std::setw(9) << std::fixed << std::setprecision(0) << r.total_deposits << "$"
                      << std::setw(9) << r.total_withdrawals << "$"
                      << std::setw(9) << r.net_profit << "$"
                      << std::setw(7) << std::setprecision(0) << r.roi_on_deposits << "%"
                      << std::setw(5) << r.stop_out_count
                      << std::setw(7) << r.max_positions
                      << std::setw(7) << r.final_positions
                      << std::setw(9) << r.peak_equity << "$"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::endl;
        }
    }

    // ===== GOLD =====
    std::cout << "\n\nLoading XAUUSD ticks..." << std::endl;
    auto gold_ticks = load_ticks(gold_path);
    std::cout << "Loaded " << gold_ticks.size() << " ticks" << std::endl;

    InstrumentSpec gold = {"XAUUSD", 100.0, 500.0, 0.01, 1.0};

    std::cout << "\n============ GOLD (XAUUSD) ============" << std::endl;

    for (double deposit : gold_deposits) {
        std::cout << "\n--- Initial Deposit: $" << (int)deposit << " ---" << std::endl;
        std::cout << std::setw(4) << "X%"
                  << std::setw(12) << "TotDep"
                  << std::setw(12) << "TotWith"
                  << std::setw(12) << "NetProf"
                  << std::setw(8) << "ROI%"
                  << std::setw(5) << "SO"
                  << std::setw(7) << "MaxPos"
                  << std::setw(7) << "EndPos"
                  << std::setw(12) << "PeakEq"
                  << std::setw(8) << "MaxDD%" << std::endl;
        std::cout << std::string(91, '-') << std::endl;

        for (int X = 5; X <= 50; X += 5) {
            auto r = RunNeverCloseV2(gold, gold_ticks, (double)X, deposit,
                                     "2025.01.01", "2025.12.29");

            std::cout << std::setw(3) << X << "%"
                      << std::setw(11) << std::fixed << std::setprecision(0) << r.total_deposits << "$"
                      << std::setw(11) << r.total_withdrawals << "$"
                      << std::setw(11) << r.net_profit << "$"
                      << std::setw(7) << std::setprecision(0) << r.roi_on_deposits << "%"
                      << std::setw(5) << r.stop_out_count
                      << std::setw(7) << r.max_positions
                      << std::setw(7) << r.final_positions
                      << std::setw(11) << r.peak_equity << "$"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::endl;
        }
    }

    std::cout << "\n--- INTERPRETATION ---" << std::endl;
    std::cout << "X%:      Stop-out when equity drops X% from peak equity" << std::endl;
    std::cout << "TotDep:  Total deposits (initial + restarts)" << std::endl;
    std::cout << "NetProf: Total withdrawn - Total deposited" << std::endl;
    std::cout << "SO:      Number of equity-based stop-outs" << std::endl;
    std::cout << "PeakEq:  Highest equity reached" << std::endl;
    std::cout << "MaxDD%:  Worst equity drawdown experienced" << std::endl;

    std::cout << "\nDone." << std::endl;
    return 0;
}
