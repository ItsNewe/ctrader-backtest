/**
 * Dual TP Grid Strategy - Optimized Parallel Parameter Sweep
 * Removed expensive per-tick operations for speed
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>

using namespace backtest;

// ============================================================================
// Optimized DualTPGrid Strategy
// ============================================================================

struct DualTPConfig {
    double survive_pct = 12.0;  // Changed to 12%
    double base_spacing = 1.5;
    double min_volume = 0.01;
    double max_volume = 0.5;
    double contract_size = 100.0;
    double leverage = 500.0;
    double tight_tp_mult = 1.0;
    double wide_tp_mult = 5.0;
};

class DualTPGridFast {
public:
    DualTPGridFast(const DualTPConfig& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double price = tick.ask;

        const auto& open_trades = engine.GetOpenPositions();

        // Track position bounds - O(n) but unavoidable
        double lowest_entry = 1e9, highest_entry = 0;
        int buy_count = 0;

        // Simple zone tracking with unordered_map (O(1) average)
        tight_zones_.clear();
        wide_zones_.clear();

        for (const auto* trade : open_trades) {
            if (trade->direction != TradeDirection::BUY) continue;

            lowest_entry = std::min(lowest_entry, trade->entry_price);
            highest_entry = std::max(highest_entry, trade->entry_price);
            buy_count++;

            int zone = static_cast<int>(trade->entry_price / config_.base_spacing);
            double tp_dist = trade->take_profit - trade->entry_price;

            if (tp_dist < config_.base_spacing * config_.tight_tp_mult * 1.5) {
                tight_zones_[zone] = true;
            } else {
                wide_zones_[zone] = true;
            }
        }

        double lot_size = CalculateLotSize(price, engine, buy_count, highest_entry);
        int current_zone = static_cast<int>(price / config_.base_spacing);

        if (buy_count == 0) {
            // First entry - open both
            OpenDual(price, lot_size, engine);
        } else if (price <= lowest_entry - config_.base_spacing) {
            // New low - open if zone empty
            if (!tight_zones_[current_zone] && !wide_zones_[current_zone]) {
                OpenDual(price, lot_size, engine);
            }
        } else if (price >= highest_entry + config_.base_spacing) {
            // New high - open if zone empty
            if (!tight_zones_[current_zone] && !wide_zones_[current_zone]) {
                OpenDual(price, lot_size, engine);
            }
        } else if (!tight_zones_[current_zone] && wide_zones_[current_zone]) {
            // Re-entry: tight closed, wide still open
            double tight_tp = price + config_.base_spacing * config_.tight_tp_mult;
            engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, tight_tp);
            tight_reentries_++;
        }
    }

    int GetTightReentries() const { return tight_reentries_; }

private:
    DualTPConfig config_;
    std::unordered_map<int, bool> tight_zones_;
    std::unordered_map<int, bool> wide_zones_;
    int tight_reentries_ = 0;

    void OpenDual(double price, double lot_size, TickBasedEngine& engine) {
        double tight_tp = price + config_.base_spacing * config_.tight_tp_mult;
        engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, tight_tp);

        double wide_tp = price + config_.base_spacing * config_.wide_tp_mult;
        engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, wide_tp);
    }

    double CalculateLotSize(double price, TickBasedEngine& engine, int pos_count, double highest_entry) {
        double equity = engine.GetEquity();
        double end_price = (pos_count == 0)
            ? price * ((100.0 - config_.survive_pct) / 100.0)
            : highest_entry * ((100.0 - config_.survive_pct) / 100.0);

        double distance = std::max(price - end_price, config_.base_spacing);
        double num_trades = std::max(1.0, std::floor(distance / config_.base_spacing)) * 2;

        double current_volume = 0;
        for (const auto* trade : engine.GetOpenPositions()) {
            current_volume += trade->lot_size;
        }

        double equity_at_target = equity - current_volume * distance * config_.contract_size;
        double used_margin = current_volume * config_.contract_size * price / config_.leverage;

        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < 30.0) {
            return config_.min_volume;
        }

        double d_equity = config_.contract_size * config_.min_volume * config_.base_spacing *
                         (num_trades * (num_trades + 1) / 2);
        double d_margin = num_trades * config_.min_volume * config_.contract_size / config_.leverage;

        double max_mult = config_.max_volume / config_.min_volume;
        double low = 1.0, high = max_mult, best_mult = 1.0;

        for (int i = 0; i < 10; i++) {  // Fixed iterations instead of while loop
            double mid_val = (low + high) / 2.0;
            double test_equity = equity_at_target - mid_val * d_equity;
            double test_margin = used_margin + mid_val * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > 30.0) {
                best_mult = mid_val;
                low = mid_val;
            } else {
                high = mid_val;
            }
        }

        return std::min(best_mult * config_.min_volume, config_.max_volume);
    }
};

// ============================================================================
// Tick Loading
// ============================================================================

std::vector<Tick> LoadTicks(const std::string& file_path, const std::string& start_date, const std::string& end_date) {
    std::vector<Tick> ticks;
    ticks.reserve(60000000);  // Pre-allocate for speed

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << file_path << std::endl;
        return ticks;
    }

    std::string line;
    line.reserve(256);
    std::getline(file, line);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        size_t pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);

        std::string date_str = tick.timestamp.substr(0, 10);
        if (date_str < start_date || date_str > end_date) continue;

        size_t pos2 = line.find('\t', pos + 1);
        if (pos2 == std::string::npos) continue;
        tick.bid = std::stod(line.substr(pos + 1, pos2 - pos - 1));

        size_t pos3 = line.find('\t', pos2 + 1);
        if (pos3 == std::string::npos) pos3 = line.length();
        tick.ask = std::stod(line.substr(pos2 + 1, pos3 - pos2 - 1));

        ticks.push_back(tick);
    }

    return ticks;
}

// ============================================================================
// Sweep
// ============================================================================

struct SweepResult {
    double tight_tp_mult;
    double wide_tp_mult;
    double base_spacing;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    int total_trades;
};

SweepResult RunTest(const std::vector<Tick>& ticks, const DualTPConfig& cfg,
                    const std::string& start_date, const std::string& end_date) {

    TickBacktestConfig engine_config;
    engine_config.symbol = "XAUUSD";
    engine_config.initial_balance = 10000.0;
    engine_config.contract_size = cfg.contract_size;
    engine_config.leverage = cfg.leverage;
    engine_config.pip_size = 0.01;
    engine_config.swap_long = -66.99;
    engine_config.swap_short = 41.2;
    engine_config.swap_mode = 1;
    engine_config.swap_3days = 3;
    engine_config.start_date = start_date;
    engine_config.end_date = end_date;
    engine_config.verbose = false;

    TickBasedEngine engine(engine_config);
    DualTPGridFast strategy(cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto res = engine.GetResults();

    SweepResult sr;
    sr.tight_tp_mult = cfg.tight_tp_mult;
    sr.wide_tp_mult = cfg.wide_tp_mult;
    sr.base_spacing = cfg.base_spacing;
    sr.final_balance = res.final_balance;
    sr.return_mult = res.final_balance / 10000.0;
    sr.max_dd_pct = res.max_drawdown_pct;
    sr.total_trades = res.total_trades;

    return sr;
}

int main() {
    std::cout << "===========================================================" << std::endl;
    std::cout << "   Dual TP Grid - Optimized Parallel Sweep" << std::endl;
    std::cout << "   survive_pct = 12%, Date: 2025.01.01 - 2026.01.29" << std::endl;
    std::cout << "===========================================================" << std::endl;

    std::string tick_file = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string start_date = "2025.01.01";
    std::string end_date = "2026.01.29";

    std::cout << "Loading ticks..." << std::endl;
    auto load_start = std::chrono::high_resolution_clock::now();
    std::vector<Tick> ticks = LoadTicks(tick_file, start_date, end_date);
    auto load_end = std::chrono::high_resolution_clock::now();
    std::cout << "Loaded " << ticks.size() << " ticks in "
              << std::chrono::duration_cast<std::chrono::seconds>(load_end - load_start).count() << "s" << std::endl;

    if (ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return 1;
    }

    // Parameter ranges
    std::vector<double> tight_tp_mults = {0.5, 1.0, 1.5, 2.0};
    std::vector<double> wide_tp_mults = {5.0, 7.0, 10.0, 15.0, 20.0};
    std::vector<double> base_spacings = {1.0, 1.5, 2.0};

    std::vector<DualTPConfig> configs;
    for (double tight : tight_tp_mults) {
        for (double wide : wide_tp_mults) {
            if (tight >= wide) continue;
            for (double spacing : base_spacings) {
                DualTPConfig config;
                config.survive_pct = 12.0;  // 12%
                config.base_spacing = spacing;
                config.tight_tp_mult = tight;
                config.wide_tp_mult = wide;
                config.max_volume = 0.5;
                configs.push_back(config);
            }
        }
    }

    std::cout << "Testing " << configs.size() << " configurations" << std::endl;

    std::vector<SweepResult> results(configs.size());
    std::atomic<int> completed{0};
    std::mutex output_mutex;

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " threads\n" << std::endl;

    auto sweep_start = std::chrono::high_resolution_clock::now();

    auto worker = [&](size_t w_start, size_t w_end) {
        for (size_t i = w_start; i < w_end; ++i) {
            results[i] = RunTest(ticks, configs[i], start_date, end_date);
            int done = ++completed;

            std::lock_guard<std::mutex> lock(output_mutex);
            std::cout << "  [" << done << "/" << configs.size() << "] "
                      << "t=" << std::fixed << std::setprecision(1) << configs[i].tight_tp_mult
                      << " w=" << configs[i].wide_tp_mult
                      << " s=" << configs[i].base_spacing
                      << " -> " << std::setprecision(2) << results[i].return_mult << "x"
                      << " DD=" << results[i].max_dd_pct << "%"
                      << std::endl;
        }
    };

    std::vector<std::thread> threads;
    size_t chunk_size = (configs.size() + num_threads - 1) / num_threads;

    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t start_idx = t * chunk_size;
        size_t end_idx = std::min(start_idx + chunk_size, configs.size());
        if (start_idx < configs.size()) {
            threads.emplace_back(worker, start_idx, end_idx);
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    auto sweep_end = std::chrono::high_resolution_clock::now();
    auto sweep_time = std::chrono::duration_cast<std::chrono::seconds>(sweep_end - sweep_start);

    std::sort(results.begin(), results.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.return_mult > b.return_mult;
    });

    std::cout << "\n===========================================================" << std::endl;
    std::cout << "   COMPLETE - " << sweep_time.count() << "s" << std::endl;
    std::cout << "===========================================================" << std::endl;

    std::cout << "\nTOP 10 BY RETURN:\n" << std::endl;
    std::cout << std::setw(6) << "Tight" << std::setw(6) << "Wide" << std::setw(6) << "Sp"
              << std::setw(10) << "Return" << std::setw(8) << "DD%" << std::endl;
    std::cout << std::string(36, '-') << std::endl;

    for (int i = 0; i < std::min(10, (int)results.size()); i++) {
        const auto& r = results[i];
        std::cout << std::fixed << std::setprecision(1)
                  << std::setw(6) << r.tight_tp_mult
                  << std::setw(6) << r.wide_tp_mult
                  << std::setw(6) << r.base_spacing
                  << std::setprecision(2) << std::setw(9) << r.return_mult << "x"
                  << std::setw(8) << r.max_dd_pct
                  << std::endl;
    }

    std::cout << "\nTOP 10 BY RISK-ADJUSTED:\n" << std::endl;
    std::sort(results.begin(), results.end(), [](const SweepResult& a, const SweepResult& b) {
        if (a.final_balance < 100 && b.final_balance >= 100) return false;
        if (b.final_balance < 100 && a.final_balance >= 100) return true;
        double ra = a.max_dd_pct > 0 ? a.return_mult / a.max_dd_pct : 0;
        double rb = b.max_dd_pct > 0 ? b.return_mult / b.max_dd_pct : 0;
        return ra > rb;
    });

    std::cout << std::setw(6) << "Tight" << std::setw(6) << "Wide" << std::setw(6) << "Sp"
              << std::setw(10) << "Return" << std::setw(8) << "DD%" << std::setw(8) << "Ratio" << std::endl;
    std::cout << std::string(44, '-') << std::endl;

    int shown = 0;
    for (const auto& r : results) {
        if (shown >= 10 || r.final_balance < 100) continue;
        double ratio = r.max_dd_pct > 0 ? r.return_mult / r.max_dd_pct : 0;
        std::cout << std::fixed << std::setprecision(1)
                  << std::setw(6) << r.tight_tp_mult
                  << std::setw(6) << r.wide_tp_mult
                  << std::setw(6) << r.base_spacing
                  << std::setprecision(2) << std::setw(9) << r.return_mult << "x"
                  << std::setw(8) << r.max_dd_pct
                  << std::setw(8) << ratio
                  << std::endl;
        shown++;
    }

    return 0;
}
