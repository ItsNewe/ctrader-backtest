#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <deque>

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

enum RestartCondition {
    IMMEDIATE,          // Restart on same tick
    WAIT_REVERSAL,      // Wait for spread-based turn-up detection
    WAIT_1PCT_BOUNCE,   // Wait for 1% recovery from post-stopout low
    WAIT_2PCT_BOUNCE,   // Wait for 2% recovery from post-stopout low
    WAIT_STABILIZE,     // Wait for 100-tick range to narrow below 0.5%
    WAIT_1000_TICKS,    // Fixed 1000 tick cooldown
    WAIT_10000_TICKS,   // Fixed 10000 tick cooldown
    NUM_CONDITIONS
};

const char* ConditionName(RestartCondition c) {
    switch (c) {
        case IMMEDIATE: return "IMMEDIATE";
        case WAIT_REVERSAL: return "REVERSAL";
        case WAIT_1PCT_BOUNCE: return "1%BOUNCE";
        case WAIT_2PCT_BOUNCE: return "2%BOUNCE";
        case WAIT_STABILIZE: return "STABILIZE";
        case WAIT_1000_TICKS: return "1K_TICKS";
        case WAIT_10000_TICKS: return "10K_TICKS";
        default: return "UNKNOWN";
    }
}

struct StrategyResult {
    double survive_pct;
    double initial_deposit;
    double total_deposits;
    double total_withdrawals;
    double net_profit;
    int stop_out_count;
    int tp_count;
    double roi_on_deposits;
    double max_ath;
    double final_price;
    long ticks_waiting;      // Total ticks spent waiting to restart
    int restart_count;       // How many times we actually restarted
    double avg_wait_ticks;   // Average wait per restart
    double restart_price_vs_stopout;  // Average: restart_price / stopout_price
};

