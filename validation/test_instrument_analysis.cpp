/**
 * Instrument Mean Reversion Analysis
 *
 * Compares XAUUSD (Gold) and EURUSD (Forex) for grid trading suitability.
 *
 * Analyzes:
 * 1. Hurst exponent estimate (H < 0.5 = mean reverting, H > 0.5 = trending)
 * 2. Autocorrelation (negative lag-1 = mean reverting)
 * 3. Mean reversion speed (how fast price returns to SMA)
 * 4. Range-to-trend ratio ((High-Low) / |Close-Open|)
 * 5. Volatility clustering (GARCH-like behavior)
 * 6. V7 grid strategy performance comparison
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <algorithm>

// ===========================================================================
// Data Structures
// ===========================================================================

struct Tick {
    double bid;
    double ask;
    double mid() const { return (bid + ask) / 2.0; }
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
    int entry_tick;
};

struct InstrumentStats {
    char name[32];
    size_t tick_count;
    double price_start;
    double price_end;
    double price_min;
    double price_max;
    double avg_spread;

    // Mean reversion metrics
    double hurst_exponent;
    double lag1_autocorr;
    double lag5_autocorr;
    double mean_reversion_speed;
    double range_to_trend_ratio;
    double volatility_clustering;

    // Grid strategy performance
    double return_pct;
    double max_dd;
    int trades_completed;
    int winning_trades;
    double win_rate;
    double avg_time_to_tp;  // Average ticks to reach TP
    double avg_profit_per_trade;
};

// ===========================================================================
// Statistical Functions
// ===========================================================================

/**
 * Calculate Hurst Exponent using Rescaled Range (R/S) Analysis
 *
 * H < 0.5: Mean reverting (anti-persistent)
 * H = 0.5: Random walk (no memory)
 * H > 0.5: Trending (persistent)
 */
double CalculateHurstExponent(const std::vector<double>& prices, int min_chunk, int max_chunk) {
    if (prices.size() < (size_t)max_chunk * 2) return 0.5;

    // Calculate log returns
    std::vector<double> returns;
    returns.reserve(prices.size() - 1);
    for (size_t i = 1; i < prices.size(); i++) {
        if (prices[i-1] > 0) {
            returns.push_back(log(prices[i] / prices[i-1]));
        }
    }

    if (returns.size() < (size_t)max_chunk) return 0.5;

    // R/S analysis for different chunk sizes
    std::vector<double> log_n, log_rs;

    for (int chunk_size = min_chunk; chunk_size <= max_chunk; chunk_size *= 2) {
        int num_chunks = (int)returns.size() / chunk_size;
        if (num_chunks < 2) break;

        double rs_sum = 0;
        int valid_chunks = 0;

        for (int c = 0; c < num_chunks; c++) {
            int start = c * chunk_size;

            // Mean of chunk
            double mean = 0;
            for (int i = 0; i < chunk_size; i++) {
                mean += returns[start + i];
            }
            mean /= chunk_size;

            // Cumulative deviation from mean
            std::vector<double> cum_dev(chunk_size);
            cum_dev[0] = returns[start] - mean;
            for (int i = 1; i < chunk_size; i++) {
                cum_dev[i] = cum_dev[i-1] + (returns[start + i] - mean);
            }

            // Range = max - min of cumulative deviation
            double max_dev = cum_dev[0], min_dev = cum_dev[0];
            for (int i = 1; i < chunk_size; i++) {
                max_dev = std::max(max_dev, cum_dev[i]);
                min_dev = std::min(min_dev, cum_dev[i]);
            }
            double range = max_dev - min_dev;

            // Standard deviation
            double var = 0;
            for (int i = 0; i < chunk_size; i++) {
                double d = returns[start + i] - mean;
                var += d * d;
            }
            double std_dev = sqrt(var / chunk_size);

            if (std_dev > 1e-10) {
                rs_sum += range / std_dev;
                valid_chunks++;
            }
        }

        if (valid_chunks > 0) {
            double rs_avg = rs_sum / valid_chunks;
            log_n.push_back(log((double)chunk_size));
            log_rs.push_back(log(rs_avg));
        }
    }

    // Linear regression to find Hurst exponent (slope of log(R/S) vs log(n))
    if (log_n.size() < 3) return 0.5;

    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    int n = (int)log_n.size();

    for (int i = 0; i < n; i++) {
        sum_x += log_n[i];
        sum_y += log_rs[i];
        sum_xy += log_n[i] * log_rs[i];
        sum_x2 += log_n[i] * log_n[i];
    }

    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);

    // Hurst exponent is the slope
    return std::max(0.0, std::min(1.0, slope));
}

