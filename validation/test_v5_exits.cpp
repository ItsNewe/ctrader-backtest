/**
 * V5 Exit Strategy Optimization Test
 *
 * Tests comprehensive exit and position management improvements starting from
 * V5 SMA 11000 baseline. This test explores:
 * 1. Dynamic Take Profit levels (0.5x to 2.0x spacing)
 * 2. Trailing Stop mechanisms
 * 3. Partial Take Profit strategies
 * 4. Time-based exits
 * 5. Trend Reversal exits (price crosses below SMA)
 * 6. Aggressive DD exits (15%, 20% vs baseline 25%)
 *
 * Compile:
 *   cd validation
 *   g++ -std=c++17 -O0 -o test_v5_exits test_v5_exits.cpp -I../include
 *
 * Note: Must use -O0 (no optimization) due to compiler issues with -O2 on large file I/O
 *
 * Run:
 *   ./test_v5_exits.exe
 */

#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

using namespace backtest;

// ===========================
// CONFIGURATION STRUCTURES
// ===========================

struct ExitConfig {
    std::string name;

    // V5 Core parameters (fixed)
    double stop_new = 5.0;
    double partial_close = 8.0;
    double close_all = 25.0;
    int max_positions = 20;
    double partial_pct = 0.50;
    int sma_period = 11000;  // V5 optimal

    // EXIT STRATEGIES

    // 1. Dynamic Take Profit
    double tp_multiplier = 1.0;  // Multiply spacing by this (0.5, 1.0, 1.5, 2.0)

    // 2. Trailing Stop
    bool use_trailing_stop = false;
    double trail_percent = 0.0;  // Trail at X% of max profit (50%, 70%)
    bool use_breakeven_lock = false;
    double breakeven_profit = 0.3;  // Lock at breakeven once +X in profit

    // 3. Partial Take Profit
    bool use_partial_tp = false;
    double partial_tp_pct = 0.0;    // Close X% at TP/2
    double partial_tp_level = 0.0;  // Level to close partial (0.5 = TP/2)
    // For 3-tier: use two separate partial configs
    bool use_three_tier = false;

    // 4. Time-based Exit
    bool use_time_exit = false;
    int time_exit_ticks = 0;       // Close after X ticks
    bool time_exit_profitable_only = false;

    // 5. Trend Reversal Exit
    bool use_trend_reversal_exit = false;
    double trend_reversal_close_pct = 1.0;  // 1.0 = close all, 0.5 = close 50%

    // 6. Aggressive DD Exit
    // Note: close_all is the main DD parameter, these are more aggressive versions
    bool use_aggressive_dd = false;
    double aggressive_dd_threshold = 25.0;  // 15%, 20% instead of 25%
};

struct Result {
    double return_pct;
    double max_dd;
    double risk_adjusted;
    int total_trades;
    double avg_trade;
    double sharpe_ratio;
};

// ===========================
// HELPER STRUCTURES
// ===========================

// Extended Trade structure with exit tracking
struct ExtendedTrade : public Trade {
    int entry_tick_index = 0;
    double max_profit = 0.0;
    double trailing_stop = 0.0;
    bool breakeven_locked = false;
    bool partial_closed = false;
};

// Simple Moving Average
struct SMA {
    std::deque<double> prices;
    int period;
    double sum;

    SMA(int p) : period(p), sum(0.0) {}

    void Add(double price) {
        prices.push_back(price);
        sum += price;

        if ((int)prices.size() > period) {
            sum -= prices.front();
            prices.pop_front();
        }
    }

    double Get() const {
        if (prices.size() < (size_t)period) return 0.0;
        return sum / period;
    }

    bool IsReady() const {
        return prices.size() >= (size_t)period;
    }

    double GetPrev() const {
        if (prices.size() < 2) return Get();
        // Calculate SMA without the last price
        return (sum - prices.back()) / (prices.size() - 1);
    }
};

// ===========================
// TEST ENGINE
// ===========================

