/**
 * Deep Trade Analysis: Winners vs Losers
 *
 * Analyzes the characteristics of winning vs losing trades to find patterns.
 * Goal: Build a profile of "Ideal trade entry conditions" vs "Avoid entry conditions"
 *
 * Runs V7 strategy WITHOUT the volatility filter to generate both winning
 * and losing trades, then analyzes what conditions led to each outcome.
 *
 * Metrics analyzed for each trade:
 * - Entry time, price, ATR at entry
 * - Exit time, price, exit type (TP hit vs forced close)
 * - Time held (in ticks)
 * - Max Adverse Excursion (MAE) - how far against us did it go?
 * - Max Favorable Excursion (MFE) - how close to TP did it get?
 * - Number of other positions open at entry
 * - Price deviation from SMA at entry
 * - Hour of day at entry
 * - ATR ratio (short/long) at entry
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <algorithm>

// Tick structure with timestamp
struct Tick {
    char timestamp[32];
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

// Detailed trade record for analysis
struct TradeRecord {
    // Entry info
    size_t entry_tick_idx;
    double entry_price;
    double entry_atr_short;
    double entry_atr_long;
    double entry_atr_ratio;
    double entry_sma;
    double entry_sma_deviation;  // (price - sma) / sma * 100
    int entry_hour;
    int entry_minute;
    int positions_at_entry;

    // Exit info
    size_t exit_tick_idx;
    double exit_price;
    double take_profit;
    bool hit_tp;  // true = TP hit, false = forced close
    char exit_reason[32];

    // Excursions (during trade lifetime)
    double max_adverse_excursion;   // MAE: worst drawdown
    double max_favorable_excursion; // MFE: closest to TP
    double profit_loss;

    // Time info
    size_t ticks_held;

    // Classification
    bool is_winner;
};

// Simple SMA calculator
class SMA {
    std::deque<double> prices;
    int period;
    double sum = 0;
public:
    SMA(int p) : period(p) {}
    void Add(double price) {
        prices.push_back(price);
        sum += price;
        if ((int)prices.size() > period) {
            sum -= prices.front();
            prices.pop_front();
        }
    }
    double Get() const { return prices.empty() ? 0 : sum / prices.size(); }
    bool IsReady() const { return (int)prices.size() >= period; }
};

// Simple ATR calculator
class ATR {
    std::deque<double> ranges;
    int period;
    double sum = 0;
    double last_price = 0;
public:
    ATR(int p) : period(p) {}
    void Add(double price) {
        if (last_price > 0) {
            double range = fabs(price - last_price);
            ranges.push_back(range);
            sum += range;
            if ((int)ranges.size() > period) {
                sum -= ranges.front();
                ranges.pop_front();
            }
        }
        last_price = price;
    }
    double Get() const { return ranges.empty() ? 0 : sum / ranges.size(); }
    bool IsReady() const { return (int)ranges.size() >= period; }
};

// Live trade tracking
struct LiveTrade {
    size_t id;
    size_t entry_tick_idx;
    double entry_price;
    double lot_size;
    double take_profit;

    // Entry conditions snapshot
    double entry_atr_short;
    double entry_atr_long;
    double entry_sma;
    int entry_hour;
    int entry_minute;
    int positions_at_entry;

    // Excursion tracking
    double mae;  // Max adverse excursion
    double mfe;  // Max favorable excursion
};

// Parse hour from timestamp "YYYY.MM.DD HH:MM:SS"
int ParseHour(const char* ts) {
    if (strlen(ts) < 13) return 0;
    int h = (ts[11] - '0') * 10 + (ts[12] - '0');
    return h;
}

int ParseMinute(const char* ts) {
    if (strlen(ts) < 16) return 0;
    int m = (ts[14] - '0') * 10 + (ts[15] - '0');
    return m;
}

// Load ticks using C-style file I/O
std::vector<Tick> LoadTicks(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;
    ticks.reserve(max_ticks);

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    char line[256];
    // Skip header
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp) && ticks.size() < max_ticks) {
        Tick tick;
        char* token = strtok(line, "\t");
        if (!token) continue;
        strncpy(tick.timestamp, token, 31);
        tick.timestamp[31] = '\0';

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);

        token = strtok(NULL, "\t\r\n");
        if (!token) continue;
        tick.ask = atof(token);

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
        }

        if (ticks.size() % 500000 == 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(fp);
    return ticks;
}

// Statistics helper
struct Stats {
    double mean;
    double median;
    double min;
    double max;
    double stddev;
    int count;
};

Stats CalculateStats(const std::vector<double>& values) {
    Stats s = {0, 0, DBL_MAX, -DBL_MAX, 0, (int)values.size()};
    if (values.empty()) return s;

    double sum = 0;
    for (double v : values) {
        sum += v;
        s.min = std::min(s.min, v);
        s.max = std::max(s.max, v);
    }
    s.mean = sum / values.size();

    // Median
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    s.median = sorted[sorted.size() / 2];

    // Stddev
    double var_sum = 0;
    for (double v : values) {
        var_sum += (v - s.mean) * (v - s.mean);
    }
    s.stddev = sqrt(var_sum / values.size());

    return s;
}

// Hour distribution
struct HourDist {
    int hour_counts[24];
    double hour_pnl[24];

    HourDist() {
        memset(hour_counts, 0, sizeof(hour_counts));
        memset(hour_pnl, 0, sizeof(hour_pnl));
    }
};

int main() {
    printf("=======================================================\n");
    printf("DEEP TRADE ANALYSIS: Winners vs Losers\n");
    printf("=======================================================\n\n");

    // Load ticks
    const char* filename = "Broker/XAUUSD_TICKS_2025.csv";
    printf("Loading ticks from: %s\n", filename);

    std::vector<Tick> ticks = LoadTicks(filename, 50000000);  // Load 50M ticks for comprehensive analysis
    if (ticks.empty()) {
        printf("Failed to load tick data\n");
        return 1;
    }
    printf("Loaded %zu ticks total\n\n", ticks.size());

    // Strategy parameters (V7 style)
    const double CONTRACT_SIZE = 100.0;
    const double SPACING = 1.0;
    const int MAX_POSITIONS = 20;
    const double STOP_NEW_DD = 5.0;
    const double PARTIAL_CLOSE_DD = 8.0;
    const double CLOSE_ALL_DD = 25.0;

    // Indicator parameters
    const int ATR_SHORT = 100;
    const int ATR_LONG = 500;
    const int SMA_PERIOD = 1000;
    const double VOL_THRESHOLD = 0.8;

    // Initialize indicators
    SMA sma(SMA_PERIOD);
    ATR atr_short(ATR_SHORT);
    ATR atr_long(ATR_LONG);

    // Trading state
    std::vector<LiveTrade*> positions;
    std::vector<TradeRecord> completed_trades;
    size_t next_id = 1;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    bool partial_done = false;
    bool all_closed = false;

    printf("Running V7 strategy and collecting trade data...\n");

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update indicators
        sma.Add(tick.bid);
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Update equity
        equity = balance;
        for (LiveTrade* t : positions) {
            double unrealized = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
            equity += unrealized;

            // Update MAE/MFE
            double excursion = tick.bid - t->entry_price;
            if (excursion < 0 && fabs(excursion) > t->mae) {
                t->mae = fabs(excursion);
            }
            double to_tp = t->take_profit - tick.bid;
            double mfe_potential = (t->take_profit - t->entry_price) - to_tp;
            if (mfe_potential > t->mfe) {
                t->mfe = mfe_potential;
            }
        }

        // Reset on flat
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

        // Protection: Close all
        if (dd_pct > CLOSE_ALL_DD && !all_closed && !positions.empty()) {
            for (LiveTrade* t : positions) {
                TradeRecord rec;
                rec.entry_tick_idx = t->entry_tick_idx;
                rec.entry_price = t->entry_price;
                rec.entry_atr_short = t->entry_atr_short;
                rec.entry_atr_long = t->entry_atr_long;
                rec.entry_atr_ratio = (t->entry_atr_long > 0) ? t->entry_atr_short / t->entry_atr_long : 1.0;
                rec.entry_sma = t->entry_sma;
                rec.entry_sma_deviation = (t->entry_sma > 0) ? (t->entry_price - t->entry_sma) / t->entry_sma * 100.0 : 0.0;
                rec.entry_hour = t->entry_hour;
                rec.entry_minute = t->entry_minute;
                rec.positions_at_entry = t->positions_at_entry;

                rec.exit_tick_idx = i;
                rec.exit_price = tick.bid;
                rec.take_profit = t->take_profit;
                rec.hit_tp = false;
                strcpy(rec.exit_reason, "CLOSE_ALL_DD");

                rec.max_adverse_excursion = t->mae;
                rec.max_favorable_excursion = t->mfe;
                rec.profit_loss = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                rec.ticks_held = i - t->entry_tick_idx;
                rec.is_winner = (rec.profit_loss > 0);

                balance += rec.profit_loss;
                completed_trades.push_back(rec);
                delete t;
            }
            positions.clear();
            all_closed = true;
            peak_equity = balance;
            equity = balance;
            continue;
        }

        // Protection: Partial close
        if (dd_pct > PARTIAL_CLOSE_DD && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](LiveTrade* a, LiveTrade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });

            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int j = 0; j < to_close && !positions.empty(); j++) {
                LiveTrade* t = positions[0];

                TradeRecord rec;
                rec.entry_tick_idx = t->entry_tick_idx;
                rec.entry_price = t->entry_price;
                rec.entry_atr_short = t->entry_atr_short;
                rec.entry_atr_long = t->entry_atr_long;
                rec.entry_atr_ratio = (t->entry_atr_long > 0) ? t->entry_atr_short / t->entry_atr_long : 1.0;
                rec.entry_sma = t->entry_sma;
                rec.entry_sma_deviation = (t->entry_sma > 0) ? (t->entry_price - t->entry_sma) / t->entry_sma * 100.0 : 0.0;
                rec.entry_hour = t->entry_hour;
                rec.entry_minute = t->entry_minute;
                rec.positions_at_entry = t->positions_at_entry;

                rec.exit_tick_idx = i;
                rec.exit_price = tick.bid;
                rec.take_profit = t->take_profit;
                rec.hit_tp = false;
                strcpy(rec.exit_reason, "PARTIAL_CLOSE");

                rec.max_adverse_excursion = t->mae;
                rec.max_favorable_excursion = t->mfe;
                rec.profit_loss = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                rec.ticks_held = i - t->entry_tick_idx;
                rec.is_winner = (rec.profit_loss > 0);

                balance += rec.profit_loss;
                completed_trades.push_back(rec);
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TP
        for (auto it = positions.begin(); it != positions.end();) {
            LiveTrade* t = *it;
            if (tick.bid >= t->take_profit) {
                TradeRecord rec;
                rec.entry_tick_idx = t->entry_tick_idx;
                rec.entry_price = t->entry_price;
                rec.entry_atr_short = t->entry_atr_short;
                rec.entry_atr_long = t->entry_atr_long;
                rec.entry_atr_ratio = (t->entry_atr_long > 0) ? t->entry_atr_short / t->entry_atr_long : 1.0;
                rec.entry_sma = t->entry_sma;
                rec.entry_sma_deviation = (t->entry_sma > 0) ? (t->entry_price - t->entry_sma) / t->entry_sma * 100.0 : 0.0;
                rec.entry_hour = t->entry_hour;
                rec.entry_minute = t->entry_minute;
                rec.positions_at_entry = t->positions_at_entry;

                rec.exit_tick_idx = i;
                rec.exit_price = t->take_profit;  // TP price
                rec.take_profit = t->take_profit;
                rec.hit_tp = true;
                strcpy(rec.exit_reason, "TP_HIT");

                rec.max_adverse_excursion = t->mae;
                rec.max_favorable_excursion = t->mfe;
                rec.profit_loss = (t->take_profit - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                rec.ticks_held = i - t->entry_tick_idx;
                rec.is_winner = true;  // TP hit is always a winner

                balance += rec.profit_loss;
                completed_trades.push_back(rec);
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // DISABLED: V7 volatility filter - we want all trades to analyze patterns
        // We still RECORD the ATR values but don't use them to filter entries
        // This gives us more losing trades to analyze and find patterns
        bool volatility_ok = true;  // Always allow trading
        // Record what the filter WOULD have said for analysis:
        bool would_filter_allow = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            would_filter_allow = atr_short.Get() < atr_long.Get() * VOL_THRESHOLD;
        }
        (void)would_filter_allow;  // Suppress unused warning - we capture this in entry_atr_ratio

        // Open new positions
        if (dd_pct < STOP_NEW_DD && volatility_ok && (int)positions.size() < MAX_POSITIONS) {
            double lowest = DBL_MAX, highest = -DBL_MAX;
            for (LiveTrade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + SPACING) ||
                              (highest <= tick.ask - SPACING);

            if (should_open) {
                LiveTrade* t = new LiveTrade();
                t->id = next_id++;
                t->entry_tick_idx = i;
                t->entry_price = tick.ask;
                t->lot_size = 0.01;
                t->take_profit = tick.ask + tick.spread() + SPACING;

                t->entry_atr_short = atr_short.Get();
                t->entry_atr_long = atr_long.Get();
                t->entry_sma = sma.Get();
                t->entry_hour = ParseHour(tick.timestamp);
                t->entry_minute = ParseMinute(tick.timestamp);
                t->positions_at_entry = (int)positions.size();

                t->mae = 0;
                t->mfe = 0;

                positions.push_back(t);
            }
        }

        // Progress
        if (i % 1000000 == 0 && i > 0) {
            printf("  Processed %zu ticks, %zu trades completed\n", i, completed_trades.size());
        }
    }

    // Close remaining positions at end
    for (LiveTrade* t : positions) {
        const Tick& tick = ticks.back();

        TradeRecord rec;
        rec.entry_tick_idx = t->entry_tick_idx;
        rec.entry_price = t->entry_price;
        rec.entry_atr_short = t->entry_atr_short;
        rec.entry_atr_long = t->entry_atr_long;
        rec.entry_atr_ratio = (t->entry_atr_long > 0) ? t->entry_atr_short / t->entry_atr_long : 1.0;
        rec.entry_sma = t->entry_sma;
        rec.entry_sma_deviation = (t->entry_sma > 0) ? (t->entry_price - t->entry_sma) / t->entry_sma * 100.0 : 0.0;
        rec.entry_hour = t->entry_hour;
        rec.entry_minute = t->entry_minute;
        rec.positions_at_entry = t->positions_at_entry;

        rec.exit_tick_idx = ticks.size() - 1;
        rec.exit_price = tick.bid;
        rec.take_profit = t->take_profit;
        rec.hit_tp = false;
        strcpy(rec.exit_reason, "END_OF_DATA");

        rec.max_adverse_excursion = t->mae;
        rec.max_favorable_excursion = t->mfe;
        rec.profit_loss = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
        rec.ticks_held = ticks.size() - 1 - t->entry_tick_idx;
        rec.is_winner = (rec.profit_loss > 0);

        completed_trades.push_back(rec);
        delete t;
    }
    positions.clear();

    printf("\nCompleted strategy run: %zu total trades\n\n", completed_trades.size());

    // =========================================================================
    // ANALYSIS: Separate winners and losers
    // =========================================================================

    std::vector<TradeRecord> winners, losers;
    for (const auto& t : completed_trades) {
        if (t.is_winner) {
            winners.push_back(t);
        } else {
            losers.push_back(t);
        }
    }

    printf("=======================================================\n");
    printf("TRADE CLASSIFICATION\n");
    printf("=======================================================\n");
    printf("Total trades:   %zu\n", completed_trades.size());
    printf("Winners:        %zu (%.1f%%)\n", winners.size(), winners.size() * 100.0 / completed_trades.size());
    printf("Losers:         %zu (%.1f%%)\n", losers.size(), losers.size() * 100.0 / completed_trades.size());
    printf("\n");

    // Exit type breakdown
    int tp_hits = 0, partial_closes = 0, close_all = 0, end_of_data = 0;
    for (const auto& t : completed_trades) {
        if (strcmp(t.exit_reason, "TP_HIT") == 0) tp_hits++;
        else if (strcmp(t.exit_reason, "PARTIAL_CLOSE") == 0) partial_closes++;
        else if (strcmp(t.exit_reason, "CLOSE_ALL_DD") == 0) close_all++;
        else if (strcmp(t.exit_reason, "END_OF_DATA") == 0) end_of_data++;
    }

    printf("Exit Types:\n");
    printf("  TP Hit:        %d (%.1f%%)\n", tp_hits, tp_hits * 100.0 / completed_trades.size());
    printf("  Partial Close: %d (%.1f%%)\n", partial_closes, partial_closes * 100.0 / completed_trades.size());
    printf("  Close All DD:  %d (%.1f%%)\n", close_all, close_all * 100.0 / completed_trades.size());
    printf("  End of Data:   %d (%.1f%%)\n", end_of_data, end_of_data * 100.0 / completed_trades.size());
    printf("\n");

    // =========================================================================
    // VOLATILITY FILTER SIMULATION: What would V7 filter have caught?
    // =========================================================================
    printf("=======================================================\n");
    printf("VOLATILITY FILTER SIMULATION (V7 threshold = %.2f)\n", VOL_THRESHOLD);
    printf("=======================================================\n");

    int would_allow_winners = 0, would_allow_losers = 0;
    int would_block_winners = 0, would_block_losers = 0;

    for (const auto& t : completed_trades) {
        bool would_allow = (t.entry_atr_ratio < VOL_THRESHOLD);
        if (t.is_winner) {
            if (would_allow) would_allow_winners++;
            else would_block_winners++;
        } else {
            if (would_allow) would_allow_losers++;
            else would_block_losers++;
        }
    }

    printf("\nTrades V7 filter WOULD ALLOW:\n");
    printf("  Winners: %d\n", would_allow_winners);
    printf("  Losers:  %d\n", would_allow_losers);
    printf("  Win Rate: %.2f%%\n", would_allow_winners * 100.0 / (would_allow_winners + would_allow_losers + 0.001));

    printf("\nTrades V7 filter WOULD BLOCK:\n");
    printf("  Winners: %d (opportunity cost)\n", would_block_winners);
    printf("  Losers:  %d (saved)\n", would_block_losers);
    printf("  Pct of losers blocked: %.1f%%\n", would_block_losers * 100.0 / (losers.size() + 0.001));

    // Test multiple thresholds
    printf("\nOptimal threshold analysis:\n");
    printf("%-12s | %-12s | %-12s | %-12s | %-10s\n",
           "Threshold", "Allow Win", "Allow Lose", "Block Lose%", "Win Rate");
    printf("-------------|--------------|--------------|--------------|------------\n");

    for (double thresh = 0.5; thresh <= 1.5; thresh += 0.1) {
        int aw = 0, al = 0, bl = 0;
        for (const auto& t : completed_trades) {
            bool allow = (t.entry_atr_ratio < thresh);
            if (t.is_winner && allow) aw++;
            if (!t.is_winner && allow) al++;
            if (!t.is_winner && !allow) bl++;
        }
        double wr = aw * 100.0 / (aw + al + 0.001);
        double blocked_pct = bl * 100.0 / (losers.size() + 0.001);
        printf("%11.2f  | %12d | %12d | %11.1f%% | %9.2f%%\n",
               thresh, aw, al, blocked_pct, wr);
    }
    printf("\n");

    // =========================================================================
    // ANALYSIS 1: ATR Levels at Entry
    // =========================================================================
    printf("=======================================================\n");
    printf("ANALYSIS 1: ATR LEVELS AT ENTRY\n");
    printf("=======================================================\n");

    std::vector<double> winner_atr_short, loser_atr_short;
    std::vector<double> winner_atr_ratio, loser_atr_ratio;

    for (const auto& t : winners) {
        winner_atr_short.push_back(t.entry_atr_short);
        winner_atr_ratio.push_back(t.entry_atr_ratio);
    }
    for (const auto& t : losers) {
        loser_atr_short.push_back(t.entry_atr_short);
        loser_atr_ratio.push_back(t.entry_atr_ratio);
    }

    Stats w_atr = CalculateStats(winner_atr_short);
    Stats l_atr = CalculateStats(loser_atr_short);
    Stats w_ratio = CalculateStats(winner_atr_ratio);
    Stats l_ratio = CalculateStats(loser_atr_ratio);

    printf("\nShort-term ATR at entry:\n");
    printf("  WINNERS: mean=%.4f, median=%.4f, stddev=%.4f\n", w_atr.mean, w_atr.median, w_atr.stddev);
    printf("  LOSERS:  mean=%.4f, median=%.4f, stddev=%.4f\n", l_atr.mean, l_atr.median, l_atr.stddev);
    printf("  DIFF:    Winners %.2f%% %s than losers\n",
           fabs(w_atr.mean - l_atr.mean) / l_atr.mean * 100,
           w_atr.mean < l_atr.mean ? "LOWER" : "HIGHER");

    printf("\nATR Ratio (short/long) at entry:\n");
    printf("  WINNERS: mean=%.4f, median=%.4f\n", w_ratio.mean, w_ratio.median);
    printf("  LOSERS:  mean=%.4f, median=%.4f\n", l_ratio.mean, l_ratio.median);
    printf("  INSIGHT: ");
    if (w_ratio.mean < l_ratio.mean) {
        printf("Winners entered at LOWER volatility (%.2f%% lower)\n",
               (l_ratio.mean - w_ratio.mean) / l_ratio.mean * 100);
    } else {
        printf("Winners entered at HIGHER volatility (%.2f%% higher)\n",
               (w_ratio.mean - l_ratio.mean) / l_ratio.mean * 100);
    }
    printf("\n");

    // =========================================================================
    // ANALYSIS 2: Time of Day
    // =========================================================================
    printf("=======================================================\n");
    printf("ANALYSIS 2: TIME OF DAY (Entry Hour)\n");
    printf("=======================================================\n");

    HourDist winner_hours, loser_hours;
    for (const auto& t : winners) {
        winner_hours.hour_counts[t.entry_hour]++;
        winner_hours.hour_pnl[t.entry_hour] += t.profit_loss;
    }
    for (const auto& t : losers) {
        loser_hours.hour_counts[t.entry_hour]++;
        loser_hours.hour_pnl[t.entry_hour] += t.profit_loss;
    }

    printf("\n%-6s | %-10s | %-10s | %-8s | %-10s\n", "Hour", "Winners", "Losers", "Win%", "Net P/L");
    printf("-------|------------|------------|----------|------------\n");

    for (int h = 0; h < 24; h++) {
        int w = winner_hours.hour_counts[h];
        int l = loser_hours.hour_counts[h];
        double total = w + l;
        double win_pct = (total > 0) ? w * 100.0 / total : 0;
        double net_pnl = winner_hours.hour_pnl[h] + loser_hours.hour_pnl[h];

        if (total > 10) {  // Only show hours with significant trades
            printf("%02d:00  | %10d | %10d | %7.1f%% | $%9.2f\n", h, w, l, win_pct, net_pnl);
        }
    }

    // Find best and worst hours
    int best_hour = 0, worst_hour = 0;
    double best_winrate = 0, worst_winrate = 100;
    for (int h = 0; h < 24; h++) {
        int total = winner_hours.hour_counts[h] + loser_hours.hour_counts[h];
        if (total > 20) {
            double wr = winner_hours.hour_counts[h] * 100.0 / total;
            if (wr > best_winrate) { best_winrate = wr; best_hour = h; }
            if (wr < worst_winrate) { worst_winrate = wr; worst_hour = h; }
        }
    }

    printf("\n  BEST HOUR:  %02d:00 (%.1f%% win rate)\n", best_hour, best_winrate);
    printf("  WORST HOUR: %02d:00 (%.1f%% win rate)\n", worst_hour, worst_winrate);
    printf("\n");

    // =========================================================================
    // ANALYSIS 3: Number of Positions at Entry
    // =========================================================================
    printf("=======================================================\n");
    printf("ANALYSIS 3: POSITIONS OPEN AT ENTRY\n");
    printf("=======================================================\n");

    std::vector<double> winner_pos, loser_pos;
    for (const auto& t : winners) winner_pos.push_back(t.positions_at_entry);
    for (const auto& t : losers) loser_pos.push_back(t.positions_at_entry);

    Stats w_pos = CalculateStats(winner_pos);
    Stats l_pos = CalculateStats(loser_pos);

    printf("\nPositions already open when entering:\n");
    printf("  WINNERS: mean=%.2f, median=%.2f, max=%.0f\n", w_pos.mean, w_pos.median, w_pos.max);
    printf("  LOSERS:  mean=%.2f, median=%.2f, max=%.0f\n", l_pos.mean, l_pos.median, l_pos.max);

    // Breakdown by position count
    printf("\nWin rate by positions at entry:\n");
    int pos_winners[25] = {0}, pos_total[25] = {0};
    for (const auto& t : completed_trades) {
        int p = std::min(t.positions_at_entry, 24);
        pos_total[p]++;
        if (t.is_winner) pos_winners[p]++;
    }

    for (int p = 0; p <= 20; p++) {
        if (pos_total[p] > 10) {
            printf("  %2d positions: %3d trades, %.1f%% winners\n",
                   p, pos_total[p], pos_winners[p] * 100.0 / pos_total[p]);
        }
    }
    printf("\n");

    // =========================================================================
    // ANALYSIS 4: SMA Deviation at Entry
    // =========================================================================
    printf("=======================================================\n");
    printf("ANALYSIS 4: SMA DEVIATION AT ENTRY\n");
    printf("=======================================================\n");

    std::vector<double> winner_dev, loser_dev;
    for (const auto& t : winners) winner_dev.push_back(t.entry_sma_deviation);
    for (const auto& t : losers) loser_dev.push_back(t.entry_sma_deviation);

    Stats w_dev = CalculateStats(winner_dev);
    Stats l_dev = CalculateStats(loser_dev);

    printf("\nPrice deviation from SMA at entry (%%):\n");
    printf("  WINNERS: mean=%.3f%%, median=%.3f%%\n", w_dev.mean, w_dev.median);
    printf("  LOSERS:  mean=%.3f%%, median=%.3f%%\n", l_dev.mean, l_dev.median);
    printf("  INSIGHT: ");
    if (w_dev.mean > l_dev.mean) {
        printf("Winners entered when price was ABOVE SMA more often\n");
    } else {
        printf("Winners entered when price was BELOW SMA more often\n");
    }
    printf("\n");

    // =========================================================================
    // ANALYSIS 5: MAE and MFE
    // =========================================================================
    printf("=======================================================\n");
    printf("ANALYSIS 5: MAX ADVERSE/FAVORABLE EXCURSION\n");
    printf("=======================================================\n");

    std::vector<double> winner_mae, loser_mae;
    std::vector<double> winner_mfe, loser_mfe;
    for (const auto& t : winners) {
        winner_mae.push_back(t.max_adverse_excursion);
        winner_mfe.push_back(t.max_favorable_excursion);
    }
    for (const auto& t : losers) {
        loser_mae.push_back(t.max_adverse_excursion);
        loser_mfe.push_back(t.max_favorable_excursion);
    }

    Stats w_mae = CalculateStats(winner_mae);
    Stats l_mae = CalculateStats(loser_mae);
    Stats w_mfe = CalculateStats(winner_mfe);
    Stats l_mfe = CalculateStats(loser_mfe);

    printf("\nMax Adverse Excursion (how far against us):\n");
    printf("  WINNERS: mean=$%.2f, median=$%.2f, max=$%.2f\n", w_mae.mean, w_mae.median, w_mae.max);
    printf("  LOSERS:  mean=$%.2f, median=$%.2f, max=$%.2f\n", l_mae.mean, l_mae.median, l_mae.max);

    printf("\nMax Favorable Excursion (how close to TP):\n");
    printf("  WINNERS: mean=$%.2f (hit TP so MFE = TP distance)\n", w_mfe.mean);
    printf("  LOSERS:  mean=$%.2f, max=$%.2f\n", l_mfe.mean, l_mfe.max);
    printf("  INSIGHT: Losers got %.1f%% of the way to TP on average\n",
           (l_mfe.mean / SPACING) * 100);
    printf("\n");

    // =========================================================================
    // ANALYSIS 6: Time Held
    // =========================================================================
    printf("=======================================================\n");
    printf("ANALYSIS 6: TIME HELD (in ticks)\n");
    printf("=======================================================\n");

    std::vector<double> winner_time, loser_time;
    for (const auto& t : winners) winner_time.push_back((double)t.ticks_held);
    for (const auto& t : losers) loser_time.push_back((double)t.ticks_held);

    Stats w_time = CalculateStats(winner_time);
    Stats l_time = CalculateStats(loser_time);

    printf("\nTicks held before exit:\n");
    printf("  WINNERS: mean=%.0f, median=%.0f\n", w_time.mean, w_time.median);
    printf("  LOSERS:  mean=%.0f, median=%.0f\n", l_time.mean, l_time.median);
    printf("  INSIGHT: Losers held %.1fx longer than winners\n", l_time.mean / w_time.mean);
    printf("\n");

    // =========================================================================
    // PROFILE: Ideal vs Avoid
    // =========================================================================
    printf("=======================================================\n");
    printf("ACTIONABLE ENTRY FILTER RECOMMENDATIONS\n");
    printf("=======================================================\n\n");

    printf("=== IDEAL TRADE ENTRY CONDITIONS ===\n\n");

    // ATR recommendation
    printf("1. VOLATILITY FILTER:\n");
    if (w_ratio.mean < l_ratio.mean) {
        printf("   - Enter when ATR ratio < %.2f (winners avg: %.3f)\n",
               w_ratio.median, w_ratio.mean);
        printf("   - Current V7 threshold (%.2f) may need tightening\n", VOL_THRESHOLD);
    }

    // Hour recommendation
    printf("\n2. TIME OF DAY FILTER:\n");
    printf("   - BEST hours to trade: ");
    for (int h = 0; h < 24; h++) {
        int total = winner_hours.hour_counts[h] + loser_hours.hour_counts[h];
        if (total > 20) {
            double wr = winner_hours.hour_counts[h] * 100.0 / total;
            if (wr > 75) printf("%02d:00 (%.0f%%), ", h, wr);
        }
    }
    printf("\n");

    // Position count recommendation
    printf("\n3. POSITION COUNT FILTER:\n");
    int optimal_max = 0;
    for (int p = 0; p <= 20; p++) {
        if (pos_total[p] > 10 && pos_winners[p] * 100.0 / pos_total[p] > 70) {
            optimal_max = p;
        }
    }
    printf("   - Highest win rates when 0-%d positions open\n", optimal_max);
    printf("   - Consider reducing max_positions to %d\n", std::max(5, optimal_max));

    // SMA recommendation
    printf("\n4. SMA DEVIATION FILTER:\n");
    if (w_dev.mean > 0 && l_dev.mean < 0) {
        printf("   - Enter only when price is ABOVE SMA (uptrend)\n");
    } else if (w_dev.mean < 0 && l_dev.mean > 0) {
        printf("   - Enter only when price is BELOW SMA (mean reversion)\n");
    } else {
        printf("   - SMA deviation shows: winners mean=%.3f%%, losers mean=%.3f%%\n",
               w_dev.mean, l_dev.mean);
    }

    printf("\n\n=== AVOID ENTRY CONDITIONS ===\n\n");

    printf("1. AVOID when ATR ratio > %.2f (high volatility)\n", l_ratio.median);

    printf("\n2. AVOID trading at hours: ");
    for (int h = 0; h < 24; h++) {
        int total = winner_hours.hour_counts[h] + loser_hours.hour_counts[h];
        if (total > 20) {
            double wr = winner_hours.hour_counts[h] * 100.0 / total;
            if (wr < 60) printf("%02d:00 (%.0f%%), ", h, wr);
        }
    }
    printf("\n");

    printf("\n3. AVOID when >%d positions already open\n",
           std::max(5, (int)(l_pos.mean + l_pos.stddev)));

    printf("\n4. AVOID trades where price deviates >%.1f%% from SMA\n",
           std::max(fabs(w_dev.max), fabs(l_dev.max)) * 0.5);

    printf("\n\n=== PROPOSED NEW FILTER RULES ===\n\n");

    printf("// Add to V7/V8 strategy:\n");
    printf("bool IsIdealEntry() {\n");
    printf("    // 1. Volatility check (tighter than current)\n");
    printf("    if (atr_ratio > %.2f) return false;\n", w_ratio.median);
    printf("    \n");
    printf("    // 2. Position limit (based on win rate analysis)\n");
    printf("    if (open_positions > %d) return false;\n", std::max(5, optimal_max));
    printf("    \n");
    printf("    // 3. Time filter (avoid worst hours)\n");
    printf("    if (current_hour == %d) return false;  // Worst hour\n", worst_hour);
    printf("    \n");
    printf("    return true;\n");
    printf("}\n");

    printf("\n=======================================================\n");
    printf("ANALYSIS COMPLETE\n");
    printf("=======================================================\n");

    return 0;
}