/**
 * Calculate Autocorrelation at given lag
 *
 * Negative autocorrelation indicates mean reversion
 */
double CalculateAutocorrelation(const std::vector<double>& returns, int lag) {
    if (returns.size() <= (size_t)lag) return 0;

    int n = (int)returns.size() - lag;

    // Mean of both series
    double mean1 = 0, mean2 = 0;
    for (int i = 0; i < n; i++) {
        mean1 += returns[i];
        mean2 += returns[i + lag];
    }
    mean1 /= n;
    mean2 /= n;

    // Covariance and variances
    double cov = 0, var1 = 0, var2 = 0;
    for (int i = 0; i < n; i++) {
        double d1 = returns[i] - mean1;
        double d2 = returns[i + lag] - mean2;
        cov += d1 * d2;
        var1 += d1 * d1;
        var2 += d2 * d2;
    }

    double denom = sqrt(var1 * var2);
    if (denom < 1e-10) return 0;

    return cov / denom;
}

/**
 * Calculate Mean Reversion Speed
 *
 * Measures how quickly price returns to SMA after deviation
 * Higher value = faster mean reversion = better for grid trading
 */
double CalculateMeanReversionSpeed(const std::vector<double>& prices, int sma_period) {
    if (prices.size() < (size_t)sma_period * 2) return 0;

    // Calculate SMA
    std::vector<double> sma;
    sma.reserve(prices.size() - sma_period + 1);

    double sum = 0;
    for (int i = 0; i < sma_period; i++) {
        sum += prices[i];
    }
    sma.push_back(sum / sma_period);

    for (size_t i = sma_period; i < prices.size(); i++) {
        sum += prices[i] - prices[i - sma_period];
        sma.push_back(sum / sma_period);
    }

    // Track deviation crossings
    int total_ticks_to_cross = 0;
    int crossings = 0;

    bool above = prices[sma_period - 1] > sma[0];
    int ticks_since_cross = 0;

    for (size_t i = sma_period; i < prices.size(); i++) {
        size_t sma_idx = i - sma_period + 1;
        if (sma_idx >= sma.size()) break;

        bool now_above = prices[i] > sma[sma_idx];
        ticks_since_cross++;

        if (now_above != above) {
            // Crossed the SMA
            if (crossings > 0 && ticks_since_cross < (int)prices.size() / 10) {
                total_ticks_to_cross += ticks_since_cross;
            }
            crossings++;
            above = now_above;
            ticks_since_cross = 0;
        }
    }

    if (crossings <= 1) return 0;

    // Return inverse of average ticks (normalized by SMA period)
    double avg_ticks = (double)total_ticks_to_cross / (crossings - 1);
    return sma_period / avg_ticks;  // Higher = faster reversion
}

/**
 * Calculate Range-to-Trend Ratio
 *
 * (High - Low) / |Close - Open| over windows
 * Higher ratio = more ranging behavior = better for grid
 */
