/**
 * Debug test - single run with verbose output
 * Compare with MT5 AU=24%, AG=29%
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cfloat>

using namespace backtest;

struct MergedTick {
    std::string timestamp;
    std::string symbol;
    double bid;
    double ask;
};

struct Position {
    std::string symbol;
    double entry_price;
    double lot_size;
    double contract_size;
};

struct SymbolState {
    std::string symbol;
    double contract_size;
    double survive_down;
    double volume_of_open_trades;
    double checked_last_open_price;
    std::vector<Position> positions;
};

double leverage = 500.0;
double margin_stop_out = 20.0;
int account_limit = 200;

double GetEquity(SymbolState& gold, SymbolState& silver, double balance, double au_bid, double ag_bid) {
    double unrealized = 0;
    for (const auto& p : gold.positions) {
        unrealized += (au_bid - p.entry_price) * p.lot_size * p.contract_size;
    }
    for (const auto& p : silver.positions) {
        unrealized += (ag_bid - p.entry_price) * p.lot_size * p.contract_size;
    }
    return balance + unrealized;
}

double GetUsedMargin(SymbolState& gold, SymbolState& silver, double au_ask, double ag_ask) {
    double margin = 0;
    for (const auto& p : gold.positions) {
        margin += p.lot_size * gold.contract_size * au_ask / leverage;
    }
    for (const auto& p : silver.positions) {
        margin += p.lot_size * silver.contract_size * ag_ask / leverage;
    }
    return margin;
}

int main() {
    std::cout << "=== Debug Single Run: AU=24%, AG=29% ===" << std::endl;

    // Load ticks
    std::vector<MergedTick> ticks;

    std::ifstream au_file("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    std::string line;
    std::getline(au_file, line);
    while (std::getline(au_file, line)) {
        if (line.empty()) continue;
        MergedTick tick;
        std::stringstream ss(line);
        std::string bid_str, ask_str;
        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');
        tick.symbol = "XAUUSD";
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        ticks.push_back(tick);
    }
    au_file.close();

    std::ifstream ag_file("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv");
    std::getline(ag_file, line);
    while (std::getline(ag_file, line)) {
        if (line.empty()) continue;
        MergedTick tick;
        std::stringstream ss(line);
        std::string bid_str, ask_str;
        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');
        tick.symbol = "XAGUSD";
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        ticks.push_back(tick);
    }
    ag_file.close();

    std::sort(ticks.begin(), ticks.end(), [](const MergedTick& a, const MergedTick& b) {
        return a.timestamp < b.timestamp;
    });

    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;

    // Initialize
    SymbolState gold, silver;
    gold.symbol = "XAUUSD";
    gold.contract_size = 100.0;
    gold.survive_down = 24.0;
    gold.volume_of_open_trades = 0;
    gold.checked_last_open_price = DBL_MIN;

    silver.symbol = "XAGUSD";
    silver.contract_size = 5000.0;
    silver.survive_down = 29.0;
    silver.volume_of_open_trades = 0;
    silver.checked_last_open_price = DBL_MIN;

    double balance = 10000.0;
    double peak_equity = 10000.0;
    double max_dd = 0;
    int total_trades = 0;
    int positions_closed = 0;

    double last_au_ask = 2600, last_ag_ask = 30;
    double last_au_bid = 2600, last_ag_bid = 30;

    int print_count = 0;

    for (const auto& tick : ticks) {
        if (tick.symbol == "XAUUSD") {
            last_au_ask = tick.ask;
            last_au_bid = tick.bid;
        } else {
            last_ag_ask = tick.ask;
            last_ag_bid = tick.bid;
        }

        SymbolState& sym = (tick.symbol == "XAUUSD") ? gold : silver;
        double ask = tick.ask;

        double equity = GetEquity(gold, silver, balance, last_au_bid, last_ag_bid);
        double used_margin = GetUsedMargin(gold, silver, last_au_ask, last_ag_ask);
        double margin_level = (used_margin > 0) ? (equity / used_margin) * 100.0 : 10000.0;

        // Track DD
        if (equity > peak_equity) peak_equity = equity;
        double dd = (peak_equity - equity) / peak_equity * 100.0;
        if (dd > max_dd) max_dd = dd;

        // Check stop out
        if (used_margin > 0 && margin_level < margin_stop_out) {
            std::cout << "STOP OUT at " << tick.timestamp << std::endl;
            break;
        }

        // Up while up check
        if (sym.volume_of_open_trades == 0 || ask > sym.checked_last_open_price) {
            sym.checked_last_open_price = ask;

            // Recalc volume
            sym.volume_of_open_trades = 0;
            for (const auto& p : sym.positions) {
                sym.volume_of_open_trades += p.lot_size;
            }

            // Should open check
            bool should_open = false;
            if (sym.volume_of_open_trades == 0) {
                should_open = true;
            } else {
                double equity_at_target = (margin_level > 0) ?
                    equity * margin_stop_out / margin_level : equity;
                double eq_diff = equity - equity_at_target;
                double price_diff = eq_diff / (sym.volume_of_open_trades * sym.contract_size);
                double end_price = ask * ((100.0 - sym.survive_down) / 100.0);
                should_open = (ask - price_diff) < end_price;
            }

            if (should_open) {
                // Calculate trade size
                double end_price = ask * ((100.0 - sym.survive_down) / 100.0);
                double distance = ask - end_price;

                double numerator = 100.0 * equity * leverage
                    - 100.0 * sym.contract_size * std::fabs(distance) * sym.volume_of_open_trades * leverage
                    - leverage * margin_stop_out * used_margin;

                double denominator = sym.contract_size * (
                    100.0 * std::fabs(distance) * leverage
                    + ask * 1.0 * margin_stop_out  // initial_margin_rate = 1.0
                );

                double trade_size = numerator / denominator;
                trade_size = std::floor(trade_size * 100.0) / 100.0;

                if (trade_size >= 0.01) {
                    trade_size = std::min(trade_size, 100.0);

                    // Check order limit
                    int total_pos = gold.positions.size() + silver.positions.size();
                    if (total_pos >= account_limit) {
                        // Close smallest profitable
                        double min_vol = DBL_MAX;
                        int best_idx = -1;
                        bool is_gold = false;

                        for (size_t i = 0; i < gold.positions.size(); i++) {
                            double profit = (last_au_bid - gold.positions[i].entry_price) * gold.positions[i].lot_size * gold.contract_size;
                            if (profit > 0 && gold.positions[i].lot_size < min_vol) {
                                min_vol = gold.positions[i].lot_size;
                                best_idx = i;
                                is_gold = true;
                            }
                        }
                        for (size_t i = 0; i < silver.positions.size(); i++) {
                            double profit = (last_ag_bid - silver.positions[i].entry_price) * silver.positions[i].lot_size * silver.contract_size;
                            if (profit > 0 && silver.positions[i].lot_size < min_vol) {
                                min_vol = silver.positions[i].lot_size;
                                best_idx = i;
                                is_gold = false;
                            }
                        }

                        if (best_idx >= 0) {
                            if (is_gold) {
                                double profit = (last_au_bid - gold.positions[best_idx].entry_price) * gold.positions[best_idx].lot_size * gold.contract_size;
                                balance += profit;
                                gold.volume_of_open_trades -= gold.positions[best_idx].lot_size;
                                gold.positions.erase(gold.positions.begin() + best_idx);
                            } else {
                                double profit = (last_ag_bid - silver.positions[best_idx].entry_price) * silver.positions[best_idx].lot_size * silver.contract_size;
                                balance += profit;
                                silver.volume_of_open_trades -= silver.positions[best_idx].lot_size;
                                silver.positions.erase(silver.positions.begin() + best_idx);
                            }
                            positions_closed++;

                            // Recalculate
                            equity = GetEquity(gold, silver, balance, last_au_bid, last_ag_bid);
                            used_margin = GetUsedMargin(gold, silver, last_au_ask, last_ag_ask);

                            numerator = 100.0 * equity * leverage
                                - 100.0 * sym.contract_size * std::fabs(distance) * sym.volume_of_open_trades * leverage
                                - leverage * margin_stop_out * used_margin;

                            trade_size = numerator / denominator;
                            trade_size = std::floor(trade_size * 100.0) / 100.0;
                            trade_size = std::min(std::max(trade_size, 0.0), 100.0);
                        }
                    }

                    if (trade_size >= 0.01) {
                        Position pos;
                        pos.symbol = tick.symbol;
                        pos.entry_price = ask;
                        pos.lot_size = trade_size;
                        pos.contract_size = sym.contract_size;

                        sym.positions.push_back(pos);
                        sym.volume_of_open_trades += trade_size;
                        total_trades++;

                        // Print first 20 trades
                        if (print_count < 20) {
                            std::cout << std::fixed << std::setprecision(2);
                            std::cout << tick.timestamp << " " << tick.symbol
                                      << " BUY " << trade_size << " @ " << ask
                                      << " equity=" << equity
                                      << " balance=" << balance << std::endl;
                            print_count++;
                        }
                    }
                }
            }
        }
    }

    double final_equity = GetEquity(gold, silver, balance, last_au_bid, last_ag_bid);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== DEBUG FINAL STATE ===" << std::endl;
    std::cout << "Last AU bid: " << last_au_bid << ", Last AU ask: " << last_au_ask << std::endl;
    std::cout << "Last AG bid: " << last_ag_bid << ", Last AG ask: " << last_ag_ask << std::endl;

    // Calculate unrealized for each symbol
    double au_unrealized = 0, ag_unrealized = 0;
    double au_avg_entry = 0, ag_avg_entry = 0;
    for (const auto& p : gold.positions) {
        au_unrealized += (last_au_bid - p.entry_price) * p.lot_size * p.contract_size;
        au_avg_entry += p.entry_price * p.lot_size;
    }
    if (gold.volume_of_open_trades > 0) au_avg_entry /= gold.volume_of_open_trades;

    for (const auto& p : silver.positions) {
        ag_unrealized += (last_ag_bid - p.entry_price) * p.lot_size * p.contract_size;
        ag_avg_entry += p.entry_price * p.lot_size;
    }
    if (silver.volume_of_open_trades > 0) ag_avg_entry /= silver.volume_of_open_trades;

    std::cout << "Gold avg entry: " << au_avg_entry << ", unrealized: " << au_unrealized << std::endl;
    std::cout << "Silver avg entry: " << ag_avg_entry << ", unrealized: " << ag_unrealized << std::endl;

    std::cout << "\n=== RESULTS ===" << std::endl;
    std::cout << "Final Balance: $" << balance << std::endl;
    std::cout << "Final Equity: $" << final_equity << " (" << (final_equity/10000.0) << "x)" << std::endl;
    std::cout << "Max DD: " << max_dd << "%" << std::endl;
    std::cout << "Total trades: " << total_trades << std::endl;
    std::cout << "Positions closed: " << positions_closed << std::endl;
    std::cout << "AU positions: " << gold.positions.size() << " (vol=" << gold.volume_of_open_trades << ")" << std::endl;
    std::cout << "AG positions: " << silver.positions.size() << " (vol=" << silver.volume_of_open_trades << ")" << std::endl;

    return 0;
}
