#ifndef TICK_DATA_MANAGER_H
#define TICK_DATA_MANAGER_H

#include "tick_data.h"
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>

namespace backtest {

/**
 * Tick data validation configuration and results.
 * Controls what validation checks are performed and reports any issues found.
 */
struct TickValidationConfig {
    // Validation options
    bool validate_price_range = true;       ///< Check for out-of-range prices
    bool validate_spread = true;            ///< Check for abnormal spreads
    bool validate_timestamp_order = true;   ///< Check timestamps are ascending
    bool validate_gaps = true;              ///< Check for suspicious time gaps
    bool remove_invalid_ticks = true;       ///< Remove invalid ticks vs throw error
    bool log_warnings = true;               ///< Log validation warnings

    // Price validation parameters
    double min_price = 0.0001;              ///< Minimum valid price (default for forex)
    double max_price = 1000000.0;           ///< Maximum valid price
    double max_price_change_pct = 10.0;     ///< Max % change between consecutive ticks
    double max_spread_pct = 1.0;            ///< Max spread as % of price
    int max_gap_seconds = 86400;            ///< Max allowed gap (default: 1 day)

    // Statistics (populated after validation)
    size_t total_ticks_read = 0;
    size_t invalid_price_count = 0;
    size_t invalid_spread_count = 0;
    size_t out_of_order_count = 0;
    size_t large_gap_count = 0;
    size_t removed_count = 0;
    size_t parse_error_count = 0;         ///< Lines that failed to parse (format errors)

    bool HasIssues() const {
        return invalid_price_count > 0 || invalid_spread_count > 0 ||
               out_of_order_count > 0 || large_gap_count > 0 ||
               parse_error_count > 0;
    }

    std::string GetSummary() const {
        std::ostringstream ss;
        ss << "Tick Validation Summary:\n";
        ss << "  Total ticks read: " << total_ticks_read << "\n";
        ss << "  Parse errors: " << parse_error_count << "\n";
        ss << "  Invalid prices: " << invalid_price_count << "\n";
        ss << "  Invalid spreads: " << invalid_spread_count << "\n";
        ss << "  Out of order: " << out_of_order_count << "\n";
        ss << "  Large gaps: " << large_gap_count << "\n";
        ss << "  Removed: " << removed_count << "\n";
        return ss.str();
    }

    // Preset configurations
    static TickValidationConfig ForForex() {
        TickValidationConfig c;
        c.min_price = 0.0001;
        c.max_price = 500.0;  // Most forex pairs < 500
        c.max_spread_pct = 0.5;
        return c;
    }

    static TickValidationConfig ForGold() {
        TickValidationConfig c;
        c.min_price = 100.0;
        c.max_price = 10000.0;
        c.max_spread_pct = 0.1;  // Gold has tight spreads
        c.max_price_change_pct = 5.0;
        return c;
    }

    static TickValidationConfig ForSilver() {
        TickValidationConfig c;
        c.min_price = 1.0;
        c.max_price = 500.0;
        c.max_spread_pct = 0.2;
        c.max_price_change_pct = 7.0;
        return c;
    }

    static TickValidationConfig ForCrypto() {
        TickValidationConfig c;
        c.min_price = 0.000001;
        c.max_price = 1000000.0;
        c.max_spread_pct = 2.0;  // Crypto has wider spreads
        c.max_price_change_pct = 20.0;
        return c;
    }

    static TickValidationConfig Disabled() {
        TickValidationConfig c;
        c.validate_price_range = false;
        c.validate_spread = false;
        c.validate_timestamp_order = false;
        c.validate_gaps = false;
        c.remove_invalid_ticks = false;
        c.log_warnings = false;
        return c;
    }
};

/**
 * Tick data validator - performs validation checks on tick data.
 */
class TickValidator {
public:
    explicit TickValidator(TickValidationConfig* config)
        : config_(config), last_tick_valid_(false), last_timestamp_seconds_(0) {}