double CalculateRangeToTrendRatio(const std::vector<double>& prices, int window) {
    if (prices.size() < (size_t)window * 10) return 1.0;

    double total_ratio = 0;
    int count = 0;

    for (size_t i = window; i < prices.size(); i += window / 2) {
        size_t start = i - window;

        double open = prices[start];
        double close = prices[i - 1];
        double high = prices[start], low = prices[start];

        for (size_t j = start; j < i; j++) {
            high = std::max(high, prices[j]);
            low = std::min(low, prices[j]);
        }

        double range = high - low;
        double trend = fabs(close - open);

        if (trend > 1e-10) {
            total_ratio += range / trend;
            count++;
        }
    }

    return count > 0 ? total_ratio / count : 1.0;
}

/**
 * Calculate Volatility Clustering (GARCH-like)
 *
 * Measures autocorrelation of squared returns
 * Higher = more volatility clustering
 */
double CalculateVolatilityClustering(const std::vector<double>& returns) {
    if (returns.size() < 100) return 0;

    // Square returns
    std::vector<double> sq_returns;
    sq_returns.reserve(returns.size());
    for (double r : returns) {
        sq_returns.push_back(r * r);
    }

    // Autocorrelation of squared returns at lag 1
    return fabs(CalculateAutocorrelation(sq_returns, 1));
}

// ===========================================================================
// Data Loading
// ===========================================================================

std::vector<Tick> LoadTicksCSV(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;

    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    char line[512];

    // Skip header
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    ticks.reserve(max_ticks);

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
    }

    fclose(file);
    return ticks;
}

// ===========================================================================
// V7 Grid Strategy Test
// ===========================================================================

struct V7Config {
    int atr_short_period;
    int atr_long_period;
    double volatility_threshold;
    double tp_multiplier;
    double spacing;
    double contract_size;
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int max_positions;
};

class ATR {
    std::deque<double> ranges;
    int period;
    double sum;
    double last_price;
public:
    ATR(int p) : period(p), sum(0), last_price(0) {}

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

    double Get() const {
        return ranges.empty() ? 0 : sum / ranges.size();
    }

    bool IsReady() const {
        return (int)ranges.size() >= period;
    }
};

struct GridResult {
    double return_pct;
    double max_dd;
    int trades_completed;
    int winning_trades;
    double win_rate;
    double avg_time_to_tp;
    double avg_profit;
    int positions_opened;
};

GridResult RunV7Strategy(const std::vector<Tick>& ticks, const V7Config& cfg) {
    GridResult result = {0};
    if (ticks.empty()) return result;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);

    double total_time_to_tp = 0;
    int tp_count = 0;
    double total_profit = 0;

    for (int tick_idx = 0; tick_idx < (int)ticks.size(); tick_idx++) {
        const Tick& tick = ticks[tick_idx];

        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
        }

        // Peak equity tracking
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
        result.max_dd = std::max(result.max_dd, dd_pct);

        // Protection: Close ALL
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double profit = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += profit;
                total_profit += profit;
                result.trades_completed++;
                if (profit > 0) result.winning_trades++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // Protection: Partial close
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                double profit = (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * cfg.contract_size;
                balance += profit;
                total_profit += profit;
                result.trades_completed++;
                if (profit > 0) result.winning_trades++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TP
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double profit = (t->take_profit - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += profit;
                total_profit += profit;
                result.trades_completed++;
                result.winning_trades++;

                int time_to_tp = tick_idx - t->entry_tick;
                total_time_to_tp += time_to_tp;
                tp_count++;

                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Volatility filter
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
        }

        // Open new positions
        if (dd_pct < cfg.stop_new_at_dd && volatility_ok && (int)positions.size() < cfg.max_positions) {
            double lowest = 1e30, highest = -1e30;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + cfg.spacing) ||
                              (highest <= tick.ask - cfg.spacing);

            if (should_open) {
                double lot = 0.01;
                Trade* t = new Trade();
                t->id = next_id++;
                t->entry_price = tick.ask;
                t->lot_size = lot;
                t->take_profit = tick.ask + tick.spread() + (cfg.spacing * cfg.tp_multiplier);
                t->entry_tick = tick_idx;
                positions.push_back(t);
                result.positions_opened++;
            }
        }
    }

    // Close remaining
    for (Trade* t : positions) {
        double profit = (ticks.back().bid - t->entry_price) * t->lot_size * cfg.contract_size;
        balance += profit;
        total_profit += profit;
        result.trades_completed++;
        if (profit > 0) result.winning_trades++;
        delete t;
    }

    result.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    result.win_rate = result.trades_completed > 0 ?
                      100.0 * result.winning_trades / result.trades_completed : 0;
    result.avg_time_to_tp = tp_count > 0 ? total_time_to_tp / tp_count : 0;
    result.avg_profit = result.trades_completed > 0 ? total_profit / result.trades_completed : 0;

    return result;
}

