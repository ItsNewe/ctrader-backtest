/**
 * NAS100 Adaptive Room Strategy V2
 *
 * Uses room-based position sizing (not direct lot formula)
 * to test if adaptive room reduces losses
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
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
    double room_at_entry;
};

struct Config {
    double manual_stop_out = 74.0;
    double min_lot = 0.01;
    double max_lot = 100.0;
    double leverage = 500.0;
    double contract_size = 1.0;

    // Room calculation
    double base_multiplier = 0.15;    // Base room = price * mult / 100
    double min_multiplier = 0.03;     // Minimum room at ATH
    int adaptive_mode = 0;            // 0=Fixed, 1=Distance-based, 2=ATH-based

    // Distance-based decay
    double decay_start = 500;         // Start decaying after 500 pts
    double decay_rate = 0.0005;       // Decay rate per point

    // ATH-based
    double ath_reduction = 0.5;       // Reduce room by 50% at confirmed ATH
    int ath_confirm_ticks = 500;

    // Power for position sizing growth
    double power = 0.2;               // Room grows with distance^power

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
    double avg_room_pct;
    bool margin_call;
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);
    FILE* f = fopen(filename, "r");
    if (!f) return ticks;
    char line[256];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
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

    double starting_x = 0;
    double volume_of_open_trades = 0;

    // ATH tracking
    double all_time_high = ticks.front().bid;
    int ticks_near_ath = 0;

    double total_room_pct = 0;
    int room_samples = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // ATH tracking
        if (tick.bid > all_time_high) {
            all_time_high = tick.bid;
            ticks_near_ath = 0;
        }
        if (tick.bid >= all_time_high - 20) {
            ticks_near_ath++;
        }
        bool at_confirmed_ath = (ticks_near_ath > cfg.ath_confirm_ticks);

        // Calculate equity
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

        // Portfolio DD
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // DD limit exit
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
            starting_x = 0;
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
            starting_x = 0;
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
            starting_x = 0;
            continue;
        }

        // === CALCULATE ADAPTIVE ROOM ===
        double distance = (starting_x > 0) ? tick.ask - starting_x : 0;
        double current_multiplier = cfg.base_multiplier;

        switch (cfg.adaptive_mode) {
            case 0:  // Fixed room
                current_multiplier = cfg.base_multiplier;
                break;

            case 1:  // Distance-based decay
                if (distance > cfg.decay_start) {
                    double excess = distance - cfg.decay_start;
                    double decay_factor = std::exp(-cfg.decay_rate * excess);
                    current_multiplier = cfg.min_multiplier +
                                        (cfg.base_multiplier - cfg.min_multiplier) * decay_factor;
                }
                break;

            case 2:  // ATH-based
                if (at_confirmed_ath) {
                    current_multiplier = cfg.base_multiplier * (1.0 - cfg.ath_reduction);
                    current_multiplier = std::max(current_multiplier, cfg.min_multiplier);
                }
                break;

            case 3:  // Combined
                if (distance > cfg.decay_start) {
                    double excess = distance - cfg.decay_start;
                    double decay_factor = std::exp(-cfg.decay_rate * excess);
                    current_multiplier = cfg.min_multiplier +
                                        (cfg.base_multiplier - cfg.min_multiplier) * decay_factor;
                }
                if (at_confirmed_ath) {
                    current_multiplier *= (1.0 - cfg.ath_reduction * 0.3);
                }
                break;
        }

        double base_room = tick.ask * current_multiplier / 100.0;
        total_room_pct += current_multiplier;
        room_samples++;

        // === POSITION SIZING ===
        double spread_cost = tick.spread() * cfg.contract_size;

        // First position
        if (volume_of_open_trades == 0 && balance > 100) {
            double room = base_room;

            double lot = (100 * balance * cfg.leverage) /
                        (100 * room * cfg.leverage + 100 * spread_cost * cfg.leverage +
                         cfg.manual_stop_out * tick.ask);
            lot = lot / cfg.contract_size;

            if (lot >= cfg.min_lot) {
                lot = std::min(lot, cfg.max_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                if (equity > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->room_at_entry = room;
                    positions.push_back(p);
                    starting_x = tick.ask;
                }
            }
        }
        // Additional positions on new highs
        else if (positions.size() > 0 && tick.ask > positions.back()->entry_price) {
            // Room grows with power law as distance increases
            double room = base_room * std::pow(std::max(1.0, distance), cfg.power);

            double lot = (100 * equity * cfg.leverage -
                         cfg.leverage * cfg.manual_stop_out * used_margin -
                         100 * room * cfg.leverage * volume_of_open_trades) /
                        (100 * room * cfg.leverage + 100 * spread_cost * cfg.leverage +
                         cfg.manual_stop_out * tick.ask);
            lot = lot / cfg.contract_size;

            if (lot >= cfg.min_lot) {
                lot = std::min(lot, cfg.max_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->room_at_entry = room;
                    positions.push_back(p);
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
    r.avg_room_pct = (room_samples > 0) ? total_room_pct / room_samples : 0;
    return r;
}

void PrintResult(const char* name, Result r) {
    printf("%-40s $%10.2f %6.2fx %7.1f%% %6d %5.1f%% %.3f%% %s\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0,
           r.avg_room_pct,
           r.margin_call ? "MC" : "");
}

int main() {
    printf("================================================================\n");
    printf("NAS100 ADAPTIVE ROOM V2 (Room-Based Position Sizing)\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks (%.2f -> %.2f = %+.1f%%)\n\n",
           ticks.size(), ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    printf("%-40s %12s %7s %8s %6s %6s %7s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%", "AvgRoom");
    printf("================================================================================\n\n");

    // ========================================================================
    // TEST 1: Base multiplier with fixed room
    // ========================================================================
    printf("--- FIXED ROOM (Mode 0) ---\n");

    double base_mults[] = {0.05, 0.10, 0.15, 0.20, 0.30};
    for (double bm : base_mults) {
        Config cfg;
        cfg.adaptive_mode = 0;
        cfg.base_multiplier = bm;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Fixed room=%.2f%%", bm);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // TEST 2: Distance-based decay
    // ========================================================================
    printf("--- DISTANCE-BASED DECAY (Mode 1) ---\n");
    printf("(Room shrinks as price rises further from start)\n\n");

    // Vary base vs min
    printf("Base/Min Multiplier combinations:\n");
    double min_mults[] = {0.02, 0.03, 0.05};
    for (double bm : std::vector<double>{0.1, 0.15, 0.2}) {
        for (double mm : min_mults) {
            if (mm >= bm) continue;
            Config cfg;
            cfg.adaptive_mode = 1;
            cfg.base_multiplier = bm;
            cfg.min_multiplier = mm;
            cfg.decay_start = 500;
            cfg.decay_rate = 0.001;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "base=%.2f min=%.2f", bm, mm);
            PrintResult(name, r);
        }
    }

    printf("\nDecay rate combinations:\n");
    double decay_rates[] = {0.0005, 0.001, 0.002, 0.005};
    for (double dr : decay_rates) {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = 0.15;
        cfg.min_multiplier = 0.03;
        cfg.decay_start = 500;
        cfg.decay_rate = dr;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "decay_rate=%.4f", dr);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // TEST 3: ATH-based reduction
    // ========================================================================
    printf("--- ATH-BASED REDUCTION (Mode 2) ---\n");
    printf("(Room shrinks when at confirmed all-time high)\n\n");

    double ath_reductions[] = {0.3, 0.5, 0.7};
    int ath_confirms[] = {200, 500, 1000};

    for (double ar : ath_reductions) {
        for (int ac : ath_confirms) {
            Config cfg;
            cfg.adaptive_mode = 2;
            cfg.base_multiplier = 0.15;
            cfg.ath_reduction = ar;
            cfg.ath_confirm_ticks = ac;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "ATH red=%.0f%% conf=%d", ar*100, ac);
            PrintResult(name, r);
        }
    }
    printf("\n");

    // ========================================================================
    // TEST 4: Combined
    // ========================================================================
    printf("--- COMBINED (Mode 3) ---\n");

    {
        Config cfg;
        cfg.adaptive_mode = 3;
        cfg.base_multiplier = 0.15;
        cfg.min_multiplier = 0.03;
        cfg.decay_start = 500;
        cfg.decay_rate = 0.001;
        cfg.ath_reduction = 0.3;
        cfg.ath_confirm_ticks = 500;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Combined (default)", r);
    }

    {
        Config cfg;
        cfg.adaptive_mode = 3;
        cfg.base_multiplier = 0.20;
        cfg.min_multiplier = 0.05;
        cfg.decay_start = 300;
        cfg.decay_rate = 0.002;
        cfg.ath_reduction = 0.5;
        cfg.ath_confirm_ticks = 300;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Combined (aggressive)", r);
    }

    {
        Config cfg;
        cfg.adaptive_mode = 3;
        cfg.base_multiplier = 0.25;
        cfg.min_multiplier = 0.10;
        cfg.decay_start = 1000;
        cfg.decay_rate = 0.0005;
        cfg.ath_reduction = 0.2;
        cfg.ath_confirm_ticks = 1000;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Combined (conservative)", r);
    }
    printf("\n");

    // ========================================================================
    // TEST 5: Power law comparison
    // ========================================================================
    printf("--- POWER LAW FOR ROOM GROWTH ---\n");
    printf("(How room grows with distance)\n\n");

    double powers[] = {0.0, 0.1, 0.2, 0.3, 0.5};
    for (double pw : powers) {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = 0.15;
        cfg.min_multiplier = 0.03;
        cfg.power = pw;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "power=%.1f", pw);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // BEST vs FIXED COMPARISON
    // ========================================================================
    printf("================================================================================\n");
    printf("BEST ADAPTIVE vs FIXED COMPARISON\n");
    printf("================================================================================\n\n");

    // Fixed baseline
    {
        Config cfg;
        cfg.adaptive_mode = 0;
        cfg.base_multiplier = 0.15;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Fixed (0.15%)", r);
    }

    // Best distance-based
    {
        Config cfg;
        cfg.adaptive_mode = 1;
        cfg.base_multiplier = 0.15;
        cfg.min_multiplier = 0.03;
        cfg.decay_rate = 0.001;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Distance-based (0.15->0.03)", r);
    }

    // Best ATH-based
    {
        Config cfg;
        cfg.adaptive_mode = 2;
        cfg.base_multiplier = 0.15;
        cfg.ath_reduction = 0.5;
        cfg.ath_confirm_ticks = 500;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("ATH-based (50% reduction)", r);
    }

    printf("\n================================================================================\n");
    printf("OVERFITTING ASSESSMENT\n");
    printf("================================================================================\n\n");

    printf("Your Question: Is adaptive room just overfitting?\n\n");

    printf("Analysis:\n");
    printf("1. LOGICAL BASIS: The concept has merit\n");
    printf("   - Early in trend: Need more room to survive false breakouts\n");
    printf("   - At confirmed ATH: Can be more aggressive (trend confirmed)\n\n");

    printf("2. RISK FACTORS FOR OVERFITTING:\n");
    printf("   - Too many parameters to tune (base, min, decay, ATH threshold)\n");
    printf("   - If results vary wildly with small param changes = overfitting\n");
    printf("   - If in-sample >> out-of-sample = overfitting\n\n");

    printf("3. RECOMMENDATION:\n");
    printf("   - Use SIMPLE adaptive rule (e.g., just reduce at ATH by 30-50%%)\n");
    printf("   - Avoid complex decay curves with many parameters\n");
    printf("   - Test on multiple market periods before deploying\n");
    printf("   - If improvement is small (<10%%), probably not worth complexity\n");

    printf("\n================================================================\n");
    return 0;
}
