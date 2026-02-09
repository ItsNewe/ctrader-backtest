#ifndef SYNTHETIC_TICK_GENERATOR_H
#define SYNTHETIC_TICK_GENERATOR_H

#include "tick_data.h"
#include <vector>
#include <random>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace backtest {

/**
 * Generates synthetic tick data for strategy testing
 * Allows creation of specific market scenarios for edge case testing
 */
class SyntheticTickGenerator {
public:
    SyntheticTickGenerator(double start_price = 2600.0, double spread = 0.25, unsigned int seed = 42)
        : current_price_(start_price),
          spread_(spread),
          rng_(seed),
          tick_count_(0) {
    }

    // Reset generator to initial state
    void Reset(double start_price, unsigned int seed = 42) {
        current_price_ = start_price;
        tick_count_ = 0;
        rng_.seed(seed);
        ticks_.clear();
    }

    // Generate random walk ticks
    void GenerateRandomWalk(int num_ticks, double volatility = 0.1, double drift = 0.0) {
        std::normal_distribution<double> dist(drift, volatility);

        for (int i = 0; i < num_ticks; i++) {
            double change = dist(rng_);
            current_price_ += change;
            current_price_ = std::max(current_price_, 100.0); // Floor price
            AddTick();
        }
    }

    // Generate trending market (up or down)
    void GenerateTrend(int num_ticks, double total_move, double noise = 0.05) {
        double move_per_tick = total_move / num_ticks;
        std::normal_distribution<double> noise_dist(0.0, noise);

        for (int i = 0; i < num_ticks; i++) {
            current_price_ += move_per_tick + noise_dist(rng_);
            current_price_ = std::max(current_price_, 100.0);
            AddTick();
        }
    }

    // Generate sharp crash (rapid price drop)
    void GenerateCrash(int num_ticks, double drop_percent) {
        double target_price = current_price_ * (1.0 - drop_percent / 100.0);
        double drop_per_tick = (current_price_ - target_price) / num_ticks;

        // Crash accelerates - more drop at the end
        for (int i = 0; i < num_ticks; i++) {
            double acceleration = 1.0 + (double)i / num_ticks; // 1.0 to 2.0
            current_price_ -= drop_per_tick * acceleration * 0.67; // Normalized
            current_price_ = std::max(current_price_, 100.0);
            AddTick();
        }
    }

    // Generate V-shaped recovery
    void GenerateVRecovery(int down_ticks, int up_ticks, double drop_percent) {
        double start_price = current_price_;
        double bottom_price = start_price * (1.0 - drop_percent / 100.0);

        // Down leg
        GenerateTrend(down_ticks, bottom_price - start_price, 0.02);

        // Up leg - recover to original
        GenerateTrend(up_ticks, start_price - current_price_, 0.02);
    }

    // Generate sideways/ranging market
    void GenerateSideways(int num_ticks, double range_dollars, double center_price = 0) {
        if (center_price == 0) center_price = current_price_;
        std::uniform_real_distribution<double> dist(-range_dollars/2, range_dollars/2);

        for (int i = 0; i < num_ticks; i++) {
            current_price_ = center_price + dist(rng_);
            current_price_ = std::max(current_price_, 100.0);
            AddTick();
        }
    }

    // Generate gap (sudden price jump, simulating weekend gap)
    void GenerateGap(double gap_dollars) {
        current_price_ += gap_dollars;
        current_price_ = std::max(current_price_, 100.0);
        AddTick();
    }

    // Generate whipsaw (rapid up-down-up movements)
    void GenerateWhipsaw(int cycles, double amplitude, int ticks_per_cycle) {
        for (int c = 0; c < cycles; c++) {
            // Up
            GenerateTrend(ticks_per_cycle / 2, amplitude, 0.01);
            // Down
            GenerateTrend(ticks_per_cycle / 2, -amplitude, 0.01);
        }
    }

    // Generate flash crash and recovery
    void GenerateFlashCrash(double drop_percent, int crash_ticks, int recovery_ticks) {
        double start = current_price_;

        // Rapid crash
        GenerateCrash(crash_ticks, drop_percent);

        // Partial recovery (recover 80% of the drop)
        double recovery_target = start - (start - current_price_) * 0.2;
        GenerateTrend(recovery_ticks, recovery_target - current_price_, 0.03);
    }

    // Generate prolonged bear market
    void GenerateBearMarket(int num_ticks, double total_drop_percent, int bounce_count = 3) {
        double target = current_price_ * (1.0 - total_drop_percent / 100.0);
        double total_drop = current_price_ - target;
        int ticks_per_segment = num_ticks / (bounce_count * 2);

        for (int i = 0; i < bounce_count; i++) {
            // Drop phase
            double drop_this_segment = total_drop / bounce_count;
            GenerateTrend(ticks_per_segment, -drop_this_segment * 1.2, 0.02);

            // Dead cat bounce (recover 20% of segment drop)
            GenerateTrend(ticks_per_segment, drop_this_segment * 0.2, 0.02);
        }
    }

    // Get generated ticks
    const std::vector<Tick>& GetTicks() const { return ticks_; }

    // Save to CSV file
    void SaveToCSV(const std::string& filename) const {
        std::ofstream file(filename);
        file << "timestamp,bid,ask,volume,flags\n";

        for (const auto& tick : ticks_) {
            file << tick.timestamp << ","
                 << std::fixed << std::setprecision(5)
                 << tick.bid << "," << tick.ask << ","
                 << "0,0\n";
        }
    }

    // Get current price
    double GetCurrentPrice() const { return current_price_; }

    // Get tick count
    size_t GetTickCount() const { return ticks_.size(); }

private:
    double current_price_;
    double spread_;
    std::mt19937 rng_;
    size_t tick_count_;
    std::vector<Tick> ticks_;

    void AddTick() {
        Tick tick;
        tick.bid = current_price_;
        tick.ask = current_price_ + spread_;
        tick.timestamp = GenerateTimestamp();
        ticks_.push_back(tick);
    }

    std::string GenerateTimestamp() {
        // Generate timestamp: 2025.01.01 00:00:00.000 + tick_count_ seconds
        int total_seconds = tick_count_++;
        int days = total_seconds / 86400;
        int remaining = total_seconds % 86400;
        int hours = remaining / 3600;
        remaining %= 3600;
        int minutes = remaining / 60;
        int seconds = remaining % 60;

        std::ostringstream oss;
        oss << "2025.01." << std::setfill('0') << std::setw(2) << (1 + days)
            << " " << std::setw(2) << hours
            << ":" << std::setw(2) << minutes
            << ":" << std::setw(2) << seconds << ".000";
        return oss.str();
    }
};

} // namespace backtest

#endif // SYNTHETIC_TICK_GENERATOR_H