// ===========================================================================
// Main Analysis
// ===========================================================================

InstrumentStats AnalyzeInstrument(const char* name, const std::vector<Tick>& ticks,
                                   double spacing, double contract_size) {
    InstrumentStats stats = {0};
    strncpy(stats.name, name, sizeof(stats.name) - 1);

    if (ticks.empty()) {
        printf("ERROR: No ticks for %s\n", name);
        return stats;
    }

    stats.tick_count = ticks.size();

    // Extract price series
    std::vector<double> prices;
    prices.reserve(ticks.size());
    for (const Tick& t : ticks) {
        prices.push_back(t.mid());
    }

    stats.price_start = prices.front();
    stats.price_end = prices.back();
    stats.price_min = *std::min_element(prices.begin(), prices.end());
    stats.price_max = *std::max_element(prices.begin(), prices.end());

    // Average spread
    double spread_sum = 0;
    for (const Tick& t : ticks) {
        spread_sum += t.spread();
    }
    stats.avg_spread = spread_sum / ticks.size();

    // Calculate returns
    std::vector<double> returns;
    returns.reserve(prices.size() - 1);
    for (size_t i = 1; i < prices.size(); i++) {
        if (prices[i-1] > 0) {
            returns.push_back(log(prices[i] / prices[i-1]));
        }
    }

    printf("\n========================================\n");
    printf("ANALYZING: %s\n", name);
    printf("========================================\n");
    printf("Ticks: %zu\n", stats.tick_count);
    printf("Price range: %.5f - %.5f\n", stats.price_min, stats.price_max);
    printf("Avg spread: %.6f\n", stats.avg_spread);

    // 1. Hurst Exponent
    printf("\nCalculating Hurst exponent...\n");
    stats.hurst_exponent = CalculateHurstExponent(prices, 16, 512);
    printf("  Hurst Exponent: %.4f\n", stats.hurst_exponent);
    if (stats.hurst_exponent < 0.45) {
        printf("  -> STRONG mean reversion\n");
    } else if (stats.hurst_exponent < 0.5) {
        printf("  -> Mild mean reversion\n");
    } else if (stats.hurst_exponent < 0.55) {
        printf("  -> Random walk (neutral)\n");
    } else {
        printf("  -> TRENDING behavior\n");
    }

    // 2. Autocorrelation
    printf("\nCalculating autocorrelation...\n");
    stats.lag1_autocorr = CalculateAutocorrelation(returns, 1);
    stats.lag5_autocorr = CalculateAutocorrelation(returns, 5);
    printf("  Lag-1 autocorrelation: %.4f\n", stats.lag1_autocorr);
    printf("  Lag-5 autocorrelation: %.4f\n", stats.lag5_autocorr);
    if (stats.lag1_autocorr < -0.05) {
        printf("  -> Negative autocorr = MEAN REVERTING\n");
    } else if (stats.lag1_autocorr > 0.05) {
        printf("  -> Positive autocorr = TRENDING\n");
    } else {
        printf("  -> Near zero = Random walk\n");
    }

    // 3. Mean Reversion Speed
    printf("\nCalculating mean reversion speed...\n");
    stats.mean_reversion_speed = CalculateMeanReversionSpeed(prices, 100);
    printf("  Mean Reversion Speed: %.4f\n", stats.mean_reversion_speed);
    if (stats.mean_reversion_speed > 1.5) {
        printf("  -> FAST mean reversion (excellent for grid)\n");
    } else if (stats.mean_reversion_speed > 1.0) {
        printf("  -> Moderate mean reversion\n");
    } else {
        printf("  -> Slow mean reversion\n");
    }

    // 4. Range-to-Trend Ratio
    printf("\nCalculating range-to-trend ratio...\n");
    stats.range_to_trend_ratio = CalculateRangeToTrendRatio(prices, 100);
    printf("  Range-to-Trend Ratio: %.4f\n", stats.range_to_trend_ratio);
    if (stats.range_to_trend_ratio > 5.0) {
        printf("  -> HIGH ranging (excellent for grid)\n");
    } else if (stats.range_to_trend_ratio > 3.0) {
        printf("  -> Moderate ranging\n");
    } else {
        printf("  -> Low ranging (trending)\n");
    }

    // 5. Volatility Clustering
    printf("\nCalculating volatility clustering...\n");
    stats.volatility_clustering = CalculateVolatilityClustering(returns);
    printf("  Volatility Clustering: %.4f\n", stats.volatility_clustering);
    if (stats.volatility_clustering > 0.3) {
        printf("  -> HIGH clustering (use volatility filter)\n");
    } else if (stats.volatility_clustering > 0.15) {
        printf("  -> Moderate clustering\n");
    } else {
        printf("  -> Low clustering\n");
    }

    // 6. Run V7 Strategy
    printf("\nRunning V7 grid strategy...\n");

    V7Config cfg;
    cfg.atr_short_period = 100;
    cfg.atr_long_period = 500;
    cfg.volatility_threshold = 0.8;
    cfg.tp_multiplier = 1.0;
    cfg.spacing = spacing;
    cfg.contract_size = contract_size;
    cfg.stop_new_at_dd = 5.0;
    cfg.partial_close_at_dd = 8.0;
    cfg.close_all_at_dd = 25.0;
    cfg.max_positions = 20;

    GridResult gr = RunV7Strategy(ticks, cfg);

    stats.return_pct = gr.return_pct;
    stats.max_dd = gr.max_dd;
    stats.trades_completed = gr.trades_completed;
    stats.winning_trades = gr.winning_trades;
    stats.win_rate = gr.win_rate;
    stats.avg_time_to_tp = gr.avg_time_to_tp;
    stats.avg_profit_per_trade = gr.avg_profit;

    printf("  Return: %.2f%%\n", stats.return_pct);
    printf("  Max DD: %.2f%%\n", stats.max_dd);
    printf("  Trades: %d (%.1f%% win rate)\n", stats.trades_completed, stats.win_rate);
    printf("  Avg time to TP: %.0f ticks\n", stats.avg_time_to_tp);
    printf("  Avg profit/trade: $%.2f\n", stats.avg_profit_per_trade);

    return stats;
}