Result RunTest(const std::vector<Tick>& ticks, const ExitConfig& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0, 0, 0, 0};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<ExtendedTrade*> positions;
    std::vector<double> trade_returns;  // For Sharpe calculation
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // Initialize SMA
    SMA sma(cfg.sma_period);
    double prev_sma = 0.0;
    bool price_was_above_sma = false;

    for (size_t tick_idx = 0; tick_idx < ticks.size(); tick_idx++) {
        const Tick& tick = ticks[tick_idx];

        // Update SMA
        prev_sma = sma.Get();
        sma.Add(tick.bid);
        double current_sma = sma.Get();

        // Calculate equity
        equity = balance;
        for (ExtendedTrade* t : positions) {
            double unrealized_pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
            equity += unrealized_pl;

            // Track max profit for trailing stop
            if (unrealized_pl > t->max_profit) {
                t->max_profit = unrealized_pl;
            }
        }

        // Update peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_dd = std::max(result.max_dd, dd_pct);

        // Aggressive DD Exit (if enabled)
        double effective_close_all = cfg.use_aggressive_dd ? cfg.aggressive_dd_threshold : cfg.close_all;

        // Close ALL
        if (dd_pct > effective_close_all && !all_closed && !positions.empty()) {
            for (ExtendedTrade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                trade_returns.push_back(pl);
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // Partial close (close worst 50% of positions)
        if (dd_pct > cfg.partial_close && !partial_done && positions.size() > 1) {
            // Sort by unrealized P/L
            std::sort(positions.begin(), positions.end(), [&](ExtendedTrade* a, ExtendedTrade* b) {
                double pl_a = (tick.bid - a->entry_price);
                double pl_b = (tick.bid - b->entry_price);
                return pl_a < pl_b;
            });

            int to_close = (int)(positions.size() * cfg.partial_pct);
            to_close = std::max(1, to_close);

            for (int i = 0; i < to_close && i < (int)positions.size(); i++) {
                ExtendedTrade* t = positions[i];
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                trade_returns.push_back(pl);
                delete t;
            }
            positions.erase(positions.begin(), positions.begin() + to_close);
            partial_done = true;
        }

        // Stop out check
        double used_margin = 0;
        for (ExtendedTrade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            for (ExtendedTrade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                trade_returns.push_back(pl);
                delete t;
            }
            positions.clear();
            break;
        }

        // Trend Reversal Exit (price crosses below SMA)
        if (cfg.use_trend_reversal_exit && sma.IsReady() && !positions.empty()) {
            // Detect crossover: was above, now below
            if (price_was_above_sma && tick.bid < current_sma) {
                int to_close = (int)(positions.size() * cfg.trend_reversal_close_pct);
                to_close = std::max(1, to_close);

                // Close worst positions first
                std::sort(positions.begin(), positions.end(), [&](ExtendedTrade* a, ExtendedTrade* b) {
                    double pl_a = (tick.bid - a->entry_price);
                    double pl_b = (tick.bid - b->entry_price);
                    return pl_a < pl_b;
                });

                for (int i = 0; i < to_close && i < (int)positions.size(); i++) {
                    ExtendedTrade* t = positions[i];
                    double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    balance += pl;
                    trade_returns.push_back(pl);
                    result.total_trades++;
                    delete t;
                }
                positions.erase(positions.begin(), positions.begin() + to_close);
            }
        }

        if (sma.IsReady()) {
            price_was_above_sma = (tick.bid > current_sma);
        }

        // Process exits for each position
        for (auto it = positions.begin(); it != positions.end();) {
            ExtendedTrade* t = *it;
            bool should_close = false;
            std::string exit_reason = "TP";

            // Calculate current P/L
            double current_pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
            double price_diff = tick.bid - t->entry_price;

            // Time-based Exit
            if (cfg.use_time_exit && (tick_idx - t->entry_tick_index) >= (size_t)cfg.time_exit_ticks) {
                if (!cfg.time_exit_profitable_only || current_pl > 0) {
                    should_close = true;
                    exit_reason = "Time";
                }
            }

            // Breakeven Lock
            if (cfg.use_breakeven_lock && !t->breakeven_locked && price_diff >= cfg.breakeven_profit) {
                t->breakeven_locked = true;
                t->trailing_stop = t->entry_price;  // Lock at breakeven
            }

            // Trailing Stop
            if (cfg.use_trailing_stop && current_pl > 0) {
                double trail_level = t->max_profit * (1.0 - cfg.trail_percent / 100.0);
                if (current_pl < trail_level) {
                    should_close = true;
                    exit_reason = "Trail";
                }
            }

            // Breakeven Stop Hit
            if (t->breakeven_locked && tick.bid <= t->trailing_stop) {
                should_close = true;
                exit_reason = "Breakeven";
            }

            // Partial TP (close half at TP/2, for example)
            if (cfg.use_partial_tp && !t->partial_closed) {
                double partial_tp_price = t->entry_price + (spacing * cfg.tp_multiplier * cfg.partial_tp_level);
                if (tick.bid >= partial_tp_price) {
                    // Close partial position
                    double partial_lot = t->lot_size * cfg.partial_tp_pct;
                    double remaining_lot = t->lot_size - partial_lot;

                    double partial_pl = (tick.bid - t->entry_price) * partial_lot * contract_size;
                    balance += partial_pl;
                    trade_returns.push_back(partial_pl);
                    result.total_trades++;

                    t->lot_size = remaining_lot;
                    t->partial_closed = true;
                }
            }

            // Three-tier TP
            if (cfg.use_three_tier && !t->partial_closed) {
                double tier1_price = t->entry_price + (spacing * cfg.tp_multiplier * 0.33);
                double tier2_price = t->entry_price + (spacing * cfg.tp_multiplier * 0.67);

                if (tick.bid >= tier1_price && t->lot_size >= 0.03) {
                    // Close 1/3
                    double close_lot = t->lot_size / 3.0;
                    double pl = (tick.bid - t->entry_price) * close_lot * contract_size;
                    balance += pl;
                    trade_returns.push_back(pl);
                    result.total_trades++;
                    t->lot_size -= close_lot;
                } else if (tick.bid >= tier2_price && t->lot_size >= 0.02) {
                    // Close another 1/3
                    double close_lot = t->lot_size / 2.0;  // Half of remaining
                    double pl = (tick.bid - t->entry_price) * close_lot * contract_size;
                    balance += pl;
                    trade_returns.push_back(pl);
                    result.total_trades++;
                    t->lot_size -= close_lot;
                    t->partial_closed = true;
                }
            }

            // Standard TP check
            double tp_price = t->entry_price + (spacing * cfg.tp_multiplier);
            if (tick.bid >= tp_price) {
                should_close = true;
                exit_reason = "TP";
            }

            // Close position if needed
            if (should_close) {
                double final_pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += final_pl;
                trade_returns.push_back(final_pl);
                result.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Open new position logic (V5 SMA strategy)
        if (dd_pct < cfg.stop_new && sma.IsReady() && (int)positions.size() < cfg.max_positions) {
            double ma_value = sma.Get();

            // Only trade if price is above SMA (uptrend)
            if (tick.bid > ma_value) {
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (ExtendedTrade* t : positions) {
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
                        ExtendedTrade* t = new ExtendedTrade();
                        t->id = next_id++;
                        t->direction = "BUY";
                        t->entry_price = tick.ask;
                        t->lot_size = lot;
                        t->take_profit = tick.ask + tick.spread() + (spacing * cfg.tp_multiplier);
                        t->entry_tick_index = tick_idx;
                        t->max_profit = 0.0;
                        t->trailing_stop = 0.0;
                        t->breakeven_locked = false;
                        t->partial_closed = false;
                        positions.push_back(t);
                    }
                }
            }
        }
    }

    // Close remaining positions
    if (!ticks.empty()) {
        const Tick& last_tick = ticks.back();
        for (ExtendedTrade* t : positions) {
            double pl = (last_tick.bid - t->entry_price) * t->lot_size * contract_size;
            balance += pl;
            trade_returns.push_back(pl);
            delete t;
        }
        positions.clear();
    }

    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    result.risk_adjusted = result.return_pct / std::max(1.0, result.max_dd);

    // Calculate average trade return
    if (!trade_returns.empty()) {
        double sum = 0;
        for (double r : trade_returns) {
            sum += r;
        }
        result.avg_trade = sum / trade_returns.size();

        // Calculate Sharpe ratio (simplified)
        double mean = result.avg_trade;
        double variance = 0;
        for (double r : trade_returns) {
            variance += (r - mean) * (r - mean);
        }
        double std_dev = std::sqrt(variance / trade_returns.size());
        result.sharpe_ratio = (std_dev > 0) ? (mean / std_dev) : 0.0;
    }

    return result;
}

// ===========================
// DATA LOADING
// ===========================

std::vector<Tick> LoadTicks(const std::string& filename, size_t start_line, size_t num_lines) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open file: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    size_t current_line = 0;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line) && ticks.size() < num_lines) {
        current_line++;
        if (current_line < start_line) continue;

        std::stringstream ss(line);
        std::string timestamp, bid_str, ask_str;
        std::getline(ss, timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        try {
            if (bid_str.empty() || ask_str.empty()) continue;

            Tick tick;
            tick.timestamp = timestamp;
            tick.bid = std::stod(bid_str);
            tick.ask = std::stod(ask_str);

            if (tick.bid <= 0 || tick.ask <= 0 || tick.ask < tick.bid) {
                continue;
            }

            ticks.push_back(tick);
        } catch (...) {
            continue;
        }
    }

    file.close();
    return ticks;
}