StrategyResult RunWithdrawStrategy(
    const InstrumentSpec& spec,
    const std::vector<Tick>& ticks,
    double survive_pct,
    RestartCondition restart_cond,
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
        double tp_price;
    };
    std::vector<Position> positions;
    double lowest_entry = DBL_MAX;
    double per_dollar = spec.lot_size * spec.contract_size;
    int tp_count = 0;
    int stop_out_count = 0;

    // Restart waiting state
    bool waiting_to_restart = false;
    double stopout_price = 0;
    double post_stopout_low = DBL_MAX;
    long wait_tick_count = 0;
    long total_wait_ticks = 0;
    int restart_count = 0;
    double restart_price_sum = 0;
    double stopout_price_sum = 0;

    // For WAIT_REVERSAL: direction detection
    double bid_low_since_stop = DBL_MAX;

    // For WAIT_STABILIZE: recent price window
    std::deque<double> recent_prices;
    const int stabilize_window = 100;

    for (const auto& tick : ticks) {
        if (!start_date.empty() && tick.timestamp < start_date) continue;
        if (!end_date.empty() && tick.timestamp >= end_date) break;

        double ask = tick.ask;
        double bid = tick.bid;
        double spread = ask - bid;

        // If waiting to restart, check restart condition
        if (waiting_to_restart) {
            wait_tick_count++;

            // Track post-stopout low
            if (bid < post_stopout_low) {
                post_stopout_low = bid;
                bid_low_since_stop = bid;
            }

            // For stabilize: track recent prices
            if (restart_cond == WAIT_STABILIZE) {
                recent_prices.push_back(bid);
                if ((int)recent_prices.size() > stabilize_window) {
                    recent_prices.pop_front();
                }
            }

            bool can_restart = false;
            switch (restart_cond) {
                case IMMEDIATE:
                    can_restart = true;
                    break;

                case WAIT_REVERSAL: {
                    // Price must rise by at least 1 spread from post-stopout low
                    double threshold = spread;
                    if (threshold < 0.01) threshold = 0.01;
                    can_restart = (bid > bid_low_since_stop + threshold);
                    break;
                }

                case WAIT_1PCT_BOUNCE:
                    can_restart = (bid > post_stopout_low * 1.01);
                    break;

                case WAIT_2PCT_BOUNCE:
                    can_restart = (bid > post_stopout_low * 1.02);
                    break;

                case WAIT_STABILIZE: {
                    if ((int)recent_prices.size() >= stabilize_window) {
                        double hi = *std::max_element(recent_prices.begin(), recent_prices.end());
                        double lo = *std::min_element(recent_prices.begin(), recent_prices.end());
                        double range_pct = (hi - lo) / lo * 100.0;
                        can_restart = (range_pct < 0.5);  // Range < 0.5% of price
                    }
                    break;
                }

                case WAIT_1000_TICKS:
                    can_restart = (wait_tick_count >= 1000);
                    break;

                case WAIT_10000_TICKS:
                    can_restart = (wait_tick_count >= 10000);
                    break;

                default:
                    can_restart = true;
                    break;
            }

            if (can_restart) {
                // Restart now
                double restart_price = bid;
                double new_deposit = RequiredFunds(spec, restart_price, survive_pct);
                result.total_deposits += new_deposit;
                balance = new_deposit;
                ath = restart_price;
                waiting_to_restart = false;
                total_wait_ticks += wait_tick_count;
                restart_count++;
                restart_price_sum += restart_price;
                stopout_price_sum += stopout_price;
                recent_prices.clear();
                continue;
            }
            continue;  // Still waiting
        }

        // Check TPs
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            if (bid >= positions[i].tp_price) {
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

        // Withdraw excess
        double required = RequiredFunds(spec, ath, survive_pct);
        if (positions.empty() && balance > required * 1.01) {
            double withdrawal = balance - required;
            total_withdrawals += withdrawal;
            balance = required;
        }

        // Check stop-out
        double stop_out_price = ath * (1.0 - survive_pct / 100.0);
        if (bid <= stop_out_price && !positions.empty()) {
            double total_loss = 0;
            for (const auto& pos : positions) {
                total_loss += (pos.entry_price - bid) * per_dollar;
            }
            balance = std::max(0.0, balance - total_loss);
            positions.clear();
            lowest_entry = DBL_MAX;
            stop_out_count++;

            // Enter waiting state
            waiting_to_restart = true;
            stopout_price = bid;
            post_stopout_low = bid;
            bid_low_since_stop = bid;
            wait_tick_count = 0;
            recent_prices.clear();

            // For IMMEDIATE: restart happens next iteration
            if (restart_cond == IMMEDIATE) {
                double new_deposit = RequiredFunds(spec, bid, survive_pct);
                result.total_deposits += new_deposit;
                balance = new_deposit;
                ath = bid;
                waiting_to_restart = false;
                restart_count++;
                restart_price_sum += bid;
                stopout_price_sum += bid;
            }
            continue;
        }

        // Open new positions
        if (positions.empty()) {
            double tp = ask + spread + spec.spacing;
            positions.push_back({ask, tp});
            lowest_entry = ask;
        } else {
            if (lowest_entry > ask + spec.spacing) {
                double tp = ask + spread + spec.spacing;
                positions.push_back({ask, tp});
                lowest_entry = ask;
            }
        }

        result.final_price = bid;
    }

    // Close remaining positions at market
    double final_floating = 0;
    for (const auto& pos : positions) {
        final_floating += (result.final_price - pos.entry_price) * per_dollar;
    }
    balance += final_floating;
    total_withdrawals += std::max(0.0, balance);

    result.total_withdrawals = total_withdrawals;
    result.net_profit = total_withdrawals - result.total_deposits;
    result.stop_out_count = stop_out_count;
    result.tp_count = tp_count;
    result.roi_on_deposits = (result.total_deposits > 0) ?
        (result.net_profit / result.total_deposits * 100.0) : 0;
    result.max_ath = ath;
    result.ticks_waiting = total_wait_ticks;
    result.restart_count = restart_count;
    result.avg_wait_ticks = (restart_count > 0) ? (double)total_wait_ticks / restart_count : 0;
    result.restart_price_vs_stopout = (restart_count > 0 && stopout_price_sum > 0) ?
        (restart_price_sum / stopout_price_sum) : 1.0;

    return result;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " RESTART CONDITIONS COMPARISON" << std::endl;
    std::cout << " Testing different conditions for restarting after stop-out" << std::endl;
    std::cout << "========================================================" << std::endl;

    // Load ticks
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

    // Test X values where stop-outs actually occur
    std::vector<int> silver_x_values = {5, 8, 10, 13, 15, 18};
    std::vector<int> gold_x_values = {3, 5, 7, 9, 11};

    // ===== SILVER =====
    std::cout << "\nLoading XAGUSD ticks..." << std::endl;
    auto silver_ticks = load_ticks(silver_path);
    std::cout << "Loaded " << silver_ticks.size() << " ticks" << std::endl;

    InstrumentSpec silver = {"XAGUSD", 5000.0, 500.0, 0.01, 1.0};

    std::cout << "\n============ SILVER (XAGUSD) ============" << std::endl;

    for (int X : silver_x_values) {
        std::cout << "\n--- X=" << X << "% ---" << std::endl;
        std::cout << std::setw(11) << "Condition"
                  << std::setw(10) << "TotDep"
                  << std::setw(10) << "TotWith"
                  << std::setw(10) << "NetProf"
                  << std::setw(7) << "ROI%"
                  << std::setw(5) << "SO"
                  << std::setw(6) << "TPs"
                  << std::setw(10) << "AvgWait"
                  << std::setw(8) << "RestP%" << std::endl;
        std::cout << std::string(75, '-') << std::endl;

        for (int c = 0; c < NUM_CONDITIONS; c++) {
            auto r = RunWithdrawStrategy(silver, silver_ticks, (double)X,
                                         (RestartCondition)c,
                                         "2025.01.02", "2026.01.23");

            double restart_pct = (r.restart_price_vs_stopout - 1.0) * 100.0;

            std::cout << std::setw(11) << ConditionName((RestartCondition)c)
                      << std::setw(9) << std::fixed << std::setprecision(0) << r.total_deposits << "$"
                      << std::setw(9) << r.total_withdrawals << "$"
                      << std::setw(9) << r.net_profit << "$"
                      << std::setw(6) << std::setprecision(0) << r.roi_on_deposits << "%"
                      << std::setw(5) << r.stop_out_count
                      << std::setw(6) << r.tp_count
                      << std::setw(10) << std::setprecision(0) << r.avg_wait_ticks
                      << std::setw(7) << std::setprecision(2) << restart_pct << "%"
                      << std::endl;
        }
    }

    // ===== GOLD =====
    std::cout << "\n\nLoading XAUUSD ticks..." << std::endl;
    auto gold_ticks = load_ticks(gold_path);
    std::cout << "Loaded " << gold_ticks.size() << " ticks" << std::endl;

    InstrumentSpec gold = {"XAUUSD", 100.0, 500.0, 0.01, 1.0};

    std::cout << "\n============ GOLD (XAUUSD) ============" << std::endl;

    for (int X : gold_x_values) {
        std::cout << "\n--- X=" << X << "% ---" << std::endl;
        std::cout << std::setw(11) << "Condition"
                  << std::setw(12) << "TotDep"
                  << std::setw(12) << "TotWith"
                  << std::setw(12) << "NetProf"
                  << std::setw(7) << "ROI%"
                  << std::setw(5) << "SO"
                  << std::setw(7) << "TPs"
                  << std::setw(10) << "AvgWait"
                  << std::setw(8) << "RestP%" << std::endl;
        std::cout << std::string(82, '-') << std::endl;

        for (int c = 0; c < NUM_CONDITIONS; c++) {
            auto r = RunWithdrawStrategy(gold, gold_ticks, (double)X,
                                         (RestartCondition)c,
                                         "2025.01.01", "2025.12.29");

            double restart_pct = (r.restart_price_vs_stopout - 1.0) * 100.0;

            std::cout << std::setw(11) << ConditionName((RestartCondition)c)
                      << std::setw(11) << std::fixed << std::setprecision(0) << r.total_deposits << "$"
                      << std::setw(11) << r.total_withdrawals << "$"
                      << std::setw(11) << r.net_profit << "$"
                      << std::setw(6) << std::setprecision(0) << r.roi_on_deposits << "%"
                      << std::setw(5) << r.stop_out_count
                      << std::setw(7) << r.tp_count
                      << std::setw(10) << std::setprecision(0) << r.avg_wait_ticks
                      << std::setw(7) << std::setprecision(2) << restart_pct << "%"
                      << std::endl;
        }
    }

    std::cout << "\n--- COLUMN LEGEND ---" << std::endl;
    std::cout << "TotDep:   Total capital deposited (initial + restarts)" << std::endl;
    std::cout << "TotWith:  Total withdrawn (profits + final balance)" << std::endl;
    std::cout << "NetProf:  TotWith - TotDep" << std::endl;
    std::cout << "ROI%:     NetProf / TotDep * 100" << std::endl;
    std::cout << "SO:       Number of stop-outs" << std::endl;
    std::cout << "TPs:      Number of take-profit hits" << std::endl;
    std::cout << "AvgWait:  Average ticks waited before restart" << std::endl;
    std::cout << "RestP%:   Average restart price vs stopout price" << std::endl;
    std::cout << "          (positive = restarted higher, avoids further drop)" << std::endl;

    std::cout << "\nDone." << std::endl;
    return 0;
}