void PrintComparison(const InstrumentStats& gold, const InstrumentStats& forex) {
    printf("\n");
    printf("================================================================\n");
    printf("         MEAN REVERSION CHARACTERISTICS COMPARISON\n");
    printf("================================================================\n");
    printf("\n");
    printf("%-30s %15s %15s %12s\n", "METRIC", "XAUUSD", "EURUSD", "BETTER");
    printf("%-30s %15s %15s %12s\n", "------", "------", "------", "------");

    // Hurst (lower = more mean reverting = better)
    const char* hurst_winner = gold.hurst_exponent < forex.hurst_exponent ? "XAUUSD" : "EURUSD";
    printf("%-30s %15.4f %15.4f %12s\n", "Hurst Exponent (< 0.5 better)",
           gold.hurst_exponent, forex.hurst_exponent, hurst_winner);

    // Lag-1 autocorr (more negative = better)
    const char* ac1_winner = gold.lag1_autocorr < forex.lag1_autocorr ? "XAUUSD" : "EURUSD";
    printf("%-30s %15.4f %15.4f %12s\n", "Lag-1 Autocorr (< 0 better)",
           gold.lag1_autocorr, forex.lag1_autocorr, ac1_winner);

    // Mean reversion speed (higher = better)
    const char* mrs_winner = gold.mean_reversion_speed > forex.mean_reversion_speed ? "XAUUSD" : "EURUSD";
    printf("%-30s %15.4f %15.4f %12s\n", "Mean Rev Speed (higher better)",
           gold.mean_reversion_speed, forex.mean_reversion_speed, mrs_winner);

    // Range-to-trend (higher = better)
    const char* rtr_winner = gold.range_to_trend_ratio > forex.range_to_trend_ratio ? "XAUUSD" : "EURUSD";
    printf("%-30s %15.4f %15.4f %12s\n", "Range/Trend Ratio (higher better)",
           gold.range_to_trend_ratio, forex.range_to_trend_ratio, rtr_winner);

    // Vol clustering (lower = better for simpler strategies)
    const char* vc_winner = gold.volatility_clustering < forex.volatility_clustering ? "XAUUSD" : "EURUSD";
    printf("%-30s %15.4f %15.4f %12s\n", "Vol Clustering (lower better)",
           gold.volatility_clustering, forex.volatility_clustering, vc_winner);

    printf("\n");
    printf("%-30s %15s %15s %12s\n", "STRATEGY PERFORMANCE", "XAUUSD", "EURUSD", "BETTER");
    printf("%-30s %15s %15s %12s\n", "--------------------", "------", "------", "------");

    // Return (higher = better)
    char gold_ret[32], forex_ret[32];
    snprintf(gold_ret, sizeof(gold_ret), "%.2f%%", gold.return_pct);
    snprintf(forex_ret, sizeof(forex_ret), "%.2f%%", forex.return_pct);
    const char* ret_winner = gold.return_pct > forex.return_pct ? "XAUUSD" : "EURUSD";
    printf("%-30s %15s %15s %12s\n", "Return", gold_ret, forex_ret, ret_winner);

    // Max DD (lower = better)
    char gold_dd[32], forex_dd[32];
    snprintf(gold_dd, sizeof(gold_dd), "%.2f%%", gold.max_dd);
    snprintf(forex_dd, sizeof(forex_dd), "%.2f%%", forex.max_dd);
    const char* dd_winner = gold.max_dd < forex.max_dd ? "XAUUSD" : "EURUSD";
    printf("%-30s %15s %15s %12s\n", "Max Drawdown (lower better)", gold_dd, forex_dd, dd_winner);

    // Win rate (higher = better)
    char gold_wr[32], forex_wr[32];
    snprintf(gold_wr, sizeof(gold_wr), "%.1f%%", gold.win_rate);
    snprintf(forex_wr, sizeof(forex_wr), "%.1f%%", forex.win_rate);
    const char* wr_winner = gold.win_rate > forex.win_rate ? "XAUUSD" : "EURUSD";
    printf("%-30s %15s %15s %12s\n", "Win Rate", gold_wr, forex_wr, wr_winner);

    // Time to TP (lower = better)
    const char* ttp_winner = gold.avg_time_to_tp < forex.avg_time_to_tp ? "XAUUSD" : "EURUSD";
    printf("%-30s %15.0f %15.0f %12s\n", "Avg Time to TP (ticks, lower)",
           gold.avg_time_to_tp, forex.avg_time_to_tp, ttp_winner);

    // Risk-adjusted return
    double gold_risk_adj = gold.max_dd > 0 ? gold.return_pct / gold.max_dd : 0;
    double forex_risk_adj = forex.max_dd > 0 ? forex.return_pct / forex.max_dd : 0;
    const char* ra_winner = gold_risk_adj > forex_risk_adj ? "XAUUSD" : "EURUSD";
    printf("%-30s %15.2f %15.2f %12s\n", "Return/MaxDD Ratio",
           gold_risk_adj, forex_risk_adj, ra_winner);

    // Calculate overall score
    printf("\n");
    printf("================================================================\n");
    printf("                      SCORING SUMMARY\n");
    printf("================================================================\n");

    int gold_score = 0, forex_score = 0;

    // Statistical metrics (weight: 2 each)
    if (gold.hurst_exponent < forex.hurst_exponent) gold_score += 2; else forex_score += 2;
    if (gold.lag1_autocorr < forex.lag1_autocorr) gold_score += 2; else forex_score += 2;
    if (gold.mean_reversion_speed > forex.mean_reversion_speed) gold_score += 2; else forex_score += 2;
    if (gold.range_to_trend_ratio > forex.range_to_trend_ratio) gold_score += 2; else forex_score += 2;

    // Performance metrics (weight: 3 each)
    if (gold.return_pct > forex.return_pct) gold_score += 3; else forex_score += 3;
    if (gold.max_dd < forex.max_dd) gold_score += 3; else forex_score += 3;
    if (gold.win_rate > forex.win_rate) gold_score += 3; else forex_score += 3;
    if (gold_risk_adj > forex_risk_adj) gold_score += 3; else forex_score += 3;

    printf("\nTotal Score (higher = better for grid trading):\n");
    printf("  XAUUSD: %d points\n", gold_score);
    printf("  EURUSD: %d points\n", forex_score);

    printf("\n");
    printf("================================================================\n");
    printf("                        CONCLUSION\n");
    printf("================================================================\n");
    printf("\n");

    if (gold_score > forex_score) {
        printf("WINNER: XAUUSD (Gold)\n\n");
        printf("Gold shows stronger mean reversion characteristics for grid trading.\n");
    } else if (forex_score > gold_score) {
        printf("WINNER: EURUSD\n\n");
        printf("EURUSD shows stronger mean reversion characteristics for grid trading.\n");
    } else {
        printf("RESULT: TIE\n\n");
        printf("Both instruments show similar suitability for grid trading.\n");
    }

    printf("\n");
    printf("KEY FINDINGS:\n");
    printf("\n");

    // Interpret Hurst
    if (gold.hurst_exponent < 0.5 && forex.hurst_exponent < 0.5) {
        printf("* Both instruments show mean reverting behavior (H < 0.5)\n");
    } else if (gold.hurst_exponent < 0.5) {
        printf("* XAUUSD is mean reverting, EURUSD is more random/trending\n");
    } else if (forex.hurst_exponent < 0.5) {
        printf("* EURUSD is mean reverting, XAUUSD is more random/trending\n");
    } else {
        printf("* Both instruments show trending/random behavior (H >= 0.5)\n");
    }

    // Interpret autocorrelation
    if (gold.lag1_autocorr < -0.02 || forex.lag1_autocorr < -0.02) {
        if (gold.lag1_autocorr < forex.lag1_autocorr) {
            printf("* XAUUSD has stronger negative autocorrelation (better for grid)\n");
        } else {
            printf("* EURUSD has stronger negative autocorrelation (better for grid)\n");
        }
    }

    // Interpret volatility clustering
    if (gold.volatility_clustering > 0.2 || forex.volatility_clustering > 0.2) {
        printf("* High volatility clustering detected - volatility filter recommended\n");
    }

    printf("\n");
    printf("================================================================\n");
    printf("       RECOMMENDED INSTRUMENT SELECTION CRITERIA\n");
    printf("================================================================\n");
    printf("\n");
    printf("For grid/mean reversion strategies, look for:\n\n");
    printf("1. HURST EXPONENT < 0.5\n");
    printf("   - Lower = stronger mean reversion\n");
    printf("   - Indicates price tends to return to average\n\n");
    printf("2. NEGATIVE LAG-1 AUTOCORRELATION\n");
    printf("   - Negative = price reversals are more likely than continuations\n");
    printf("   - Stronger negative = better for grid\n\n");
    printf("3. HIGH MEAN REVERSION SPEED\n");
    printf("   - Faster return to moving average = quicker TP hits\n");
    printf("   - Look for values > 1.0\n\n");
    printf("4. HIGH RANGE-TO-TREND RATIO\n");
    printf("   - Higher = more oscillating, less directional\n");
    printf("   - Look for values > 3.0\n\n");
    printf("5. MODERATE VOLATILITY CLUSTERING\n");
    printf("   - Some clustering allows profitable volatility filter\n");
    printf("   - Too high = unpredictable risk\n\n");
    printf("6. SUFFICIENT LIQUIDITY & TIGHT SPREADS\n");
    printf("   - Grid requires many small trades\n");
    printf("   - Wide spreads eat into thin margins\n\n");
    printf("7. HIGH WIN RATE IN BACKTESTS\n");
    printf("   - Grid strategies need 70%%+ win rate to be profitable\n");
    printf("   - Risk/reward is inverted (many small wins, few large losses)\n\n");
    printf("AVOID instruments that:\n");
    printf("- Have Hurst > 0.55 (strong trends)\n");
    printf("- Show positive autocorrelation (momentum behavior)\n");
    printf("- Have very high volatility clustering (unpredictable)\n");
    printf("- Are illiquid or have wide spreads\n");
    printf("\n");
}

