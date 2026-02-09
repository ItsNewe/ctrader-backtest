/**
 * V7 Parameter Sweep Test
 *
 * Tests various combinations of V7 volatility filter parameters:
 * - ATR short period (recent volatility window)
 * - ATR long period (baseline volatility window)
 * - Volatility threshold (when to allow trading)
 * - TP multiplier (take profit width)
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdlib>
#include <cstdio>

struct Tick {
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
};

struct V7Config {
    // Volatility filter params
    int atr_short_period;
    int atr_long_period;
    double volatility_threshold;

    // TP multiplier
    double tp_multiplier;

    // Fixed V3 protection (don't sweep these)
    double stop_new_at_dd = 5.0;
    double partial_close_at_dd = 8.0;
    double close_all_at_dd = 25.0;
    int max_positions = 20;
};

struct Result {
    double return_pct;
    double max_dd;
    int trades_completed;
    int positions_opened;
    double avg_trade_profit;
    double time_in_market_pct;  // How often volatility filter allowed trading
};

// ATR calculator
class ATR {
    std::deque<double> ranges;
    int period;
    double sum = 0;
    double last_price = 0;
public:
    ATR(int p) : period(p) {}

    void Add(double price) {
        if (last_price > 0) {
            double range = std::abs(price - last_price);
            ranges.push_back(range);
            sum += range;
            if ((int)ranges.size() > period) {
                sum -= ranges.front();
                ranges.pop_front();
            }
        }
        last_price = price;
    }

    double Get() const {
        return ranges.empty() ? 0 : sum / ranges.size();
    }

    bool IsReady() const {
        return (int)ranges.size() >= period;
    }
};

Result RunTest(const std::vector<Tick>& ticks, const V7Config& cfg) {
    Result r = {0, 0, 0, 0, 0, 0};
    if (ticks.empty()) return r;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);

    int volatility_allowed_count = 0;
    int volatility_check_count = 0;
    double total_trade_profit = 0;

    for (const Tick& tick : ticks) {
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Peak equity reset when no positions
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        r.max_dd = std::max(r.max_dd, dd_pct);

        // V3 Protection: Close ALL
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double profit = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += profit;
                total_trade_profit += profit;
                r.trades_completed++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                double profit = (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                balance += profit;
                total_trade_profit += profit;
                r.trades_completed++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // TP check
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double profit = (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                balance += profit;
                total_trade_profit += profit;
                r.trades_completed++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Check volatility filter
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_check_count++;
            volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
            if (volatility_ok) volatility_allowed_count++;
        }

        // Open new positions
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }

        if (dd_pct < cfg.stop_new_at_dd && volatility_ok && (int)positions.size() < cfg.max_positions) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
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
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + (spacing * cfg.tp_multiplier);
                    positions.push_back(t);
                    r.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions
    for (Trade* t : positions) {
        double profit = (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        balance += profit;
        total_trade_profit += profit;
        r.trades_completed++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.avg_trade_profit = (r.trades_completed > 0) ? total_trade_profit / r.trades_completed : 0;
    r.time_in_market_pct = (volatility_check_count > 0) ?
                           (double)volatility_allowed_count / volatility_check_count * 100.0 : 100.0;

    return r;
}

std::vector<Tick> LoadTicks(const std::string& filename, size_t max_ticks) {
    std::vector<Tick> ticks;

    FILE* file = fopen(filename.c_str(), "r");
    if (!file) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return ticks;
    }

    char line[512];

    // Skip header
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    while (fgets(line, sizeof(line), file) && ticks.size() < max_ticks) {
        char ts[64];
        double bid, ask;

        // Parse tab-separated: timestamp\tbid\task\t...
        if (sscanf(line, "%63[^\t]\t%lf\t%lf", ts, &bid, &ask) >= 3) {
            if (bid > 0 && ask > 0) {
                Tick tick;
                tick.bid = bid;
                tick.ask = ask;
                ticks.push_back(tick);
            }
        }

        if (ticks.size() % 50000 == 0 && ticks.size() > 0) {
            std::cout << "  Loaded " << ticks.size() << " ticks..." << std::endl;
        }
    }

    fclose(file);
    return ticks;
}

// Generate synthetic tick data for testing
std::vector<Tick> GenerateSyntheticTicks(size_t count, double start_price = 2600.0, double spread = 0.25) {
    std::vector<Tick> ticks;
    ticks.reserve(count);

    double price = start_price;
    double volatility = 0.1;  // Base volatility

    for (size_t i = 0; i < count; i++) {
        // Create varying volatility periods
        if (i % 10000 < 3000) {
            volatility = 0.05;  // Low volatility period
        } else if (i % 10000 < 6000) {
            volatility = 0.15;  // Medium volatility
        } else {
            volatility = 0.25;  // High volatility
        }

        // Random walk with occasional trends
        double move = ((double)(rand() % 1000) / 1000.0 - 0.5) * volatility;

        // Add slight upward bias (gold generally trends up)
        move += 0.001;

        price += move;

        Tick tick;
        tick.bid = price;
        tick.ask = price + spread;
        ticks.push_back(tick);
    }

    return ticks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     V7 PARAMETER SWEEP - VOLATILITY FILTER OPTIMIZATION" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Try to load real tick data, fall back to synthetic
    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";
    std::cout << "Loading tick data from: " << filename << std::endl;

    std::vector<Tick> ticks = LoadTicks(filename, 500000);  // 500K ticks for better accuracy

    if (ticks.empty()) {
        std::cout << "File load failed. Using synthetic data instead..." << std::endl;
        srand(42);
        ticks = GenerateSyntheticTicks(100000);
    }

    if (ticks.empty()) {
        std::cerr << "Failed to load or generate ticks!" << std::endl;
        return 1;
    }

    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    std::cout << "Price range: " << ticks.front().bid << " -> " << ticks.back().bid << std::endl;
    std::cout << std::endl;

    // Parameter ranges to sweep
    std::vector<int> atr_short_vals = {50, 100, 150, 200};
    std::vector<int> atr_long_vals = {300, 500, 750, 1000};
    std::vector<double> threshold_vals = {0.6, 0.7, 0.8, 0.9, 1.0, 1.1};
    std::vector<double> tp_mult_vals = {1.0, 1.5, 2.0};

    struct ConfigResult {
        V7Config config;
        Result result;
        double score;
    };

    std::vector<ConfigResult> all_results;

    int total_combos = atr_short_vals.size() * atr_long_vals.size() *
                       threshold_vals.size() * tp_mult_vals.size();
    int combo = 0;

    std::cout << "Testing " << total_combos << " parameter combinations..." << std::endl;
    std::cout << std::endl;

    for (int atr_short : atr_short_vals) {
        for (int atr_long : atr_long_vals) {
            if (atr_long <= atr_short * 2) continue;  // Long should be significantly longer

            for (double threshold : threshold_vals) {
                for (double tp_mult : tp_mult_vals) {
                    V7Config cfg;
                    cfg.atr_short_period = atr_short;
                    cfg.atr_long_period = atr_long;
                    cfg.volatility_threshold = threshold;
                    cfg.tp_multiplier = tp_mult;

                    Result r = RunTest(ticks, cfg);

                    // Calculate composite score:
                    // - Higher return is better
                    // - Lower max_dd is better
                    // - Risk-adjusted return
                    double risk_adj = (r.max_dd > 0) ? r.return_pct / r.max_dd : r.return_pct;
                    double score = risk_adj * 0.5 + r.return_pct * 0.3 + (100.0 - r.max_dd) * 0.2;

                    all_results.push_back({cfg, r, score});
                    combo++;

                    if (combo % 20 == 0 || combo == total_combos) {
                        std::cout << "\r  Progress: " << combo << "/" << total_combos << " combinations tested" << std::flush;
                    }
                }
            }
        }
    }

    std::cout << std::endl << std::endl;
    std::cout << "Tested " << all_results.size() << " valid combinations" << std::endl;
    std::cout << std::endl;

    // Sort by composite score
    std::sort(all_results.begin(), all_results.end(),
              [](const ConfigResult& a, const ConfigResult& b) { return a.score > b.score; });

    // Show top 15 results
    std::cout << "TOP 15 CONFIGURATIONS BY COMPOSITE SCORE:" << std::endl;
    std::cout << std::string(110, '=') << std::endl;
    std::cout << std::left
              << std::setw(6) << "Short"
              << std::setw(6) << "Long"
              << std::setw(8) << "Thresh"
              << std::setw(6) << "TP"
              << " | "
              << std::right
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Opened"
              << std::setw(12) << "InMarket%"
              << std::setw(10) << "Score"
              << std::endl;
    std::cout << std::string(110, '-') << std::endl;

    for (int i = 0; i < std::min(15, (int)all_results.size()); i++) {
        const auto& cr = all_results[i];
        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left
                  << std::setw(6) << cr.config.atr_short_period
                  << std::setw(6) << cr.config.atr_long_period
                  << std::setw(8) << cr.config.volatility_threshold
                  << std::setw(6) << cr.config.tp_multiplier
                  << " | "
                  << std::right
                  << std::setw(8) << cr.result.return_pct << "%"
                  << std::setw(8) << cr.result.max_dd << "%"
                  << std::setw(8) << cr.result.trades_completed
                  << std::setw(10) << cr.result.positions_opened
                  << std::setw(10) << cr.result.time_in_market_pct << "%"
                  << std::setw(10) << cr.score
                  << std::endl;
    }

    std::cout << std::string(110, '=') << std::endl;
    std::cout << std::endl;

    // Sort by return only
    std::sort(all_results.begin(), all_results.end(),
              [](const ConfigResult& a, const ConfigResult& b) {
                  return a.result.return_pct > b.result.return_pct;
              });

    std::cout << "TOP 10 BY RETURN ONLY:" << std::endl;
    std::cout << std::string(110, '-') << std::endl;

    for (int i = 0; i < std::min(10, (int)all_results.size()); i++) {
        const auto& cr = all_results[i];
        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left
                  << std::setw(6) << cr.config.atr_short_period
                  << std::setw(6) << cr.config.atr_long_period
                  << std::setw(8) << cr.config.volatility_threshold
                  << std::setw(6) << cr.config.tp_multiplier
                  << " | "
                  << std::right
                  << std::setw(8) << cr.result.return_pct << "%"
                  << std::setw(8) << cr.result.max_dd << "%"
                  << std::setw(8) << cr.result.trades_completed
                  << std::setw(10) << cr.result.positions_opened
                  << std::setw(10) << cr.result.time_in_market_pct << "%"
                  << std::setw(10) << cr.score
                  << std::endl;
    }

    std::cout << std::string(110, '-') << std::endl;
    std::cout << std::endl;

    // Sort by lowest DD (best risk)
    std::sort(all_results.begin(), all_results.end(),
              [](const ConfigResult& a, const ConfigResult& b) {
                  // Filter out negative returns, then sort by lowest DD
                  if (a.result.return_pct <= 0 && b.result.return_pct > 0) return false;
                  if (a.result.return_pct > 0 && b.result.return_pct <= 0) return true;
                  return a.result.max_dd < b.result.max_dd;
              });

    std::cout << "TOP 10 BY LOWEST MAX DRAWDOWN (profitable only):" << std::endl;
    std::cout << std::string(110, '-') << std::endl;

    int count = 0;
    for (const auto& cr : all_results) {
        if (cr.result.return_pct <= 0) continue;
        if (count >= 10) break;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left
                  << std::setw(6) << cr.config.atr_short_period
                  << std::setw(6) << cr.config.atr_long_period
                  << std::setw(8) << cr.config.volatility_threshold
                  << std::setw(6) << cr.config.tp_multiplier
                  << " | "
                  << std::right
                  << std::setw(8) << cr.result.return_pct << "%"
                  << std::setw(8) << cr.result.max_dd << "%"
                  << std::setw(8) << cr.result.trades_completed
                  << std::setw(10) << cr.result.positions_opened
                  << std::setw(10) << cr.result.time_in_market_pct << "%"
                  << std::setw(10) << cr.score
                  << std::endl;
        count++;
    }

    std::cout << std::string(110, '=') << std::endl;
    std::cout << std::endl;

    // Summary statistics
    int profitable = 0;
    double best_return = -9999, worst_return = 9999;
    double best_dd = 9999, worst_dd = 0;

    for (const auto& cr : all_results) {
        if (cr.result.return_pct > 0) profitable++;
        best_return = std::max(best_return, cr.result.return_pct);
        worst_return = std::min(worst_return, cr.result.return_pct);
        best_dd = std::min(best_dd, cr.result.max_dd);
        worst_dd = std::max(worst_dd, cr.result.max_dd);
    }

    std::cout << "SUMMARY STATISTICS:" << std::endl;
    std::cout << "  Total configs tested: " << all_results.size() << std::endl;
    std::cout << "  Profitable configs:   " << profitable << " ("
              << std::fixed << std::setprecision(1)
              << (100.0 * profitable / all_results.size()) << "%)" << std::endl;
    std::cout << "  Return range:         " << worst_return << "% to " << best_return << "%" << std::endl;
    std::cout << "  Max DD range:         " << best_dd << "% to " << worst_dd << "%" << std::endl;
    std::cout << std::endl;

    // Best config recommendation
    std::sort(all_results.begin(), all_results.end(),
              [](const ConfigResult& a, const ConfigResult& b) { return a.score > b.score; });

    const auto& best = all_results[0];
    std::cout << "RECOMMENDED PARAMETERS:" << std::endl;
    std::cout << "  ATR Short Period:     " << best.config.atr_short_period << std::endl;
    std::cout << "  ATR Long Period:      " << best.config.atr_long_period << std::endl;
    std::cout << "  Volatility Threshold: " << best.config.volatility_threshold << std::endl;
    std::cout << "  TP Multiplier:        " << best.config.tp_multiplier << std::endl;
    std::cout << "  Expected Return:      " << best.result.return_pct << "%" << std::endl;
    std::cout << "  Expected Max DD:      " << best.result.max_dd << "%" << std::endl;
    std::cout << std::endl;

    // Compare with current V7 defaults
    std::cout << "COMPARISON WITH CURRENT V7 DEFAULTS (100/500/0.8/1.0):" << std::endl;
    V7Config default_cfg;
    default_cfg.atr_short_period = 100;
    default_cfg.atr_long_period = 500;
    default_cfg.volatility_threshold = 0.8;
    default_cfg.tp_multiplier = 1.0;

    Result default_result = RunTest(ticks, default_cfg);

    std::cout << "  Current V7:  Return=" << default_result.return_pct << "%, MaxDD="
              << default_result.max_dd << "%, Trades=" << default_result.trades_completed << std::endl;
    std::cout << "  Optimized:   Return=" << best.result.return_pct << "%, MaxDD="
              << best.result.max_dd << "%, Trades=" << best.result.trades_completed << std::endl;

    double improvement = ((best.result.return_pct - default_result.return_pct) / std::max(0.01, std::abs(default_result.return_pct))) * 100.0;
    std::cout << "  Improvement: " << (improvement >= 0 ? "+" : "") << improvement << "% return" << std::endl;

    std::cout << "================================================================" << std::endl;

    return 0;
}
