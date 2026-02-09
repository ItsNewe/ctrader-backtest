#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include "../include/strategy_chaos_sync.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <map>

using namespace backtest;

// ============================================================================
// CHAOS SYNCHRONIZATION INVESTIGATION - TRUE PAIRS TRADING
// ============================================================================
// CONCEPT: Gold and silver prices are normally correlated. When the gold/silver
// ratio deviates significantly from its mean, trade the expected reversion.
//
// TRUE PAIRS TRADING:
// - When ratio high: SHORT gold, LONG silver (dollar-neutral)
// - When ratio low: LONG gold, SHORT silver (dollar-neutral)
// - P/L = gold_pnl + silver_pnl (legs may offset)
//
// HISTORICAL CONTEXT:
// - Long-term gold/silver ratio: ~60-70 (historically)
// - Can range from 30 (silver expensive) to 100+ (gold expensive)
// - Mean-reversion tendency over months/years
//
// CHALLENGE: We have tick data for 2025 only. Need to check:
// 1. What is the ratio range in 2025?
// 2. Does it show mean-reversion behavior at shorter timeframes?
// 3. Can we profit from desynchronization with pairs trading?
// ============================================================================

struct PriceBucket {
    double gold_price = 0.0;
    double silver_price = 0.0;
    std::string timestamp;
    bool has_gold = false;
    bool has_silver = false;
};