    /**
     * Validate a single tick.
     * @param tick The tick to validate
     * @param prev_tick Optional previous tick for sequence validation
     * @return true if tick is valid (or validation disabled)
     */
    bool ValidateTick(const Tick& tick, const Tick* prev_tick = nullptr) {
        config_->total_ticks_read++;
        bool valid = true;

        // Price range validation
        if (config_->validate_price_range) {
            if (tick.bid <= config_->min_price || tick.bid >= config_->max_price ||
                tick.ask <= config_->min_price || tick.ask >= config_->max_price ||
                !std::isfinite(tick.bid) || !std::isfinite(tick.ask)) {
                config_->invalid_price_count++;
                valid = false;
            }

            // Check for excessive price jump
            if (prev_tick && prev_tick->bid > 0 && tick.bid > 0) {
                double pct_change = std::abs(tick.bid - prev_tick->bid) / prev_tick->bid * 100.0;
                if (pct_change > config_->max_price_change_pct) {
                    config_->invalid_price_count++;
                    valid = false;
                }
            }
        }

        // Spread validation
        if (config_->validate_spread && valid) {
            if (tick.ask <= tick.bid) {
                // Ask must be >= bid
                config_->invalid_spread_count++;
                valid = false;
            } else {
                double spread_pct = (tick.ask - tick.bid) / tick.bid * 100.0;
                if (spread_pct > config_->max_spread_pct) {
                    config_->invalid_spread_count++;
                    valid = false;
                }
            }
        }

        // Timestamp order validation
        if (config_->validate_timestamp_order && valid) {
            long current_seconds = ParseTimestampToSeconds(tick.timestamp);
            if (last_tick_valid_ && current_seconds < last_timestamp_seconds_) {
                config_->out_of_order_count++;
                valid = false;
            }

            // Gap detection
            if (config_->validate_gaps && last_tick_valid_) {
                long gap = current_seconds - last_timestamp_seconds_;
                if (gap > config_->max_gap_seconds) {
                    config_->large_gap_count++;
                    // Don't invalidate, just count - gaps are often legitimate
                }
            }

            if (valid) {
                last_timestamp_seconds_ = current_seconds;
                last_tick_valid_ = true;
            }
        }

        if (!valid) {
            config_->removed_count++;
        }

        return valid || !config_->remove_invalid_ticks;
    }

    /**
     * Validate a batch of ticks in-place, removing invalid ones.
     * @param ticks Vector of ticks to validate
     */
    void ValidateBatch(std::vector<Tick>& ticks) {
        if (ticks.empty()) return;

        // Reset state for batch validation
        last_tick_valid_ = false;
        last_timestamp_seconds_ = 0;

        const Tick* prev_tick = nullptr;
        size_t write_idx = 0;

        for (size_t read_idx = 0; read_idx < ticks.size(); ++read_idx) {
            if (ValidateTick(ticks[read_idx], prev_tick)) {
                if (write_idx != read_idx) {
                    ticks[write_idx] = ticks[read_idx];
                }
                prev_tick = &ticks[write_idx];
                write_idx++;
            }
        }

        // Resize to remove invalid ticks
        if (config_->remove_invalid_ticks && write_idx < ticks.size()) {
            ticks.resize(write_idx);
        }
    }

private:
    TickValidationConfig* config_;
    bool last_tick_valid_;
    long last_timestamp_seconds_;

