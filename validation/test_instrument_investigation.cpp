/**
 * Instrument Investigation: Capital-Efficient Structural Patterns
 *
 * For each instrument, we measure:
 * 1. Oscillation characteristics (amplitude, frequency, duration)
 * 2. Cost structure (spread, swap if applicable)
 * 3. Capital efficiency potential (profit per unit of discomfort)
 *
 * The goal is to identify which discomfort premiums exist and their efficiency.
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <map>

using namespace backtest;

struct OscillationStats {
    // Basic counts
    size_t total_ticks = 0;
    size_t oscillation_count = 0;

    // Price characteristics
    double min_price = 1e9;
    double max_price = 0;
    double avg_price = 0;
    double price_range_pct = 0;

    // Oscillation amplitudes (as % of price)
    std::vector<double> amplitudes_pct;
    double median_amplitude_pct = 0;
    double p25_amplitude_pct = 0;
    double p75_amplitude_pct = 0;

    // Oscillation durations (in ticks)
    std::vector<size_t> durations;
    double median_duration = 0;

    // Spread characteristics
    double avg_spread = 0;
    double avg_spread_pct = 0;
    double max_spread = 0;

    // Hourly volatility (range as % of price)
    std::vector<double> hourly_ranges_pct;
    double median_hourly_vol_pct = 0;

    // Downswing vs upswing asymmetry
    double avg_downswing_speed = 0;  // ticks per % move
    double avg_upswing_speed = 0;

    // Capital efficiency indicators
    double oscillations_per_day = 0;
    double profit_potential_per_oscillation = 0;  // amplitude - spread (as % of price)
    double daily_profit_potential_pct = 0;
};

struct InstrumentConfig {
    std::string name;
    std::string file_path;
    double contract_size;
    double pip_size;
    double swing_threshold_pct;  // Minimum swing to count as oscillation
};

OscillationStats AnalyzeInstrument(const InstrumentConfig& config) {
    OscillationStats stats;

    std::cout << "\n=== Analyzing " << config.name << " ===" << std::endl;
    std::cout << "Loading: " << config.file_path << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = config.file_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    // Manager initializes automatically in constructor

    // Load all ticks
    std::vector<Tick> ticks;
    Tick tick;
    while (manager.GetNextTick(tick)) {
        ticks.push_back(tick);
    }

    stats.total_ticks = ticks.size();
    std::cout << "Loaded " << stats.total_ticks << " ticks" << std::endl;

    if (ticks.empty()) return stats;

    // Calculate basic price stats
    double price_sum = 0;
    for (const auto& t : ticks) {
        double mid = (t.bid + t.ask) / 2.0;
        stats.min_price = std::min(stats.min_price, mid);
        stats.max_price = std::max(stats.max_price, mid);
        price_sum += mid;

        double spread = t.ask - t.bid;
        stats.avg_spread += spread;
        stats.max_spread = std::max(stats.max_spread, spread);
    }
    stats.avg_price = price_sum / ticks.size();
    stats.avg_spread /= ticks.size();
    stats.avg_spread_pct = (stats.avg_spread / stats.avg_price) * 100.0;
    stats.price_range_pct = ((stats.max_price - stats.min_price) / stats.avg_price) * 100.0;

    // Detect oscillations using swing detection
    double swing_threshold = stats.avg_price * (config.swing_threshold_pct / 100.0);

    enum class Direction { NONE, UP, DOWN };
    Direction current_direction = Direction::NONE;
    double swing_high = ticks[0].bid;
    double swing_low = ticks[0].bid;
    size_t swing_start_idx = 0;

    std::vector<double> downswing_speeds;
    std::vector<double> upswing_speeds;

    for (size_t i = 1; i < ticks.size(); i++) {
        double price = ticks[i].bid;

        if (current_direction == Direction::NONE) {
            if (price > swing_low + swing_threshold) {
                current_direction = Direction::UP;
                swing_high = price;
            } else if (price < swing_high - swing_threshold) {
                current_direction = Direction::DOWN;
                swing_low = price;
            }
            swing_high = std::max(swing_high, price);
            swing_low = std::min(swing_low, price);
        }
        else if (current_direction == Direction::UP) {
            if (price > swing_high) {
                swing_high = price;
            } else if (price < swing_high - swing_threshold) {
                // Swing completed - record oscillation
                double amplitude = swing_high - swing_low;
                double amplitude_pct = (amplitude / stats.avg_price) * 100.0;
                stats.amplitudes_pct.push_back(amplitude_pct);
                stats.durations.push_back(i - swing_start_idx);

                // Speed: ticks per % move
                double ticks_per_pct = (i - swing_start_idx) / amplitude_pct;
                upswing_speeds.push_back(ticks_per_pct);

                stats.oscillation_count++;
                current_direction = Direction::DOWN;
                swing_low = price;
                swing_start_idx = i;
            }
        }
        else if (current_direction == Direction::DOWN) {
            if (price < swing_low) {
                swing_low = price;
            } else if (price > swing_low + swing_threshold) {
                // Swing completed - record oscillation
                double amplitude = swing_high - swing_low;
                double amplitude_pct = (amplitude / stats.avg_price) * 100.0;
                stats.amplitudes_pct.push_back(amplitude_pct);
                stats.durations.push_back(i - swing_start_idx);

                double ticks_per_pct = (i - swing_start_idx) / amplitude_pct;
                downswing_speeds.push_back(ticks_per_pct);

                stats.oscillation_count++;
                current_direction = Direction::UP;
                swing_high = price;
                swing_start_idx = i;
            }
        }
    }

    // Calculate percentiles for amplitudes
    if (!stats.amplitudes_pct.empty()) {
        std::vector<double> sorted_amp = stats.amplitudes_pct;
        std::sort(sorted_amp.begin(), sorted_amp.end());

        size_t n = sorted_amp.size();
        stats.p25_amplitude_pct = sorted_amp[n / 4];
        stats.median_amplitude_pct = sorted_amp[n / 2];
        stats.p75_amplitude_pct = sorted_amp[3 * n / 4];
    }

    // Calculate percentiles for durations
    if (!stats.durations.empty()) {
        std::vector<size_t> sorted_dur = stats.durations;
        std::sort(sorted_dur.begin(), sorted_dur.end());
        stats.median_duration = sorted_dur[sorted_dur.size() / 2];
    }

    // Calculate speed asymmetry
    if (!upswing_speeds.empty()) {
        double sum = 0;
        for (double s : upswing_speeds) sum += s;
        stats.avg_upswing_speed = sum / upswing_speeds.size();
    }
    if (!downswing_speeds.empty()) {
        double sum = 0;
        for (double s : downswing_speeds) sum += s;
        stats.avg_downswing_speed = sum / downswing_speeds.size();
    }

    // Calculate hourly volatility
    // Extract hour from timestamp string (format: "2025.01.02 03:04:05.678")
    std::map<std::string, std::pair<double, double>> hourly_hl;  // date_hour -> (high, low)
    for (const auto& t : ticks) {
        std::string date_hour = "";
        if (t.timestamp.length() >= 13) {
            date_hour = t.timestamp.substr(0, 13);  // "2025.01.02 03"
        }
        double price = t.bid;

        if (hourly_hl.find(date_hour) == hourly_hl.end()) {
            hourly_hl[date_hour] = {price, price};
        } else {
            hourly_hl[date_hour].first = std::max(hourly_hl[date_hour].first, price);
            hourly_hl[date_hour].second = std::min(hourly_hl[date_hour].second, price);
        }
    }

    for (const auto& [date_hour, hl] : hourly_hl) {
        double range_pct = ((hl.first - hl.second) / stats.avg_price) * 100.0;
        stats.hourly_ranges_pct.push_back(range_pct);
    }

    if (!stats.hourly_ranges_pct.empty()) {
        std::vector<double> sorted_vol = stats.hourly_ranges_pct;
        std::sort(sorted_vol.begin(), sorted_vol.end());
        stats.median_hourly_vol_pct = sorted_vol[sorted_vol.size() / 2];
    }

    // Calculate capital efficiency indicators
    double trading_days = stats.total_ticks / (24.0 * 60 * 60 * 4);  // Rough estimate: ~4 ticks/second
    if (trading_days > 0) {
        stats.oscillations_per_day = stats.oscillation_count / trading_days;
    }

    // Profit potential: median amplitude - spread (both as % of price)
    stats.profit_potential_per_oscillation = stats.median_amplitude_pct - stats.avg_spread_pct;
    stats.daily_profit_potential_pct = stats.profit_potential_per_oscillation * stats.oscillations_per_day;

    return stats;
}

void PrintStats(const std::string& name, const OscillationStats& stats) {
    std::cout << "\n┌─────────────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ " << std::left << std::setw(59) << name << "│" << std::endl;
    std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "│ Ticks: " << std::setw(15) << stats.total_ticks
              << " | Oscillations: " << std::setw(15) << stats.oscillation_count << "│" << std::endl;
    std::cout << "│ Price range: $" << std::setw(10) << stats.min_price
              << " - $" << std::setw(10) << stats.max_price
              << " (" << std::setprecision(1) << stats.price_range_pct << "%)    │" << std::endl;

    std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ OSCILLATION CHARACTERISTICS                                 │" << std::endl;
    std::cout << std::setprecision(3);
    std::cout << "│ Amplitude (% of price): P25=" << std::setw(6) << stats.p25_amplitude_pct
              << "  Median=" << std::setw(6) << stats.median_amplitude_pct
              << "  P75=" << std::setw(6) << stats.p75_amplitude_pct << " │" << std::endl;
    std::cout << "│ Median duration: " << std::setw(8) << stats.median_duration << " ticks"
              << std::setw(30) << " " << "│" << std::endl;

    std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ COST STRUCTURE                                              │" << std::endl;
    std::cout << std::setprecision(4);
    std::cout << "│ Avg spread: " << std::setw(10) << stats.avg_spread
              << " (" << std::setprecision(4) << stats.avg_spread_pct << "% of price)"
              << std::setw(14) << " " << "│" << std::endl;
    std::cout << "│ Max spread: " << std::setw(10) << stats.max_spread
              << std::setw(32) << " " << "│" << std::endl;

    std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ VOLATILITY                                                  │" << std::endl;
    std::cout << std::setprecision(3);
    std::cout << "│ Median hourly range: " << std::setw(6) << stats.median_hourly_vol_pct
              << "% of price" << std::setw(22) << " " << "│" << std::endl;

    std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ ASYMMETRY (ticks per % move)                                │" << std::endl;
    std::cout << std::setprecision(0);
    std::cout << "│ Upswing: " << std::setw(8) << stats.avg_upswing_speed
              << " | Downswing: " << std::setw(8) << stats.avg_downswing_speed;
    double ratio = (stats.avg_downswing_speed > 0) ? stats.avg_upswing_speed / stats.avg_downswing_speed : 0;
    std::cout << " | Ratio: " << std::setprecision(2) << ratio << "x │" << std::endl;

    std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ CAPITAL EFFICIENCY INDICATORS                               │" << std::endl;
    std::cout << std::setprecision(1);
    std::cout << "│ Oscillations/day: " << std::setw(8) << stats.oscillations_per_day
              << std::setw(34) << " " << "│" << std::endl;
    std::cout << std::setprecision(4);
    std::cout << "│ Profit potential/oscillation: " << std::setw(8) << stats.profit_potential_per_oscillation
              << "% (amp - spread)   │" << std::endl;
    std::cout << std::setprecision(2);
    std::cout << "│ Daily profit potential: " << std::setw(8) << stats.daily_profit_potential_pct
              << "% (before leverage)       │" << std::endl;
    std::cout << "└─────────────────────────────────────────────────────────────┘" << std::endl;
}

int main() {
    std::cout << "=== Instrument Investigation: Capital-Efficient Structural Patterns ===" << std::endl;
    std::cout << "Applying Discomfort Premium Framework to identify trading opportunities" << std::endl;

    std::vector<InstrumentConfig> instruments = {
        {
            "XAUUSD (Gold) 2025",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
            100.0,    // contract size
            0.01,     // pip size
            0.05      // swing threshold % (0.05% = ~$1.75 at $3500)
        },
        {
            "XAGUSD (Silver) 2025",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv",
            5000.0,
            0.001,
            0.10      // Silver more volatile, use higher threshold
        },
        {
            "NAS100 (Nasdaq) 2025",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\NAS100\\NAS100_TICKS_2025.csv",
            1.0,
            0.01,
            0.05
        },
        {
            "USDJPY 2025",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\USDJPY\\USDJPY_TICKS_2025.csv",
            100000.0,
            0.001,
            0.03      // Forex typically tighter oscillations
        }
    };

    std::vector<std::pair<std::string, OscillationStats>> results;

    for (const auto& config : instruments) {
        OscillationStats stats = AnalyzeInstrument(config);
        results.push_back({config.name, stats});
    }

    std::cout << "\n\n========== SUMMARY COMPARISON ==========" << std::endl;

    for (const auto& [name, stats] : results) {
        PrintStats(name, stats);
    }

    // Print ranking by capital efficiency
    std::cout << "\n\n========== CAPITAL EFFICIENCY RANKING ==========" << std::endl;
    std::cout << "(Ranked by daily profit potential %)\n" << std::endl;

    std::vector<std::pair<std::string, double>> ranking;
    for (const auto& [name, stats] : results) {
        ranking.push_back({name, stats.daily_profit_potential_pct});
    }
    std::sort(ranking.begin(), ranking.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    int rank = 1;
    for (const auto& [name, potential] : ranking) {
        std::cout << rank++ << ". " << std::setw(25) << std::left << name
                  << " Daily potential: " << std::fixed << std::setprecision(2)
                  << potential << "%" << std::endl;
    }

    std::cout << "\n\n========== DISCOMFORT PREMIUM ANALYSIS ==========" << std::endl;

    for (const auto& [name, stats] : results) {
        std::cout << "\n" << name << ":" << std::endl;

        // Drawdown premium potential
        double dd_potential = stats.price_range_pct;  // How much DD you'd need to endure
        std::cout << "  Drawdown discomfort: " << std::fixed << std::setprecision(1)
                  << dd_potential << "% max range" << std::endl;

        // Patience premium (inverse of oscillation frequency)
        std::cout << "  Patience required: " << stats.median_duration << " ticks per oscillation" << std::endl;

        // Spread as % of profit
        double spread_cost_ratio = (stats.avg_spread_pct / stats.median_amplitude_pct) * 100;
        std::cout << "  Spread cost ratio: " << std::setprecision(1) << spread_cost_ratio
                  << "% of amplitude" << std::endl;

        // Viability assessment
        bool viable = stats.profit_potential_per_oscillation > 0 &&
                      stats.oscillations_per_day > 10 &&
                      spread_cost_ratio < 50;
        std::cout << "  Grid strategy viable: " << (viable ? "YES" : "NO") << std::endl;
    }

    return 0;
}