// Parse timestamp to bucket ID (seconds since reference)
long ParseTimestampToSeconds(const std::string& ts) {
    if (ts.size() < 19) return 0;
    int year = std::stoi(ts.substr(0, 4));
    int month = std::stoi(ts.substr(5, 2));
    int day = std::stoi(ts.substr(8, 2));
    int hour = std::stoi(ts.substr(11, 2));
    int minute = std::stoi(ts.substr(14, 2));
    int second = std::stoi(ts.substr(17, 2));
    int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
    days += month_days[month - 1] + day;
    if (month > 2 && year % 4 == 0) days++;
    return days * 86400L + hour * 3600L + minute * 60L + second;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  CHAOS SYNCHRONIZATION INVESTIGATION" << std::endl;
    std::cout << "  Gold/Silver Ratio Mean-Reversion Strategy" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Data paths
    std::string gold_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string silver_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    // Check files exist
    if (!std::ifstream(gold_path).good()) {
        std::cerr << "ERROR: Gold tick file not found: " << gold_path << std::endl;
        return 1;
    }
    if (!std::ifstream(silver_path).good()) {
        std::cerr << "ERROR: Silver tick file not found: " << silver_path << std::endl;
        return 1;
    }

    std::cout << "Loading tick data..." << std::endl;
    std::cout << "  Gold: " << gold_path << std::endl;
    std::cout << "  Silver: " << silver_path << std::endl;
    std::cout << std::endl;

    // ========================================================================
    // PHASE 1: Load and align tick data using time buckets
    // ========================================================================
    std::cout << "--- PHASE 1: Loading and Aligning Tick Data ---" << std::endl;

    const int BUCKET_SIZE = 60;  // 60-second buckets for analysis

    std::unordered_map<long, PriceBucket> price_buckets;

    // Load gold ticks
    std::cout << "Loading XAUUSD ticks..." << std::endl;
    {
        TickDataConfig config;
        config.file_path = gold_path;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        Tick tick;
        size_t count = 0;
        std::string first_ts, last_ts;

        while (manager.GetNextTick(tick)) {
            // Filter to 2025 only
            if (tick.timestamp < "2025.01.01" || tick.timestamp >= "2025.12.30") {
                continue;
            }

            long bucket = ParseTimestampToSeconds(tick.timestamp) / BUCKET_SIZE;
            auto& pb = price_buckets[bucket];
            pb.gold_price = tick.bid;
            pb.has_gold = true;
            if (pb.timestamp.empty()) pb.timestamp = tick.timestamp;

            if (first_ts.empty()) first_ts = tick.timestamp;
            last_ts = tick.timestamp;
            count++;

            if (count % 5000000 == 0) {
                std::cout << "  Processed " << count << " gold ticks..." << std::endl;
            }
        }
        std::cout << "  Loaded " << count << " gold ticks (" << first_ts << " to " << last_ts << ")" << std::endl;
    }

    // Load silver ticks
    std::cout << "Loading XAGUSD ticks..." << std::endl;
    std::vector<Tick> silver_ticks;  // Store for strategy use
    {
        TickDataConfig config;
        config.file_path = silver_path;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        Tick tick;
        size_t count = 0;
        std::string first_ts, last_ts;

        while (manager.GetNextTick(tick)) {
            // Filter to 2025 only
            if (tick.timestamp < "2025.01.01" || tick.timestamp >= "2025.12.30") {
                continue;
            }

            long bucket = ParseTimestampToSeconds(tick.timestamp) / BUCKET_SIZE;
            auto& pb = price_buckets[bucket];
            pb.silver_price = tick.bid;
            pb.has_silver = true;
            if (pb.timestamp.empty()) pb.timestamp = tick.timestamp;

            silver_ticks.push_back(tick);

            if (first_ts.empty()) first_ts = tick.timestamp;
            last_ts = tick.timestamp;
            count++;

            if (count % 5000000 == 0) {
                std::cout << "  Processed " << count << " silver ticks..." << std::endl;
            }
        }
        std::cout << "  Loaded " << count << " silver ticks (" << first_ts << " to " << last_ts << ")" << std::endl;
    }

    // ========================================================================
    // PHASE 2: Analyze Gold/Silver Ratio
    // ========================================================================
    std::cout << std::endl;
    std::cout << "--- PHASE 2: Gold/Silver Ratio Analysis ---" << std::endl;

    std::vector<double> ratios;
    std::vector<long> ratio_buckets;
    size_t matched_buckets = 0;
    size_t gold_only = 0;
    size_t silver_only = 0;

    for (auto& [bucket, pb] : price_buckets) {
        if (pb.has_gold && pb.has_silver && pb.silver_price > 0) {
            double ratio = pb.gold_price / pb.silver_price;
            ratios.push_back(ratio);
            ratio_buckets.push_back(bucket);
            matched_buckets++;
        } else if (pb.has_gold && !pb.has_silver) {
            gold_only++;
        } else if (!pb.has_gold && pb.has_silver) {
            silver_only++;
        }
    }

    std::cout << "  Time bucket size: " << BUCKET_SIZE << " seconds" << std::endl;
    std::cout << "  Total buckets with both prices: " << matched_buckets << std::endl;
    std::cout << "  Buckets with gold only: " << gold_only << std::endl;
    std::cout << "  Buckets with silver only: " << silver_only << std::endl;
    std::cout << "  Match rate: " << std::fixed << std::setprecision(1)
              << (100.0 * matched_buckets / (matched_buckets + gold_only + silver_only)) << "%" << std::endl;

    if (ratios.empty()) {
        std::cerr << "ERROR: No matched price data found!" << std::endl;
        return 1;
    }

    // Calculate statistics
    std::sort(ratios.begin(), ratios.end());
    double sum = 0.0;
    for (double r : ratios) sum += r;
    double mean = sum / ratios.size();

    double sum_sq = 0.0;
    for (double r : ratios) sum_sq += (r - mean) * (r - mean);
    double std_dev = std::sqrt(sum_sq / ratios.size());

    double min_ratio = ratios.front();
    double max_ratio = ratios.back();
    double median = ratios[ratios.size() / 2];
    double p10 = ratios[ratios.size() * 10 / 100];
    double p90 = ratios[ratios.size() * 90 / 100];

    std::cout << std::endl;
    std::cout << "  GOLD/SILVER RATIO STATISTICS (2025):" << std::endl;
    std::cout << "  -------------------------------------" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Min:    " << min_ratio << std::endl;
    std::cout << "  P10:    " << p10 << std::endl;
    std::cout << "  Median: " << median << std::endl;
    std::cout << "  Mean:   " << mean << std::endl;
    std::cout << "  P90:    " << p90 << std::endl;
    std::cout << "  Max:    " << max_ratio << std::endl;
    std::cout << "  StdDev: " << std_dev << std::endl;
    std::cout << "  Range:  " << (max_ratio - min_ratio) << " (" << ((max_ratio - min_ratio) / mean * 100) << "% of mean)" << std::endl;

    // ========================================================================
    // PHASE 3: Analyze Mean-Reversion Behavior
    // ========================================================================
    std::cout << std::endl;
    std::cout << "--- PHASE 3: Mean-Reversion Analysis ---" << std::endl;

    // Count how often ratio crosses 1 and 2 std devs, and what happens after
    int crosses_1std_up = 0, crosses_1std_down = 0;
    int crosses_2std_up = 0, crosses_2std_down = 0;
    int reverts_1std_up = 0, reverts_1std_down = 0;
    int reverts_2std_up = 0, reverts_2std_down = 0;

    // Sort by time bucket for sequential analysis
    std::vector<std::pair<long, double>> sorted_ratios;
    for (auto& [bucket, pb] : price_buckets) {
        if (pb.has_gold && pb.has_silver && pb.silver_price > 0) {
            double ratio = pb.gold_price / pb.silver_price;
            sorted_ratios.push_back({bucket, ratio});
        }
    }
    std::sort(sorted_ratios.begin(), sorted_ratios.end());

    // Rolling mean/std with 60-bucket (1 hour) window
    const int LOOKBACK = 60;
    std::deque<double> window;
    std::vector<double> z_scores;

    for (size_t i = 0; i < sorted_ratios.size(); i++) {
        double ratio = sorted_ratios[i].second;
        window.push_back(ratio);
        if (window.size() > LOOKBACK) window.pop_front();

        if (window.size() >= LOOKBACK) {
            // Calculate rolling stats
            double w_sum = 0.0;
            for (double r : window) w_sum += r;
            double w_mean = w_sum / window.size();

            double w_sum_sq = 0.0;
            for (double r : window) w_sum_sq += (r - w_mean) * (r - w_mean);
            double w_std = std::sqrt(w_sum_sq / window.size());

            if (w_std > 0) {
                double z = (ratio - w_mean) / w_std;
                z_scores.push_back(z);

                // Check for extremes and subsequent reversion
                if (i > 0 && z_scores.size() > 1) {
                    double prev_z = z_scores[z_scores.size() - 2];

                    // Crossed above +1 std
                    if (prev_z <= 1.0 && z > 1.0) {
                        crosses_1std_up++;
                        // Check if it reverts within next 60 buckets
                        for (size_t j = i + 1; j < std::min(i + 60, sorted_ratios.size()); j++) {
                            // Simplified: just check if ratio goes back down
                            if (sorted_ratios[j].second < ratio) {
                                reverts_1std_up++;
                                break;
                            }
                        }
                    }

                    // Crossed below -1 std
                    if (prev_z >= -1.0 && z < -1.0) {
                        crosses_1std_down++;
                        for (size_t j = i + 1; j < std::min(i + 60, sorted_ratios.size()); j++) {
                            if (sorted_ratios[j].second > ratio) {
                                reverts_1std_down++;
                                break;
                            }
                        }
                    }

                    // Crossed above +2 std
                    if (prev_z <= 2.0 && z > 2.0) {
                        crosses_2std_up++;
                        for (size_t j = i + 1; j < std::min(i + 60, sorted_ratios.size()); j++) {
                            if (sorted_ratios[j].second < ratio) {
                                reverts_2std_up++;
                                break;
                            }
                        }
                    }

                    // Crossed below -2 std
                    if (prev_z >= -2.0 && z < -2.0) {
                        crosses_2std_down++;
                        for (size_t j = i + 1; j < std::min(i + 60, sorted_ratios.size()); j++) {
                            if (sorted_ratios[j].second > ratio) {
                                reverts_2std_down++;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    std::cout << "  Rolling window: " << LOOKBACK << " minutes (" << (LOOKBACK * BUCKET_SIZE / 60) << " hours)" << std::endl;
    std::cout << std::endl;
    std::cout << "  Deviation crossings and reversion rate:" << std::endl;
    std::cout << "  +1 StdDev: " << crosses_1std_up << " crosses, "
              << (crosses_1std_up > 0 ? (100.0 * reverts_1std_up / crosses_1std_up) : 0)
              << "% reverted within 1hr" << std::endl;
    std::cout << "  -1 StdDev: " << crosses_1std_down << " crosses, "
              << (crosses_1std_down > 0 ? (100.0 * reverts_1std_down / crosses_1std_down) : 0)
              << "% reverted within 1hr" << std::endl;
    std::cout << "  +2 StdDev: " << crosses_2std_up << " crosses, "
              << (crosses_2std_up > 0 ? (100.0 * reverts_2std_up / crosses_2std_up) : 0)
              << "% reverted within 1hr" << std::endl;
    std::cout << "  -2 StdDev: " << crosses_2std_down << " crosses, "
              << (crosses_2std_down > 0 ? (100.0 * reverts_2std_down / crosses_2std_down) : 0)
              << "% reverted within 1hr" << std::endl;

    // ========================================================================
    // PHASE 4: TRUE PAIRS TRADING BACKTEST
    // ========================================================================
    std::cout << std::endl;
    std::cout << "--- PHASE 4: True Pairs Trading Backtest ---" << std::endl;
    std::cout << "(Dollar-neutral: long gold + short silver or vice versa)" << std::endl;

    // Create a map of time bucket -> silver price for fast lookup
    std::map<long, double> silver_bucket_prices;
    std::map<long, double> silver_bucket_asks;
    for (const auto& tick : silver_ticks) {
        long bucket = ParseTimestampToSeconds(tick.timestamp);
        silver_bucket_prices[bucket] = tick.bid;
        silver_bucket_asks[bucket] = tick.ask;
    }

    // Test multiple configurations for TRUE pairs trading
    struct PairsConfig {
        std::string name;
        double entry_std;       // Entry threshold in std devs
        double exit_std;        // Exit when z-score crosses this toward 0
        int lookback_buckets;   // Number of buckets for rolling stats
        double gold_lots;       // Fixed gold position size
        double sl_pct;          // Stop loss as % of pair value
    };

    std::vector<PairsConfig> pairs_configs = {
        {"Entry2.0_Exit0.5_1h", 2.0, 0.5, 60, 0.01, 2.0},
        {"Entry2.5_Exit0.5_1h", 2.5, 0.5, 60, 0.01, 2.0},
        {"Entry3.0_Exit0.5_1h", 3.0, 0.5, 60, 0.01, 2.0},
        {"Entry2.0_Exit0.0_1h", 2.0, 0.0, 60, 0.01, 2.0},
        {"Entry2.0_Exit0.5_2h", 2.0, 0.5, 120, 0.01, 2.0},
        {"Entry2.0_Exit0.5_4h", 2.0, 0.5, 240, 0.01, 2.0},
        {"Entry1.5_Exit0.3_1h", 1.5, 0.3, 60, 0.01, 2.0},
        {"Entry2.0_Exit0.5_30m", 2.0, 0.5, 30, 0.01, 2.0},
        {"BigLots_2.0_0.5_1h", 2.0, 0.5, 60, 0.05, 2.0},
        {"Tight_SL_2.0_0.5_1h", 2.0, 0.5, 60, 0.01, 1.0},
    };

    // Configuration constants
    const double GOLD_CONTRACT = 100.0;
    const double SILVER_CONTRACT = 5000.0;
    const double GOLD_SWAP_LONG = -66.99 * 0.01 * GOLD_CONTRACT;  // Per lot per day
    const double GOLD_SWAP_SHORT = 41.2 * 0.01 * GOLD_CONTRACT;
    const double SILVER_SWAP_LONG = -15.0 * 0.001 * SILVER_CONTRACT;
    const double SILVER_SWAP_SHORT = 0.0;
    const double INITIAL_BALANCE = 10000.0;

    std::cout << std::endl;
    std::cout << std::left << std::setw(22) << "Config"
              << std::right << std::setw(10) << "Final$"
              << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Gold_PnL"
              << std::setw(10) << "Silv_PnL"
              << std::setw(10) << "Swap$"
              << std::setw(8) << "WinRate"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (const auto& pc : pairs_configs) {
        // Pairs trading state
        double balance = INITIAL_BALANCE;
        double peak_balance = balance;
        double max_dd_pct = 0.0;
        double gold_total_pnl = 0.0;
        double silver_total_pnl = 0.0;
        double swap_total = 0.0;
        int total_trades = 0;
        int winning_trades = 0;

        // Position state
        bool in_position = false;
        int position_dir = 0;  // 1 = long ratio (long gold, short silver), -1 = short ratio
        double gold_entry = 0.0;
        double silver_entry = 0.0;
        double gold_lots = 0.0;
        double silver_lots = 0.0;
        std::string entry_time;
        int days_held = 0;

        // Rolling ratio calculation
        std::deque<double> ratio_window;
        double last_day_bucket = 0;

        // Process synchronized data
        for (const auto& [bucket, ratio] : sorted_ratios) {
            // Get current prices
            auto gold_it = price_buckets.find(bucket);
            if (gold_it == price_buckets.end() || !gold_it->second.has_gold || !gold_it->second.has_silver) {
                continue;
            }

            double gold_bid = gold_it->second.gold_price;
            double gold_ask = gold_bid + 0.30;  // Approximate spread
            double silver_bid = gold_it->second.silver_price;
            double silver_ask = silver_bid + 0.015;  // Approximate spread

            // Update ratio window
            ratio_window.push_back(ratio);
            while (ratio_window.size() > (size_t)pc.lookback_buckets) {
                ratio_window.pop_front();
            }

            // Need enough data for stats
            if (ratio_window.size() < (size_t)pc.lookback_buckets) {
                continue;
            }

            // Calculate rolling mean and std
            double r_sum = 0.0;
            for (double r : ratio_window) r_sum += r;
            double r_mean = r_sum / ratio_window.size();

            double r_sum_sq = 0.0;
            for (double r : ratio_window) r_sum_sq += (r - r_mean) * (r - r_mean);
            double r_std = std::sqrt(r_sum_sq / ratio_window.size());

            if (r_std < 0.001) continue;  // Skip if no variance

            double z_score = (ratio - r_mean) / r_std;

            // Swap charges (daily)
            long day_bucket = bucket / 86400;
            if (in_position && day_bucket != last_day_bucket && last_day_bucket != 0) {
                // Charge swap for overnight hold
                double gold_swap = (gold_lots > 0) ? GOLD_SWAP_LONG * gold_lots : GOLD_SWAP_SHORT * (-gold_lots);
                double silver_swap = (silver_lots > 0) ? SILVER_SWAP_LONG * silver_lots : SILVER_SWAP_SHORT * (-silver_lots);
                swap_total += gold_swap + silver_swap;
                balance += gold_swap + silver_swap;
                days_held++;
            }
            last_day_bucket = day_bucket;

            // Update unrealized P/L and check for stop loss
            if (in_position) {
                // Calculate current P/L
                double gold_exit = (gold_lots > 0) ? gold_bid : gold_ask;
                double silver_exit = (silver_lots > 0) ? silver_bid : silver_ask;

                double gold_pnl = (gold_exit - gold_entry) * std::abs(gold_lots) * GOLD_CONTRACT;
                if (gold_lots < 0) gold_pnl = -gold_pnl;

                double silver_pnl = (silver_exit - silver_entry) * std::abs(silver_lots) * SILVER_CONTRACT;
                if (silver_lots < 0) silver_pnl = -silver_pnl;

                double unrealized = gold_pnl + silver_pnl;
                double pair_value = gold_entry * std::abs(gold_lots) * GOLD_CONTRACT +
                                   silver_entry * std::abs(silver_lots) * SILVER_CONTRACT;

                // Check stop loss
                if (pair_value > 0 && unrealized < -pair_value * pc.sl_pct / 100.0) {
                    // Close at stop loss
                    balance += unrealized;
                    gold_total_pnl += gold_pnl;
                    silver_total_pnl += silver_pnl;
                    total_trades++;
                    if (unrealized > 0) winning_trades++;
                    in_position = false;
                    position_dir = 0;
                    continue;
                }

                // Check exit signal (ratio returning to mean)
                bool should_exit = false;
                if (position_dir == 1 && z_score > -pc.exit_std) {
                    // Was long ratio (expecting ratio to rise), now returned
                    should_exit = true;
                } else if (position_dir == -1 && z_score < pc.exit_std) {
                    // Was short ratio (expecting ratio to fall), now returned
                    should_exit = true;
                }

                if (should_exit) {
                    balance += unrealized;
                    gold_total_pnl += gold_pnl;
                    silver_total_pnl += silver_pnl;
                    total_trades++;
                    if (unrealized > 0) winning_trades++;
                    in_position = false;
                    position_dir = 0;
                }
            }

            // Check entry signals
            if (!in_position) {
                if (z_score > pc.entry_std) {
                    // Ratio high (gold expensive vs silver)
                    // SHORT gold, LONG silver
                    position_dir = -1;
                    gold_lots = -pc.gold_lots;

                    // Dollar-neutral sizing
                    double gold_notional = pc.gold_lots * gold_ask * GOLD_CONTRACT;
                    silver_lots = gold_notional / (silver_ask * SILVER_CONTRACT);

                    gold_entry = gold_bid;  // Short gold at bid
                    silver_entry = silver_ask;  // Long silver at ask
                    entry_time = gold_it->second.timestamp;
                    in_position = true;
                    days_held = 0;
                } else if (z_score < -pc.entry_std) {
                    // Ratio low (gold cheap vs silver)
                    // LONG gold, SHORT silver
                    position_dir = 1;
                    gold_lots = pc.gold_lots;

                    // Dollar-neutral sizing
                    double gold_notional = pc.gold_lots * gold_ask * GOLD_CONTRACT;
                    silver_lots = -gold_notional / (silver_bid * SILVER_CONTRACT);

                    gold_entry = gold_ask;  // Long gold at ask
                    silver_entry = silver_bid;  // Short silver at bid
                    entry_time = gold_it->second.timestamp;
                    in_position = true;
                    days_held = 0;
                }
            }

            // Update max drawdown
            if (balance > peak_balance) peak_balance = balance;
            double dd_pct = (peak_balance - balance) / peak_balance * 100.0;
            if (dd_pct > max_dd_pct) max_dd_pct = dd_pct;
        }

        // Close any remaining position
        if (in_position) {
            // Use last known prices
            auto last_it = sorted_ratios.rbegin();
            if (last_it != sorted_ratios.rend()) {
                auto gold_it = price_buckets.find(last_it->first);
                if (gold_it != price_buckets.end()) {
                    double gold_exit = (gold_lots > 0) ? gold_it->second.gold_price : gold_it->second.gold_price + 0.30;
                    double silver_exit = (silver_lots > 0) ? gold_it->second.silver_price : gold_it->second.silver_price + 0.015;

                    double gold_pnl = (gold_exit - gold_entry) * std::abs(gold_lots) * GOLD_CONTRACT;
                    if (gold_lots < 0) gold_pnl = -gold_pnl;

                    double silver_pnl = (silver_exit - silver_entry) * std::abs(silver_lots) * SILVER_CONTRACT;
                    if (silver_lots < 0) silver_pnl = -silver_pnl;

                    balance += gold_pnl + silver_pnl;
                    gold_total_pnl += gold_pnl;
                    silver_total_pnl += silver_pnl;
                    total_trades++;
                    if (gold_pnl + silver_pnl > 0) winning_trades++;
                }
            }
        }

        double win_rate = total_trades > 0 ? (100.0 * winning_trades / total_trades) : 0;

        std::cout << std::left << std::setw(22) << pc.name
                  << std::right << std::fixed << std::setprecision(0)
                  << std::setw(10) << balance
                  << std::setprecision(2)
                  << std::setw(7) << (balance / INITIAL_BALANCE) << "x"
                  << std::setprecision(1)
                  << std::setw(8) << max_dd_pct
                  << std::setw(8) << total_trades
                  << std::setprecision(0)
                  << std::setw(10) << gold_total_pnl
                  << std::setw(10) << silver_total_pnl
                  << std::setw(10) << swap_total
                  << std::setprecision(1)
                  << std::setw(8) << win_rate << "%"
                  << std::endl;
    }

    // ========================================================================
    // PHASE 5: Gold-Only Strategy (for comparison)
    // ========================================================================
    std::cout << std::endl;
    std::cout << "--- PHASE 5: Gold-Only Strategy (Comparison) ---" << std::endl;
    std::cout << "(Trade gold based on ratio signal, no silver leg)" << std::endl;

    // Test multiple configurations
    struct TestConfig {
        std::string name;
        double entry_std;
        double exit_std;
        int lookback_sec;
        double lot_size;
        double sl_pct;
        double tp_pct;
    };

    std::vector<TestConfig> configs = {
        {"Conservative", 3.0, 0.5, 3600, 0.01, 1.0, 2.0},
        {"Moderate", 2.5, 0.5, 1800, 0.01, 1.0, 1.5},
        {"Aggressive", 2.0, 0.3, 1800, 0.01, 0.8, 1.0},
        {"VeryAggressive", 1.5, 0.2, 900, 0.01, 0.5, 0.8},
        {"LongWindow", 2.5, 0.5, 7200, 0.01, 1.5, 2.5},
        {"TightEntry", 3.5, 0.3, 1800, 0.02, 0.8, 1.2},
    };

    std::cout << std::endl;
    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(8) << "Final$"
              << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "HiSig"
              << std::setw(8) << "LoSig"
              << std::setw(8) << "Swap$"
              << std::setw(12) << "Status"
              << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (const auto& tc : configs) {
        // Create gold engine config
        TickDataConfig tick_config;
        tick_config.file_path = gold_path;
        tick_config.format = TickDataFormat::MT5_CSV;
        tick_config.load_all_into_memory = false;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.account_currency = "USD";
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.margin_rate = 1.0;
        config.pip_size = 0.01;
        config.swap_long = -66.99;
        config.swap_short = 41.2;
        config.swap_mode = 1;
        config.swap_3days = 3;
        config.start_date = "2025.01.01";
        config.end_date = "2025.12.30";
        config.tick_data_config = tick_config;
        config.verbose = false;

        try {
            TickBasedEngine engine(config);

            // Configure strategy
            StrategyChaosSync::Config sc;
            sc.lookback_seconds = tc.lookback_sec;
            sc.entry_std_devs = tc.entry_std;
            sc.exit_std_devs = tc.exit_std;
            sc.lot_size = tc.lot_size;
            sc.max_lots = 0.10;
            // contract_size/leverage now read from engine.GetConfig()
            sc.max_positions = 5;
            sc.sl_pct = tc.sl_pct;
            sc.tp_pct = tc.tp_pct;
            sc.warmup_buckets = tc.lookback_sec;  // Match warmup to lookback
            sc.bucket_size_seconds = 1;
            sc.gold_only = true;

            StrategyChaosSync strategy(sc);

            // Load silver data into strategy
            strategy.LoadSilverData(silver_ticks);

            // Run backtest
            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto results = engine.GetResults();

            std::string status = results.stop_out_occurred ? "STOP-OUT" : "ok";

            std::cout << std::left << std::setw(18) << tc.name
                      << std::right << std::fixed << std::setprecision(0)
                      << std::setw(8) << results.final_balance
                      << std::setprecision(2)
                      << std::setw(7) << (results.final_balance / results.initial_balance) << "x"
                      << std::setprecision(1)
                      << std::setw(8) << strategy.GetMaxDDPct()
                      << std::setw(8) << results.total_trades
                      << std::setw(8) << strategy.GetRatioSignalsHigh()
                      << std::setw(8) << strategy.GetRatioSignalsLow()
                      << std::setprecision(0)
                      << std::setw(8) << results.total_swap_charged
                      << "  " << std::left << status
                      << std::endl;

        } catch (const std::exception& e) {
            std::cout << std::left << std::setw(18) << tc.name
                      << "  ERROR: " << e.what() << std::endl;
        }
    }

    // ========================================================================
    // PHASE 6: Summary
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY & CONCLUSIONS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "KEY FINDINGS:" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  1. Gold/Silver Ratio in 2025: " << min_ratio << " - " << max_ratio << std::endl;
    std::cout << "     (Mean: " << mean << ", StdDev: " << std_dev << ")" << std::endl;
    std::cout << "     Coefficient of Variation: " << (std_dev / mean * 100) << "%" << std::endl;
    std::cout << std::endl;
    std::cout << "  2. Data alignment: " << matched_buckets << " synchronized price points" << std::endl;
    std::cout << "     Match rate: " << (100.0 * matched_buckets / (matched_buckets + gold_only + silver_only)) << "%" << std::endl;
    std::cout << std::endl;
    std::cout << "  3. Mean-reversion at 2 std devs: "
              << "High=" << (crosses_2std_up > 0 ? (100.0 * reverts_2std_up / crosses_2std_up) : 0) << "%, "
              << "Low=" << (crosses_2std_down > 0 ? (100.0 * reverts_2std_down / crosses_2std_down) : 0) << "%" << std::endl;
    std::cout << std::endl;
    std::cout << "BASELINE COMPARISON:" << std::endl;
    std::cout << "  FillUpOscillation XAUUSD: 6.57x return, 67% DD" << std::endl;
    std::cout << "  FillUpOscillation XAGUSD (pct): 43.4x return, 29% DD" << std::endl;
    std::cout << std::endl;
    std::cout << "PAIRS TRADING CONCLUSIONS:" << std::endl;
    std::cout << "  - Dollar-neutral pairs trade: long one metal, short the other" << std::endl;
    std::cout << "  - Profit from ratio mean-reversion, not directional moves" << std::endl;
    std::cout << "  - Swap costs on both legs can be significant" << std::endl;
    std::cout << "  - Compare returns and DD to single-instrument baselines above" << std::endl;
    std::cout << std::endl;

    return 0;
}