    // Parse timestamp to seconds (same as FillUpOscillation)
    static long ParseTimestampToSeconds(const std::string& ts) {
        if (ts.size() < 19) return 0;
        int year = (ts[0] - '0') * 1000 + (ts[1] - '0') * 100 + (ts[2] - '0') * 10 + (ts[3] - '0');
        int month = (ts[5] - '0') * 10 + (ts[6] - '0');
        int day = (ts[8] - '0') * 10 + (ts[9] - '0');
        int hour = (ts[11] - '0') * 10 + (ts[12] - '0');
        int minute = (ts[14] - '0') * 10 + (ts[15] - '0');
        int second = (ts[17] - '0') * 10 + (ts[18] - '0');
        static const int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    }
};

/**
 * Manages loading and streaming of tick data
 * Supports both full memory load and streaming mode for large datasets
 */
class TickDataManager {
public:
    explicit TickDataManager(const TickDataConfig& config,
                            TickValidationConfig validation_config = TickValidationConfig::Disabled())
        : config_(config), validation_config_(validation_config),
          validator_(&validation_config_), current_index_(0), total_ticks_loaded_(0) {
        Initialize();
    }

    ~TickDataManager() {
        if (file_stream_.is_open()) {
            file_stream_.close();
        }
    }

    // Get next tick (streaming mode)
    bool GetNextTick(Tick& tick) {
        if (config_.load_all_into_memory) {
            if (current_index_ >= ticks_.size()) {
                return false;
            }
            tick = ticks_[current_index_++];
            return true;
        } else {
            return ReadTickFromStream(tick);
        }
    }

    // Reset to beginning
    void Reset() {
        current_index_ = 0;
        if (file_stream_.is_open()) {
            file_stream_.clear();
            file_stream_.seekg(0);
            SkipHeader();
        }
    }

    // Get total tick count (only available in memory mode or after full scan)
    size_t GetTotalTickCount() const {
        return config_.load_all_into_memory ? ticks_.size() : total_ticks_loaded_;
    }

    // Load all ticks into memory
    void LoadAllTicks() {
        if (config_.load_all_into_memory) {
            return; // Already loaded
        }

        Reset();
        ticks_.clear();
        Tick tick;
        while (ReadTickFromStream(tick)) {
            ticks_.push_back(tick);
        }
        config_.load_all_into_memory = true;
        total_ticks_loaded_ = ticks_.size();
        current_index_ = 0;
    }

    // Get tick at specific index (requires memory mode)
    // Access all loaded ticks (for sharing across parallel engines)
    const std::vector<Tick>& GetAllTicks() const {
        return ticks_;
    }

    const Tick& GetTickAt(size_t index) const {
        if (!config_.load_all_into_memory) {
            throw std::runtime_error("GetTickAt() requires load_all_into_memory mode");
        }
        if (index >= ticks_.size()) {
            throw std::out_of_range("Tick index out of range");
        }
        return ticks_[index];
    }

    // Get time range of tick data
    struct TimeRange {
        std::string start_time;
        std::string end_time;
        size_t tick_count;
    };

    TimeRange GetTimeRange() const {
        TimeRange range;
        if (config_.load_all_into_memory && !ticks_.empty()) {
            range.start_time = ticks_.front().timestamp;
            range.end_time = ticks_.back().timestamp;
            range.tick_count = ticks_.size();
        } else {
            range.tick_count = total_ticks_loaded_;
        }
        return range;
    }

    // Get validation statistics
    const TickValidationConfig& GetValidationStats() const {
        return validation_config_;
    }

    // Enable/disable validation
    void SetValidationConfig(const TickValidationConfig& config) {
        validation_config_ = config;
        validator_ = TickValidator(&validation_config_);
    }

private:
    TickDataConfig config_;
    TickValidationConfig validation_config_;
    TickValidator validator_;
    std::vector<Tick> ticks_;           // In-memory storage
    std::ifstream file_stream_;         // Streaming mode
    size_t current_index_;
    size_t total_ticks_loaded_;
    Tick last_valid_tick_;              // For streaming validation

    void Initialize() {
        // Allow empty path when ticks will be fed externally via RunWithTicks
        if (config_.file_path.empty()) {
            return;  // Skip initialization - external ticks will be used
        }

        if (config_.load_all_into_memory) {
            LoadFromFile();
        } else {
            OpenStreamingFile();
        }
    }

