#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <fstream>

using namespace backtest;

struct InstrumentSpec {
    std::string name;
    double contract_size;
    double leverage;
    double spacing;
    double margin_stop_out_level;  // 20%
    double initial_margin_rate;    // 1.0 for most
};

struct Position {
    double entry_price;
    double lot_size;
    double tp_price;   // 0 = no TP (up-while-up)
    bool is_grid;      // true = fill-up grid, false = up-while-up
};

struct StrategyResult {
    double initial_deposit;
    double final_equity;
    double max_equity;
    double min_equity;
    double max_dd_pct;
    int grid_tp_count;
    int up_positions_opened;
    int grid_positions_opened;
    int max_positions;
    int final_positions;
    double total_up_lots;
    double total_grid_lots;
    bool stopped_out;
    std::string stopout_date;
};

StrategyResult RunFillUpWithUpWhileUp(
    const InstrumentSpec& spec,
    const std::vector<Tick>& ticks,
    double survive_pct,
    double initial_balance,
    const std::string& start_date,
    const std::string& end_date)
{
    StrategyResult result = {};
    result.initial_deposit = initial_balance;
    result.stopped_out = false;

    double balance = initial_balance;
    std::vector<Position> positions;
    double per_dollar = spec.contract_size;  // P&L per $1 per 1.0 lot

    // Fill-up grid state
    double lowest_buy = DBL_MAX;
    double highest_buy = DBL_MIN;

    // Tracking
    double max_equity = initial_balance;
    double min_equity = initial_balance;
    int grid_tp_count = 0;
    int up_positions_opened = 0;
    int grid_positions_opened = 0;
    int max_positions_count = 0;
    double total_up_lots = 0;
    double total_grid_lots = 0;

    bool started = false;

    for (const auto& tick : ticks) {
        if (!start_date.empty() && tick.timestamp < start_date) continue;
        if (!end_date.empty() && tick.timestamp >= end_date) break;

        double ask = tick.ask;
        double bid = tick.bid;
        double spread = ask - bid;

        if (!started) {
            started = true;
        }

        // --- Calculate current state ---
        double floating_pnl = 0;
        double volume_up = 0;      // Total lots of up-while-up positions
        double volume_grid = 0;    // Total lots of grid positions
        double used_margin = 0;

        for (const auto& pos : positions) {
            floating_pnl += (bid - pos.entry_price) * pos.lot_size * per_dollar;
            used_margin += pos.lot_size * spec.contract_size * pos.entry_price / spec.leverage * spec.initial_margin_rate;
            if (pos.is_grid) {
                volume_grid += pos.lot_size;
            } else {
                volume_up += pos.lot_size;
            }
        }

        double equity = balance + floating_pnl;

        // Track equity extremes
        if (equity > max_equity) max_equity = equity;
        if (equity < min_equity) min_equity = equity;

        // --- Check broker stop-out (20% margin level) ---
        if (!positions.empty() && used_margin > 0 && equity < spec.margin_stop_out_level / 100.0 * used_margin) {
            result.stopped_out = true;
            result.stopout_date = tick.timestamp;
            balance = std::max(0.0, equity);
            positions.clear();
            break;
        }

        // --- Check grid TPs ---
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            if (positions[i].tp_price > 0 && bid >= positions[i].tp_price) {
                double profit = (positions[i].tp_price - positions[i].entry_price) * positions[i].lot_size * per_dollar;
                balance += profit;
                grid_tp_count++;
                positions.erase(positions.begin() + i);
            }
        }

        // Recalculate after TPs
        floating_pnl = 0;
        volume_up = 0;
        used_margin = 0;
        for (const auto& pos : positions) {
            floating_pnl += (bid - pos.entry_price) * pos.lot_size * per_dollar;
            used_margin += pos.lot_size * spec.contract_size * pos.entry_price / spec.leverage * spec.initial_margin_rate;
            if (!pos.is_grid) volume_up += pos.lot_size;
        }
        equity = balance + floating_pnl;

        // --- UP-WHILE-UP COMPONENT ---
        // Calculate max lot size that maintains survive_pct% survival
        double total_volume = volume_up;  // Only count up-while-up for this sizing
        double distance = ask * survive_pct / 100.0;
        double end_price = ask * (1.0 - survive_pct / 100.0);

        // Sizing formula (CFDLEVERAGE mode):
        // At the bottom (price drops by 'distance'):
        //   floating_loss = total_volume * distance * contract_size
        //   new_floating_loss = trade_size * distance * contract_size
        //   new_spread_loss = trade_size * spread * contract_size
        //   new_margin = trade_size * contract_size * avg_price / leverage * margin_rate
        //   Equity at bottom = equity - (total_volume + trade_size) * distance * contract_size - trade_size * spread * contract_size
        //   Margin at bottom = used_margin + trade_size * contract_size * ask / leverage * margin_rate
        //   Require: equity_at_bottom >= margin_stop_out_level/100 * margin_at_bottom
        //
        // Solving for trade_size:
        double equity_at_bottom_no_new = equity - total_volume * distance * per_dollar;
        double margin_so = spec.margin_stop_out_level / 100.0;

        // trade_size * (distance * per_dollar + spread * per_dollar + margin_so * contract_size * ask / leverage * margin_rate)
        //   <= equity_at_bottom_no_new - margin_so * used_margin
        double cost_per_lot = distance * per_dollar + spread * per_dollar +
                              margin_so * spec.contract_size * ask / spec.leverage * spec.initial_margin_rate;

        double available = equity_at_bottom_no_new - margin_so * used_margin;

        double trade_size = 0;
        if (cost_per_lot > 0 && available > 0) {
            trade_size = available / cost_per_lot;
            // Round down to 0.01
            trade_size = std::floor(trade_size * 100.0) / 100.0;
        }

        if (trade_size >= 0.01) {
            // Cap at reasonable max (100 lots)
            trade_size = std::min(trade_size, 100.0);
            Position pos;
            pos.entry_price = ask;
            pos.lot_size = trade_size;
            pos.tp_price = 0;  // No TP for up-while-up
            pos.is_grid = false;
            positions.push_back(pos);
            up_positions_opened++;
            total_up_lots += trade_size;
        }

        // --- FILL-UP GRID COMPONENT ---
        // Recalculate grid volume and sizing
        volume_grid = 0;
        for (const auto& pos : positions) {
            if (pos.is_grid) volume_grid += pos.lot_size;
        }

        // Grid sizing: same formula but for grid positions
        double grid_total_volume = volume_grid + volume_up;  // All positions affect survival
        double grid_equity_at_bottom = equity - grid_total_volume * distance * per_dollar;

        // Recalculate used_margin with new up-while-up position
        used_margin = 0;
        for (const auto& pos : positions) {
            used_margin += pos.lot_size * spec.contract_size * pos.entry_price / spec.leverage * spec.initial_margin_rate;
        }

        double grid_available = grid_equity_at_bottom - margin_so * used_margin;
        double grid_trade_size = 0;
        if (cost_per_lot > 0 && grid_available > 0) {
            grid_trade_size = grid_available / cost_per_lot;
            grid_trade_size = std::floor(grid_trade_size * 100.0) / 100.0;
        }
        grid_trade_size = std::min(grid_trade_size, 100.0);

        // Grid open logic: below lowest or above highest (with spacing)
        if (grid_trade_size >= 0.01) {
            bool opened = false;
            if (positions.empty() || lowest_buy == DBL_MAX) {
                // First grid position
                Position pos;
                pos.entry_price = ask;
                pos.lot_size = grid_trade_size;
                pos.tp_price = ask + spread + spec.spacing;
                pos.is_grid = true;
                positions.push_back(pos);
                lowest_buy = ask;
                highest_buy = ask;
                grid_positions_opened++;
                total_grid_lots += grid_trade_size;
                opened = true;
            } else {
                if (lowest_buy >= ask + spec.spacing) {
                    Position pos;
                    pos.entry_price = ask;
                    pos.lot_size = grid_trade_size;
                    pos.tp_price = ask + spread + spec.spacing;
                    pos.is_grid = true;
                    positions.push_back(pos);
                    lowest_buy = ask;
                    grid_positions_opened++;
                    total_grid_lots += grid_trade_size;
                    opened = true;
                } else if (highest_buy <= ask - spec.spacing) {
                    Position pos;
                    pos.entry_price = ask;
                    pos.lot_size = grid_trade_size;
                    pos.tp_price = ask + spread + spec.spacing;
                    pos.is_grid = true;
                    positions.push_back(pos);
                    highest_buy = ask;
                    grid_positions_opened++;
                    total_grid_lots += grid_trade_size;
                    opened = true;
                }
            }
        }

        // Update max positions
        if ((int)positions.size() > max_positions_count) {
            max_positions_count = (int)positions.size();
        }
    }

    // Final equity
    double final_floating = 0;
    for (const auto& pos : positions) {
        final_floating += (ticks.back().bid - pos.entry_price) * pos.lot_size * per_dollar;
    }
    result.final_equity = balance + final_floating;
    result.max_equity = max_equity;
    result.min_equity = min_equity;
    result.max_dd_pct = (max_equity > 0) ? (max_equity - min_equity) / max_equity * 100.0 : 0;
    result.grid_tp_count = grid_tp_count;
    result.up_positions_opened = up_positions_opened;
    result.grid_positions_opened = grid_positions_opened;
    result.max_positions = max_positions_count;
    result.final_positions = (int)positions.size();
    result.total_up_lots = total_up_lots;
    result.total_grid_lots = total_grid_lots;

    return result;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " FILL-UP + UP-WHILE-UP COMBINED STRATEGY" << std::endl;
    std::cout << " Grid with TP (oscillation profit)" << std::endl;
    std::cout << " + Up-while-up no TP (trend profit)" << std::endl;
    std::cout << " Dynamic sizing: max lots maintaining X% survive" << std::endl;
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

    // Different initial balances to test
    std::vector<double> test_balances = {1000, 5000, 10000, 50000};

    // ===== SILVER (survive 19%) =====
    std::cout << "\nLoading XAGUSD ticks..." << std::endl;
    auto silver_ticks = load_ticks(silver_path);
    std::cout << "Loaded " << silver_ticks.size() << " ticks" << std::endl;

    InstrumentSpec silver = {"XAGUSD", 5000.0, 500.0, 1.0, 20.0, 1.0};

    std::cout << "\n--- SILVER (XAGUSD, survive=19%, spacing=$1) ---" << std::endl;
    std::cout << std::setw(10) << "Balance"
              << std::setw(12) << "FinalEq"
              << std::setw(10) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "GridTP"
              << std::setw(8) << "UpPos"
              << std::setw(9) << "GridPos"
              << std::setw(8) << "MaxPos"
              << std::setw(10) << "UpLots"
              << std::setw(10) << "GridLots"
              << std::setw(6) << "SO?" << std::endl;
    std::cout << std::string(99, '-') << std::endl;

    for (double bal : test_balances) {
        auto r = RunFillUpWithUpWhileUp(silver, silver_ticks, 19.0, bal,
                                        "2025.01.02", "2026.01.23");

        double ret = (r.initial_deposit > 0) ? r.final_equity / r.initial_deposit : 0;

        std::cout << std::setw(9) << std::fixed << std::setprecision(0) << bal << "$"
                  << std::setw(11) << r.final_equity << "$"
                  << std::setw(8) << std::setprecision(1) << ret << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.grid_tp_count
                  << std::setw(8) << r.up_positions_opened
                  << std::setw(9) << r.grid_positions_opened
                  << std::setw(8) << r.max_positions
                  << std::setw(9) << std::setprecision(1) << r.total_up_lots
                  << std::setw(9) << r.total_grid_lots
                  << std::setw(5) << (r.stopped_out ? "YES" : "NO")
                  << std::endl;
        if (r.stopped_out) {
            std::cout << "    Stop-out date: " << r.stopout_date << std::endl;
        }
    }

    // ===== GOLD (survive 12%) =====
    std::cout << "\n\nLoading XAUUSD ticks..." << std::endl;
    auto gold_ticks = load_ticks(gold_path);
    std::cout << "Loaded " << gold_ticks.size() << " ticks" << std::endl;

    InstrumentSpec gold = {"XAUUSD", 100.0, 500.0, 1.0, 20.0, 1.0};

    std::cout << "\n--- GOLD (XAUUSD, survive=12%, spacing=$1) ---" << std::endl;
    std::cout << std::setw(10) << "Balance"
              << std::setw(12) << "FinalEq"
              << std::setw(10) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "GridTP"
              << std::setw(8) << "UpPos"
              << std::setw(9) << "GridPos"
              << std::setw(8) << "MaxPos"
              << std::setw(10) << "UpLots"
              << std::setw(10) << "GridLots"
              << std::setw(6) << "SO?" << std::endl;
    std::cout << std::string(99, '-') << std::endl;

    for (double bal : test_balances) {
        auto r = RunFillUpWithUpWhileUp(gold, gold_ticks, 12.0, bal,
                                        "2025.01.01", "2025.12.29");

        double ret = (r.initial_deposit > 0) ? r.final_equity / r.initial_deposit : 0;

        std::cout << std::setw(9) << std::fixed << std::setprecision(0) << bal << "$"
                  << std::setw(11) << r.final_equity << "$"
                  << std::setw(8) << std::setprecision(1) << ret << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.grid_tp_count
                  << std::setw(8) << r.up_positions_opened
                  << std::setw(9) << r.grid_positions_opened
                  << std::setw(8) << r.max_positions
                  << std::setw(9) << std::setprecision(1) << r.total_up_lots
                  << std::setw(9) << r.total_grid_lots
                  << std::setw(5) << (r.stopped_out ? "YES" : "NO")
                  << std::endl;
        if (r.stopped_out) {
            std::cout << "    Stop-out date: " << r.stopout_date << std::endl;
        }
    }

    std::cout << "\n--- COLUMN LEGEND ---" << std::endl;
    std::cout << "FinalEq:  Equity at end (balance + floating P&L)" << std::endl;
    std::cout << "Return:   FinalEq / InitialBalance" << std::endl;
    std::cout << "MaxDD%:   (PeakEquity - MinEquity) / PeakEquity" << std::endl;
    std::cout << "GridTP:   Number of grid take-profit hits" << std::endl;
    std::cout << "UpPos:    Number of up-while-up positions opened (no TP)" << std::endl;
    std::cout << "GridPos:  Number of grid positions opened (with TP)" << std::endl;
    std::cout << "MaxPos:   Peak simultaneous positions" << std::endl;
    std::cout << "UpLots:   Total lots opened by up-while-up" << std::endl;
    std::cout << "GridLots: Total lots opened by grid" << std::endl;

    std::cout << "\nDone." << std::endl;
    return 0;
}