// ===========================
// MAIN
// ===========================

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     V5 EXIT STRATEGY OPTIMIZATION TEST" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::cout << "BASELINE: V5 SMA 11000 (stop_new: 5%, partial: 8%, close_all: 25%)" << std::endl;
    std::cout << std::endl;

    std::cout << "TEST CATEGORIES:" << std::endl;
    std::cout << "1. Dynamic Take Profit (0.5x - 2.0x spacing)" << std::endl;
    std::cout << "2. Trailing Stop (50%, 70% of max profit)" << std::endl;
    std::cout << "3. Partial Take Profit (50% at TP/2, 3-tier)" << std::endl;
    std::cout << "4. Time-based Exit (10k, 50k ticks)" << std::endl;
    std::cout << "5. Trend Reversal Exit (close when price < SMA)" << std::endl;
    std::cout << "6. Aggressive DD Exit (15%, 20% vs 25%)" << std::endl;
    std::cout << std::endl;

    // Define test periods (same as V5 SMA test)
    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Jan",         1, 500000},
        {"Apr",         8000000, 500000},
        {"Jun",         12000000, 500000},
        {"Oct",         20000000, 500000},
        {"Dec Pre",     50000000, 1500000},
        {"Dec Crash",   51314023, 2000000},
    };

    std::cout << "Loading data for " << periods.size() << " test periods..." << std::endl;
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        std::cout << "  Loading " << p.name << " (" << p.count << " ticks)..." << std::endl;
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
        if (!ticks.empty()) {
            std::cout << "    Loaded " << ticks.size() << " ticks" << std::endl;
        }
    }
    std::cout << std::endl;

    // Check if we have valid data
    bool has_valid_data = false;
    for (const auto& ticks : all_ticks) {
        if (!ticks.empty()) {
            has_valid_data = true;
            break;
        }
    }

    if (!has_valid_data) {
        std::cerr << "ERROR: No valid tick data loaded. Cannot run tests." << std::endl;
        return 1;
    }

    auto RunPhase = [&](const std::vector<ExitConfig>& configs, const std::string& phase_name) {
        std::cout << "\n================================================================" << std::endl;
        std::cout << phase_name << std::endl;
        std::cout << "================================================================" << std::endl;

        // Header
        std::cout << "\n" << std::left << std::setw(25) << "Strategy";
        for (const auto& p : periods) {
            std::cout << " | " << std::setw(7) << p.name.substr(0, 7);
        }
        std::cout << " | " << std::setw(8) << "TOTAL" << " | " << std::setw(8) << "MaxDD"
                  << " | " << std::setw(6) << "R/A" << " | " << std::setw(6) << "Trades" << std::endl;
        std::cout << std::string(25 + periods.size() * 10 + 40, '-') << std::endl;

        std::vector<std::tuple<std::string, double, double, double, int>> summary;

        for (const auto& cfg : configs) {
            std::cout << std::left << std::setw(25) << cfg.name;
            double total = 0;
            double worst_dd = 0;
            int total_trades = 0;

            for (size_t i = 0; i < periods.size(); i++) {
                if (all_ticks[i].empty()) {
                    std::cout << " | " << std::right << std::setw(7) << "N/A";
                    continue;
                }

                Result r = RunTest(all_ticks[i], cfg);
                total += r.return_pct;
                worst_dd = std::max(worst_dd, r.max_dd);
                total_trades += r.total_trades;

                std::cout << std::fixed << std::setprecision(1);
                std::cout << " | " << std::right << std::setw(6) << r.return_pct << "%";
            }

            double risk_adj = total / std::max(1.0, worst_dd);

            std::cout << " | " << std::right << std::setw(7) << total << "%"
                      << " | " << std::setw(7) << worst_dd << "%"
                      << " | " << std::setw(6) << std::setprecision(3) << risk_adj
                      << " | " << std::setw(6) << total_trades << std::endl;

            summary.push_back({cfg.name, total, worst_dd, risk_adj, total_trades});
        }

        std::cout << std::string(25 + periods.size() * 10 + 40, '-') << std::endl;

        // Results summary
        std::cout << "\nSUMMARY (Sorted by Risk-Adjusted Return):" << std::endl;
        std::cout << std::string(95, '-') << std::endl;

        // Sort by risk-adjusted return
        std::sort(summary.begin(), summary.end(),
                  [](const auto& a, const auto& b) { return std::get<3>(a) > std::get<3>(b); });

        for (size_t i = 0; i < summary.size(); i++) {
            const auto& [name, total, dd, ra, trades] = summary[i];
            std::cout << std::right << std::setw(2) << (i + 1) << ". "
                      << std::left << std::setw(25) << name
                      << " | Return: " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << total << "%"
                      << " | MaxDD: " << std::setw(5) << dd << "%"
                      << " | R/A: " << std::setw(7) << std::setprecision(3) << ra
                      << " | Trades: " << std::setw(6) << trades << std::endl;
        }

        return summary;
    };

    // ============================================================
    // PHASE 1: Dynamic Take Profit
    // ============================================================
    std::vector<ExitConfig> phase1_configs;

    ExitConfig cfg_base1 = ExitConfig();
    cfg_base1.name = "Baseline (TP = 1.0x)";
    cfg_base1.tp_multiplier = 1.0;
    phase1_configs.push_back(cfg_base1);

    ExitConfig cfg_tp_05 = ExitConfig();
    cfg_tp_05.name = "Tight TP (0.5x)";
    cfg_tp_05.tp_multiplier = 0.5;
    phase1_configs.push_back(cfg_tp_05);

    ExitConfig cfg_tp_15 = ExitConfig();
    cfg_tp_15.name = "Wider TP (1.5x)";
    cfg_tp_15.tp_multiplier = 1.5;
    phase1_configs.push_back(cfg_tp_15);

    ExitConfig cfg_tp_20 = ExitConfig();
    cfg_tp_20.name = "Much Wider TP (2.0x)";
    cfg_tp_20.tp_multiplier = 2.0;
    phase1_configs.push_back(cfg_tp_20);

    auto phase1_results = RunPhase(phase1_configs, "PHASE 1: DYNAMIC TAKE PROFIT");

    // ============================================================
    // PHASE 2: Trailing Stop
    // ============================================================
    std::vector<ExitConfig> phase2_configs;

    ExitConfig cfg_base2 = ExitConfig();
    cfg_base2.name = "Baseline (No Trail)";
    phase2_configs.push_back(cfg_base2);

    ExitConfig cfg_trail50 = ExitConfig();
    cfg_trail50.name = "Trail 50% Max Profit";
    cfg_trail50.use_trailing_stop = true;
    cfg_trail50.trail_percent = 50.0;
    phase2_configs.push_back(cfg_trail50);

    ExitConfig cfg_trail70 = ExitConfig();
    cfg_trail70.name = "Trail 70% Max Profit";
    cfg_trail70.use_trailing_stop = true;
    cfg_trail70.trail_percent = 70.0;
    phase2_configs.push_back(cfg_trail70);

    ExitConfig cfg_breakeven = ExitConfig();
    cfg_breakeven.name = "Breakeven Lock +0.3";
    cfg_breakeven.use_breakeven_lock = true;
    cfg_breakeven.breakeven_profit = 0.3;
    phase2_configs.push_back(cfg_breakeven);

    auto phase2_results = RunPhase(phase2_configs, "PHASE 2: TRAILING STOP");

    // ============================================================
    // PHASE 3: Partial Take Profit
    // ============================================================
    std::vector<ExitConfig> phase3_configs;

    ExitConfig cfg_base3 = ExitConfig();
    cfg_base3.name = "Baseline (Full TP)";
    phase3_configs.push_back(cfg_base3);

    ExitConfig cfg_partial50 = ExitConfig();
    cfg_partial50.name = "50% at TP/2";
    cfg_partial50.use_partial_tp = true;
    cfg_partial50.partial_tp_pct = 0.5;
    cfg_partial50.partial_tp_level = 0.5;
    phase3_configs.push_back(cfg_partial50);

    ExitConfig cfg_three_tier = ExitConfig();
    cfg_three_tier.name = "3-Tier (33% each)";
    cfg_three_tier.use_three_tier = true;
    phase3_configs.push_back(cfg_three_tier);

    auto phase3_results = RunPhase(phase3_configs, "PHASE 3: PARTIAL TAKE PROFIT");

    // ============================================================
    // PHASE 4: Time-based Exit
    // ============================================================
    std::vector<ExitConfig> phase4_configs;

    ExitConfig cfg_base4 = ExitConfig();
    cfg_base4.name = "Baseline (No Time Exit)";
    phase4_configs.push_back(cfg_base4);

    ExitConfig cfg_time10k_profit = ExitConfig();
    cfg_time10k_profit.name = "Close @ 10k ticks (profit)";
    cfg_time10k_profit.use_time_exit = true;
    cfg_time10k_profit.time_exit_ticks = 10000;
    cfg_time10k_profit.time_exit_profitable_only = true;
    phase4_configs.push_back(cfg_time10k_profit);

    ExitConfig cfg_time50k_all = ExitConfig();
    cfg_time50k_all.name = "Close @ 50k ticks (all)";
    cfg_time50k_all.use_time_exit = true;
    cfg_time50k_all.time_exit_ticks = 50000;
    cfg_time50k_all.time_exit_profitable_only = false;
    phase4_configs.push_back(cfg_time50k_all);

    auto phase4_results = RunPhase(phase4_configs, "PHASE 4: TIME-BASED EXIT");

    // ============================================================
    // PHASE 5: Trend Reversal Exit
    // ============================================================
    std::vector<ExitConfig> phase5_configs;

    ExitConfig cfg_base5 = ExitConfig();
    cfg_base5.name = "Baseline (No Trend Exit)";
    phase5_configs.push_back(cfg_base5);

    ExitConfig cfg_trend_all = ExitConfig();
    cfg_trend_all.name = "Close ALL @ SMA Cross";
    cfg_trend_all.use_trend_reversal_exit = true;
    cfg_trend_all.trend_reversal_close_pct = 1.0;
    phase5_configs.push_back(cfg_trend_all);

    ExitConfig cfg_trend_50 = ExitConfig();
    cfg_trend_50.name = "Close 50% @ SMA Cross";
    cfg_trend_50.use_trend_reversal_exit = true;
    cfg_trend_50.trend_reversal_close_pct = 0.5;
    phase5_configs.push_back(cfg_trend_50);

    auto phase5_results = RunPhase(phase5_configs, "PHASE 5: TREND REVERSAL EXIT");

    // ============================================================
    // PHASE 6: Aggressive DD Exit
    // ============================================================
    std::vector<ExitConfig> phase6_configs;

    ExitConfig cfg_base6 = ExitConfig();
    cfg_base6.name = "Baseline (25% DD)";
    phase6_configs.push_back(cfg_base6);

    ExitConfig cfg_dd20 = ExitConfig();
    cfg_dd20.name = "Aggressive 20% DD";
    cfg_dd20.use_aggressive_dd = true;
    cfg_dd20.aggressive_dd_threshold = 20.0;
    phase6_configs.push_back(cfg_dd20);

    ExitConfig cfg_dd15 = ExitConfig();
    cfg_dd15.name = "Very Aggressive 15% DD";
    cfg_dd15.use_aggressive_dd = true;
    cfg_dd15.aggressive_dd_threshold = 15.0;
    phase6_configs.push_back(cfg_dd15);

    auto phase6_results = RunPhase(phase6_configs, "PHASE 6: AGGRESSIVE DD EXIT");

    // ============================================================
    // FINAL RECOMMENDATIONS
    // ============================================================
    std::cout << "\n================================================================" << std::endl;
    std::cout << "FINAL RECOMMENDATIONS" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::cout << "\nBEST DYNAMIC TP: " << std::get<0>(phase1_results[0]) << std::endl;
    std::cout << "  Risk-Adjusted: " << std::fixed << std::setprecision(3) << std::get<3>(phase1_results[0]) << std::endl;

    std::cout << "\nBEST TRAILING STOP: " << std::get<0>(phase2_results[0]) << std::endl;
    std::cout << "  Risk-Adjusted: " << std::fixed << std::setprecision(3) << std::get<3>(phase2_results[0]) << std::endl;

    std::cout << "\nBEST PARTIAL TP: " << std::get<0>(phase3_results[0]) << std::endl;
    std::cout << "  Risk-Adjusted: " << std::fixed << std::setprecision(3) << std::get<3>(phase3_results[0]) << std::endl;

    std::cout << "\nBEST TIME EXIT: " << std::get<0>(phase4_results[0]) << std::endl;
    std::cout << "  Risk-Adjusted: " << std::fixed << std::setprecision(3) << std::get<3>(phase4_results[0]) << std::endl;

    std::cout << "\nBEST TREND REVERSAL: " << std::get<0>(phase5_results[0]) << std::endl;
    std::cout << "  Risk-Adjusted: " << std::fixed << std::setprecision(3) << std::get<3>(phase5_results[0]) << std::endl;

    std::cout << "\nBEST DD STRATEGY: " << std::get<0>(phase6_results[0]) << std::endl;
    std::cout << "  Risk-Adjusted: " << std::fixed << std::setprecision(3) << std::get<3>(phase6_results[0]) << std::endl;

    // Compare improvements
    double baseline_ra = std::get<3>(phase1_results[0]);

    std::cout << "\n----------------------------------------------------------------" << std::endl;
    std::cout << "IMPROVEMENT vs BASELINE:" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    auto ShowImprovement = [&](const std::string& category, double new_ra) {
        double improvement = ((new_ra - baseline_ra) / baseline_ra) * 100.0;
        std::cout << std::left << std::setw(25) << category << ": "
                  << std::showpos << std::fixed << std::setprecision(1)
                  << improvement << "%" << std::noshowpos << " improvement" << std::endl;
    };

    ShowImprovement("Dynamic TP", std::get<3>(phase1_results[0]));
    ShowImprovement("Trailing Stop", std::get<3>(phase2_results[0]));
    ShowImprovement("Partial TP", std::get<3>(phase3_results[0]));
    ShowImprovement("Time Exit", std::get<3>(phase4_results[0]));
    ShowImprovement("Trend Reversal", std::get<3>(phase5_results[0]));
    ShowImprovement("Aggressive DD", std::get<3>(phase6_results[0]));

    std::cout << "\n================================================================" << std::endl;
    std::cout << "TEST COMPLETE" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