    void LoadFromFile() {
        std::ifstream file(config_.file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open tick data file: " + config_.file_path);
        }

        ticks_.clear();
        std::string line;

        // Skip header
        std::getline(file, line);

        // Read all ticks
        while (std::getline(file, line)) {
            Tick tick;
            if (ParseTickLine(line, tick)) {
                ticks_.push_back(tick);
            } else if (!line.empty()) {
                validation_config_.parse_error_count++;
            }
        }

        file.close();

        // Validate batch after loading
        if (validation_config_.validate_price_range ||
            validation_config_.validate_spread ||
            validation_config_.validate_timestamp_order) {
            validator_.ValidateBatch(ticks_);
        }

        total_ticks_loaded_ = ticks_.size();
    }

    void OpenStreamingFile() {
        file_stream_.open(config_.file_path);
        if (!file_stream_.is_open()) {
            throw std::runtime_error("Failed to open tick data file for streaming: " + config_.file_path);
        }
        SkipHeader();
    }

    void SkipHeader() {
        std::string header;
        std::getline(file_stream_, header);
    }

    bool ReadTickFromStream(Tick& tick) {
        std::string line;
        while (std::getline(file_stream_, line)) {
            if (ParseTickLine(line, tick)) {
                // Validate in streaming mode
                const Tick* prev = (last_valid_tick_.bid > 0) ? &last_valid_tick_ : nullptr;
                if (validator_.ValidateTick(tick, prev)) {
                    last_valid_tick_ = tick;
                    total_ticks_loaded_++;
                    return true;
                }
                // If validation is set to remove invalid, continue to next line
                if (validation_config_.remove_invalid_ticks) {
                    continue;
                }
                // Otherwise return the invalid tick anyway
                total_ticks_loaded_++;
                return true;
            } else if (!line.empty()) {
                // Track parse errors in streaming mode
                validation_config_.parse_error_count++;
            }
        }
        return false;
    }

    bool ParseTickLine(const std::string& line, Tick& tick) {
        if (line.empty()) {
            return false;
        }

        std::stringstream ss(line);
        std::string timestamp_str, bid_str, ask_str, volume_str, flags_str;

        // MT5 CSV format: TAB-delimited
        // Format: Timestamp\tBid\tAsk[\tVolume\tFlags]
        // Volume and Flags are optional (some data exports don't include them)
        std::getline(ss, timestamp_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');
        std::getline(ss, volume_str, '\t');  // Optional
        std::getline(ss, flags_str, '\t');   // Optional

        try {
            tick.timestamp = timestamp_str;
            tick.bid = std::stod(bid_str);
            tick.ask = std::stod(ask_str);
            // Volume is optional - default to 0 if not provided
            if (!volume_str.empty()) {
                tick.volume = std::stol(volume_str);
            } else {
                tick.volume = 0;
            }
            return true;
        } catch (const std::exception& e) {
            // Skip invalid lines
            return false;
        }
    }
};

/**
 * Tick iterator for range-based loops
 */
class TickIterator {
public:
    TickIterator(TickDataManager& manager, bool is_end = false)
        : manager_(manager), is_end_(is_end) {
        if (!is_end_) {
            has_tick_ = manager_.GetNextTick(current_tick_);
        }
    }

    Tick& operator*() { return current_tick_; }
    const Tick& operator*() const { return current_tick_; }
    Tick* operator->() { return &current_tick_; }

    TickIterator& operator++() {
        has_tick_ = manager_.GetNextTick(current_tick_);
        return *this;
    }

    bool operator!=(const TickIterator& other) const {
        if (other.is_end_) {
            return has_tick_;
        }
        return false;
    }

private:
    TickDataManager& manager_;
    Tick current_tick_;
    bool has_tick_ = false;
    bool is_end_ = false;
};

} // namespace backtest

#endif // TICK_DATA_MANAGER_H
