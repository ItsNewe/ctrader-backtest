#ifndef TICK_DATA_H
#define TICK_DATA_H

#include <string>
#include <chrono>

namespace backtest {

/**
 * Represents a single tick (price update) with bid/ask spread
 */
struct Tick {
    std::string timestamp;       // ISO format: "2024.01.02 19:00:00.123"
    double bid;                  // Bid price
    double ask;                  // Ask price
    long volume;                 // Tick volume (if available)

    // Helper to get mid price
    double mid() const {
        return (bid + ask) / 2.0;
    }

    // Helper to get spread in price points
    double spread() const {
        return ask - bid;
    }

    // Helper to get spread in pips (assumes 5-digit broker)
    double spread_pips() const {
        return (ask - bid) * 100000.0;
    }

    // Convert timestamp string to time_point for comparisons
    // Format expected: "2024.01.02 19:00:00.123"
    std::chrono::system_clock::time_point to_time_point() const;
};

/**
 * Tick data format specification
 */
enum class TickDataFormat {
    MT5_CSV,        // MT5 CSV export format (TAB-delimited)
    BINARY,         // Custom binary format (for compression)
    FXT            // Dukascopy FXT format
};

/**
 * Configuration for tick data loading
 */
struct TickDataConfig {
    TickDataFormat format = TickDataFormat::MT5_CSV;
    std::string file_path;
    bool load_all_into_memory = false;  // false = streaming mode
    size_t cache_size_mb = 100;         // Cache size for streaming mode

    TickDataConfig() = default;
    TickDataConfig(const std::string& path) : file_path(path) {}
};

} // namespace backtest

#endif // TICK_DATA_H
