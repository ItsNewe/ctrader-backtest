#ifndef TICK_DATA_MANAGER_H
#define TICK_DATA_MANAGER_H

#include "tick_data.h"
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <algorithm>

namespace backtest {

/**
 * Manages loading and streaming of tick data
 * Supports both full memory load and streaming mode for large datasets
 */
class TickDataManager {
public:
    explicit TickDataManager(const TickDataConfig& config)
        : config_(config), current_index_(0), total_ticks_loaded_(0) {
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

private:
    TickDataConfig config_;
    std::vector<Tick> ticks_;           // In-memory storage
    std::ifstream file_stream_;         // Streaming mode
    size_t current_index_;
    size_t total_ticks_loaded_;

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
            }
        }

        total_ticks_loaded_ = ticks_.size();
        file.close();
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
        if (std::getline(file_stream_, line)) {
            if (ParseTickLine(line, tick)) {
                total_ticks_loaded_++;
                return true;
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