int main() {
    printf("================================================================\n");
    printf("     INSTRUMENT MEAN REVERSION ANALYSIS FOR GRID TRADING\n");
    printf("================================================================\n");
    printf("\n");
    printf("This analysis compares XAUUSD and EURUSD to determine which\n");
    printf("instrument has stronger mean reversion characteristics,\n");
    printf("making it more suitable for grid trading strategies.\n");
    printf("\n");

    // Load XAUUSD data
    printf("Loading XAUUSD tick data...\n");
    const char* gold_file = "Grid/XAUUSD_TICKS_2025.csv";
    std::vector<Tick> gold_ticks = LoadTicksCSV(gold_file, 1000000);

    if (gold_ticks.empty()) {
        printf("Trying alternate path...\n");
        gold_ticks = LoadTicksCSV("validation/Grid/XAUUSD_TICKS_2025.csv", 1000000);
    }

    // Load EURUSD data
    printf("Loading EURUSD tick data...\n");
    const char* forex_file = "EURUSD_TICKS_202401.csv";
    std::vector<Tick> forex_ticks = LoadTicksCSV(forex_file, 1000000);

    if (forex_ticks.empty()) {
        printf("Trying alternate path...\n");
        forex_ticks = LoadTicksCSV("validation/EURUSD_TICKS_202401.csv", 1000000);
    }

    if (gold_ticks.empty()) {
        printf("ERROR: Could not load XAUUSD data\n");
        return 1;
    }

    if (forex_ticks.empty()) {
        printf("ERROR: Could not load EURUSD data\n");
        return 1;
    }

    printf("Loaded %zu XAUUSD ticks and %zu EURUSD ticks\n",
           gold_ticks.size(), forex_ticks.size());

    // Analyze each instrument
    // XAUUSD: spacing = 1.0 (dollars), contract_size = 100
    InstrumentStats gold_stats = AnalyzeInstrument("XAUUSD", gold_ticks, 1.0, 100.0);

    // EURUSD: spacing = 0.001 (10 pips), contract_size = 100000
    InstrumentStats forex_stats = AnalyzeInstrument("EURUSD", forex_ticks, 0.001, 100000.0);

    // Print comparison
    PrintComparison(gold_stats, forex_stats);

    return 0;
}
