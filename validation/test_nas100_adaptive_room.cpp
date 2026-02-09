/**
 * NAS100 Adaptive Room Strategy
 *
 * Concept: Room decreases as price confirms new ATHs
 * - Early positions: Large room (conservative)
 * - ATH positions: Small room (aggressive)
 *
 * Testing for overfitting by:
 * 1. Testing on different time periods
 * 2. Comparing in-sample vs out-of-sample
 * 3. Checking parameter sensitivity
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <algorithm>

struct Tick {
    double bid;
    double ask;
    int year, month, day;
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
};

struct Config {
    double manual_stop_out = 74.0;
    double min_lot = 0.01;
    double max_lot = 100.0;
    double leverage = 500.0;
    double contract_size = 1.0;

    // Base room parameters
    double base_multiplier = 0.2;     // Starting room = price * base_mult / 100

    // Adaptive room parameters
    int adaptive_mode = 0;            // 0=Fixed, 1=Linear decay, 2=Exponential decay, 3=ATH-based

    // Mode 1: Linear decay
    double min_multiplier = 0.05;     // Room shrinks to this
    double decay_distance = 2000;     // Over this many points

    // Mode 2: Exponential decay
    double decay_rate = 0.001;        // Exponential decay rate

    // Mode 3: ATH-based
    double ath_lookback_pts = 500;    // Points above recent high = confirmed ATH
    double ath_room_reduction = 0.5;  // Reduce room by 50% at ATH
    int ath_confirmation_ticks = 1000; // Ticks above level to confirm

    // Position sizing
    double power = -0.5;
    double lot_coefficient = 50.0;
    bool direct_lot = true;

    // Risk management
    double max_dd = 30.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int max_positions;
    double avg_room_used;
    bool margin_call;
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count,
                            int start_y = 0, int start_m = 0, int start_d = 0,
                            int end_y = 9999, int end_m = 12, int end_d = 31) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);
    FILE* f = fopen(filename, "r");
    if (!f) return ticks;
    char line[256];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
        if (strlen(line) >= 10) {
            char y[5] = {line[0], line[1], line[2], line[3], 0};
            char m[3] = {line[5], line[6], 0};
            char d[3] = {line[8], line[9], 0};
            tick.year = atoi(y);
            tick.month = atoi(m);
            tick.day = atoi(d);

            if (start_y > 0) {
                int date = tick.year * 10000 + tick.month * 100 + tick.day;
                int start = start_y * 10000 + start_m * 100 + start_d;
                int end = end_y * 10000 + end_m * 100 + end_d;
                if (date < start || date > end) continue;
            }
        }
        char* token = strtok(line, "\t");
        if (!token) continue;
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);
        if (tick.bid > 0 && tick.ask > 0) ticks.push_back(tick);
    }
    fclose(f);
    return ticks;
}

Result RunStrategy(const std::vector<Tick>& ticks, Config cfg) {
    Result r = {0};
    if (ticks.empty()) return r;

    const double initial_balance = 10000.0;
    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;

    double checked_last_open_price = -DBL_MAX;
    double starting_x = 0;
    double volume_of_open_trades = 0;

    // ATH tracking
    double all_time_high = ticks.front().bid;
    double recent_high = ticks.front().bid;
    int ticks_at_ath = 0;
    bool ath_confirmed = false;

    double total_room = 0;
    int room_count = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update ATH tracking
        if (tick.bid > all_time_high) {
            all_time_high = tick.bid;
            ticks_at_ath = 0;
        }
        if (tick.bid >= all_time_high - 10) {  // Within 10 pts of ATH
            ticks_at_ath++;
        }
        ath_confirmed = (ticks_at_ath > cfg.ath_confirmation_ticks);

        // Update recent high (rolling window)
        if (tick.bid > recent_high) recent_high = tick.bid;

        // Calculate equity and margin
        equity = balance;
        double used_margin = 0;
        volume_of_open_trades = 0;

        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
            volume_of_open_trades += p->lot_size;
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        if ((int)positions.size() > r.max_positions) r.max_positions = positions.size();

        // Portfolio DD check
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        if (dd_pct >= cfg.max_dd && !positions.empty()) {
            for (Position* p : positions) {
                double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                balance += pnl;
                r.total_trades++;
                if (pnl > 0) r.winning_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        // Margin call
        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            if (balance <= 0) break;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        // Manual stop out
        if (margin_level < cfg.manual_stop_out && margin_level > 0 && !positions.empty()) {
            for (Position* p : positions) {
                double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                balance += pnl;
                r.total_trades++;
                if (pnl > 0) r.winning_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        // === CALCULATE ADAPTIVE ROOM ===
        double current_multiplier = cfg.base_multiplier;
        double distance_from_start = (starting_x > 0) ? tick.ask - starting_x : 0;

        switch (cfg.adaptive_mode) {
            case 0:  // Fixed
                current_multiplier = cfg.base_multiplier;
                break;

            case 1:  // Linear decay
                if (distance_from_start > 0) {
                    double decay_factor = std::min(1.0, distance_from_start / cfg.decay_distance);
                    current_multiplier = cfg.base_multiplier -
                                        (cfg.base_multiplier - cfg.min_multiplier) * decay_factor;
                }
                break;

            case 2:  // Exponential decay
                if (distance_from_start > 0) {
                    current_multiplier = cfg.min_multiplier +
                                        (cfg.base_multiplier - cfg.min_multiplier) *
                                        std::exp(-cfg.decay_rate * distance_from_start);
                }
                break;

            case 3:  // ATH-based
                if (ath_confirmed && tick.bid > all_time_high - cfg.ath_lookback_pts) {
                    current_multiplier = cfg.base_multiplier * (1.0 - cfg.ath_room_reduction);
                }
                break;

            case 4:  // Combined: Linear + ATH bonus
                if (distance_from_start > 0) {
                    double decay_factor = std::min(1.0, distance_from_start / cfg.decay_distance);
                    current_multiplier = cfg.base_multiplier -
                                        (cfg.base_multiplier - cfg.min_multiplier) * decay_factor;
                }
                if (ath_confirmed) {
                    current_multiplier *= (1.0 - cfg.ath_room_reduction * 0.5);
                }
                break;
        }

        double local_room = tick.ask * current_multiplier / 100.0;
        total_room += current_multiplier;
        room_count++;

        // === OPEN POSITIONS ===
        double spread_cost = tick.spread() * cfg.contract_size;

        if (volume_of_open_trades == 0) {
            double lot;
            if (cfg.direct_lot) {
                lot = cfg.lot_coefficient;
            } else {
                double temp = (100 * balance * cfg.leverage) /
                             (100 * local_room * cfg.leverage + 100 * spread_cost * cfg.leverage +
                              cfg.manual_stop_out * tick.ask);
                lot = temp / cfg.contract_size;
            }

            if (lot >= cfg.min_lot) {
                lot = std::min(lot, cfg.max_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                if (equity > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                    starting_x = tick.ask;
                }
            }
        }
        else if (tick.ask > checked_last_open_price) {
            double distance = tick.ask - starting_x;
            if (distance < 1) distance = 1;

            double lot;
            if (cfg.direct_lot) {
                lot = cfg.lot_coefficient * std::pow(distance, cfg.power);
                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
            } else {
                double room_temp = local_room * std::pow(distance, std::abs(cfg.power));
                double temp = (100 * equity * cfg.leverage -
                              cfg.leverage * cfg.manual_stop_out * used_margin -
                              100 * room_temp * cfg.leverage * volume_of_open_trades) /
                             (100 * room_temp * cfg.leverage + 100 * spread_cost * cfg.leverage +
                              cfg.manual_stop_out * tick.ask);
                lot = temp / cfg.contract_size;
                if (lot < cfg.min_lot) continue;
                lot = std::min(lot, cfg.max_lot);
            }

            lot = std::round(lot * 100) / 100;
            if (lot >= cfg.min_lot) {
                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                }
            }
        }
    }

    // Close remaining
    for (Position* p : positions) {
        double pnl = (ticks.back().bid - p->entry_price) * p->lot_size * cfg.contract_size;
        balance += pnl;
        r.total_trades++;
        if (pnl > 0) r.winning_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    r.avg_room_used = (room_count > 0) ? total_room / room_count : 0;
    return r;
}

void PrintResult(const char* name, Result r) {
    printf("%-40s $%10.2f %6.1fx %7.1f%% %6d %5.1f%% %s\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0,
           r.margin_call ? "MC" : "");
}

int main() {
    printf("================================================================\n");
    printf("NAS100 ADAPTIVE ROOM STRATEGY - OVERFITTING ANALYSIS\n");
    printf("================================================================\n\n");

    // Load different periods for out-of-sample testing
    auto full_year = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    auto h1_2025 = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000, 2025, 1, 1, 2025, 6, 30);
    auto h2_2025 = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000, 2025, 7, 1, 2025, 12, 31);
    auto q1 = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000, 2025, 1, 1, 2025, 3, 31);
    auto q2 = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000, 2025, 4, 1, 2025, 6, 30);
    auto q3 = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000, 2025, 7, 1, 2025, 9, 30);
    auto q4 = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000, 2025, 10, 1, 2025, 12, 31);

    printf("Full Year: %zu ticks (%.2f -> %.2f = %+.1f%%)\n",
           full_year.size(), full_year.front().bid, full_year.back().bid,
           (full_year.back().bid - full_year.front().bid) / full_year.front().bid * 100);
    printf("H1 2025: %zu ticks\n", h1_2025.size());
    printf("H2 2025: %zu ticks\n", h2_2025.size());
    printf("Q1: %zu, Q2: %zu, Q3: %zu, Q4: %zu\n\n", q1.size(), q2.size(), q3.size(), q4.size());

    printf("%-40s %12s %7s %8s %6s %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%");
    printf("================================================================================\n\n");

    // ========================================================================
    // TEST 1: Compare adaptive modes on full year
    // ========================================================================
    printf("--- ADAPTIVE MODE COMPARISON (Full Year) ---\n");

    const char* mode_names[] = {"Fixed Room", "Linear Decay", "Exp Decay", "ATH-Based", "Combined"};

    for (int mode = 0; mode <= 4; mode++) {
        Config cfg;
        cfg.adaptive_mode = mode;
        cfg.base_multiplier = 0.2;
        cfg.min_multiplier = 0.05;
        cfg.decay_distance = 2000;
        cfg.decay_rate = 0.001;
        cfg.ath_room_reduction = 0.5;
        Result r = RunStrategy(full_year, cfg);
        PrintResult(mode_names[mode], r);
    }
    printf("\n");

    // ========================================================================
    // TEST 2: Parameter sensitivity (overfitting check)
    // ========================================================================
    printf("--- PARAMETER SENSITIVITY (Linear Decay) ---\n");

    double base_mults[] = {0.1, 0.15, 0.2, 0.25, 0.3};
    double min_mults[] = {0.03, 0.05, 0.08, 0.1};
    double decay_dists[] = {1000, 2000, 3000, 5000};

    printf("Base Multiplier sweep (min=0.05, decay=2000):\n");
    for (double bm : base_mults) {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = bm;
        cfg.min_multiplier = 0.05;
        cfg.decay_distance = 2000;
        Result r = RunStrategy(full_year, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  base=%.2f", bm);
        PrintResult(name, r);
    }

    printf("\nMin Multiplier sweep (base=0.2, decay=2000):\n");
    for (double mm : min_mults) {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = 0.2;
        cfg.min_multiplier = mm;
        cfg.decay_distance = 2000;
        Result r = RunStrategy(full_year, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  min=%.2f", mm);
        PrintResult(name, r);
    }

    printf("\nDecay Distance sweep (base=0.2, min=0.05):\n");
    for (double dd : decay_dists) {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = 0.2;
        cfg.min_multiplier = 0.05;
        cfg.decay_distance = dd;
        Result r = RunStrategy(full_year, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  decay=%.0f", dd);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // TEST 3: IN-SAMPLE vs OUT-OF-SAMPLE
    // ========================================================================
    printf("--- IN-SAMPLE vs OUT-OF-SAMPLE ---\n");
    printf("(Train on H1, Test on H2)\n\n");

    // Find best config on H1 (in-sample)
    double best_return = 0;
    double best_bm = 0.2, best_mm = 0.05, best_dd = 2000;

    printf("H1 2025 (In-Sample) - Finding best config:\n");
    for (double bm : base_mults) {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = bm;
        cfg.min_multiplier = 0.05;
        cfg.decay_distance = 2000;
        Result r = RunStrategy(h1_2025, cfg);
        if (r.return_multiple > best_return) {
            best_return = r.return_multiple;
            best_bm = bm;
        }
        char name[64];
        snprintf(name, sizeof(name), "  base=%.2f", bm);
        PrintResult(name, r);
    }

    printf("\nBest In-Sample: base=%.2f (%.1fx return)\n\n", best_bm, best_return);

    // Test on H2 (out-of-sample)
    printf("H2 2025 (Out-of-Sample) - Testing best config:\n");
    {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = best_bm;
        cfg.min_multiplier = 0.05;
        cfg.decay_distance = 2000;
        Result r = RunStrategy(h2_2025, cfg);
        PrintResult("  Best from H1", r);
    }

    // Also test fixed (no adaptive) on H2
    {
        Config cfg;
        cfg.adaptive_mode = 0;
        cfg.base_multiplier = 0.2;
        Result r = RunStrategy(h2_2025, cfg);
        PrintResult("  Fixed (baseline)", r);
    }
    printf("\n");

    // ========================================================================
    // TEST 4: QUARTERLY CONSISTENCY
    // ========================================================================
    printf("--- QUARTERLY CONSISTENCY (Same config on each quarter) ---\n");
    printf("Config: Linear decay, base=0.2, min=0.05, decay=2000\n\n");

    auto* quarters = new std::pair<const char*, std::vector<Tick>*>[4]{
        {"Q1 2025", &q1}, {"Q2 2025", &q2}, {"Q3 2025", &q3}, {"Q4 2025", &q4}
    };

    for (int i = 0; i < 4; i++) {
        if (quarters[i].second->empty()) {
            printf("%-40s (no data)\n", quarters[i].first);
            continue;
        }

        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = 0.2;
        cfg.min_multiplier = 0.05;
        cfg.decay_distance = 2000;
        Result r = RunStrategy(*quarters[i].second, cfg);

        char name[64];
        snprintf(name, sizeof(name), "%s (%.1f%% mkt move)",
                quarters[i].first,
                (quarters[i].second->back().bid - quarters[i].second->front().bid) /
                quarters[i].second->front().bid * 100);
        PrintResult(name, r);
    }
    delete[] quarters;
    printf("\n");

    // ========================================================================
    // TEST 5: ATH-BASED MODE
    // ========================================================================
    printf("--- ATH-BASED ADAPTIVE ROOM ---\n");

    double ath_reductions[] = {0.3, 0.5, 0.7};
    int ath_confirmations[] = {500, 1000, 2000, 5000};

    printf("ATH Room Reduction sweep:\n");
    for (double ar : ath_reductions) {
        Config cfg;
        cfg.adaptive_mode = 3;
        cfg.ath_room_reduction = ar;
        cfg.ath_confirmation_ticks = 1000;
        Result r = RunStrategy(full_year, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  ATH reduction=%.0f%%", ar * 100);
        PrintResult(name, r);
    }

    printf("\nATH Confirmation Ticks sweep:\n");
    for (int ac : ath_confirmations) {
        Config cfg;
        cfg.adaptive_mode = 3;
        cfg.ath_room_reduction = 0.5;
        cfg.ath_confirmation_ticks = ac;
        Result r = RunStrategy(full_year, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  ATH confirm=%d ticks", ac);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // OVERFITTING ANALYSIS
    // ========================================================================
    printf("================================================================================\n");
    printf("OVERFITTING ANALYSIS\n");
    printf("================================================================================\n\n");

    // Calculate variance across quarters
    double quarterly_returns[4];
    for (int i = 0; i < 4; i++) {
        std::vector<Tick>* q = (i == 0) ? &q1 : (i == 1) ? &q2 : (i == 2) ? &q3 : &q4;
        if (q->empty()) {
            quarterly_returns[i] = 1.0;
            continue;
        }
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = 0.2;
        cfg.min_multiplier = 0.05;
        cfg.decay_distance = 2000;
        Result r = RunStrategy(*q, cfg);
        quarterly_returns[i] = r.return_multiple;
    }

    double mean = 0;
    for (int i = 0; i < 4; i++) mean += quarterly_returns[i];
    mean /= 4;

    double variance = 0;
    for (int i = 0; i < 4; i++) variance += (quarterly_returns[i] - mean) * (quarterly_returns[i] - mean);
    variance /= 4;

    printf("Quarterly Return Variance: %.4f\n", variance);
    printf("Quarterly Returns: Q1=%.2fx, Q2=%.2fx, Q3=%.2fx, Q4=%.2fx\n",
           quarterly_returns[0], quarterly_returns[1], quarterly_returns[2], quarterly_returns[3]);
    printf("Mean: %.2fx, StdDev: %.2fx\n\n", mean, std::sqrt(variance));

    if (variance > 0.1) {
        printf("WARNING: High variance suggests OVERFITTING risk!\n");
        printf("Strategy performance varies significantly across periods.\n");
    } else if (variance > 0.05) {
        printf("CAUTION: Moderate variance - some period dependency.\n");
    } else {
        printf("Good: Low variance suggests consistent strategy.\n");
    }

    printf("\n================================================================\n");
    printf("CONCLUSION\n");
    printf("================================================================\n\n");

    printf("Adaptive Room Concept:\n");
    printf("- Logical basis: Be conservative early, aggressive at confirmed ATH\n");
    printf("- Risk: Can become overfitting if parameters too specific\n\n");

    printf("Recommendations:\n");
    printf("1. Use simple decay (linear or exp) rather than complex ATH logic\n");
    printf("2. Test on multiple periods before deploying\n");
    printf("3. Wide parameter ranges = less overfitting\n");
    printf("4. If in-sample >> out-of-sample, likely overfitting\n");

    printf("\n================================================================\n");
    return 0;
}
