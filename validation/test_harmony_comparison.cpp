/**
 * Compare: Up While Up vs Up While Down vs Combined (Harmony)
 *
 * Test each strategy independently at the same survive% levels
 * to understand where the failure comes from.
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cfloat>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

using namespace backtest;

std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header
    g_shared_ticks.reserve(52000000);

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;
        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');
        tick.timestamp = datetime_str;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;
        g_shared_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Loaded " << g_shared_ticks.size() << " ticks in "
              << std::chrono::duration_cast<std::chrono::seconds>(end - start).count() << "s" << std::endl;
}

// ============================================================================
// Strategy 1: Up While Up ONLY
// ============================================================================
class UpWhileUpOnly {
public:
    struct Config {
        double survive_pct = 13.0;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double spacing_pct = 0.5;
    };

    struct Stats {
        int entries = 0;
        double volume = 0.0;
        double max_volume = 0.0;
    };

private:
    Config config_;
    Stats stats_;
    double last_entry_ = 0.0;
    double total_volume_ = 0.0;
    bool initialized_ = false;

public:
    UpWhileUpOnly(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!initialized_) {
            initialized_ = true;
        }

        UpdateVolume(engine);
        stats_.max_volume = std::max(stats_.max_volume, total_volume_);

        double spacing = tick.ask * (config_.spacing_pct / 100.0);
        if (last_entry_ == 0.0 || tick.ask >= last_entry_ + spacing) {
            ExecuteEntry(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }

private:
    void UpdateVolume(TickBasedEngine& engine) {
        total_volume_ = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                total_volume_ += trade->lot_size;
            }
        }
    }

    double CalculateLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);
        double potential_loss = total_volume_ * survive_dist * config_.contract_size;
        double available = equity - potential_loss;

        if (available <= 0) return 0.0;

        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double loss_per_lot = survive_dist * config_.contract_size;

        double lots = available / (margin_per_lot + loss_per_lot);
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;
        return std::max(lots, 0.0);
    }

    void ExecuteEntry(const Tick& tick, TickBasedEngine& engine) {
        double lots = CalculateLotSize(tick, engine);
        if (lots < config_.min_volume) return;

        Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
        if (trade) {
            last_entry_ = tick.ask;
            stats_.entries++;
            stats_.volume += lots;
        }
    }
};

// ============================================================================
// Strategy 2: Up While Down ONLY
// ============================================================================
class UpWhileDownOnly {
public:
    struct Config {
        double survive_pct = 13.0;
        double min_volume = 0.01;
        double contract_size = 100.0;
        double leverage = 500.0;
        double lot_size = 0.01;
    };

    struct Stats {
        int entries = 0;
        double volume = 0.0;
        double max_volume = 0.0;
    };

private:
    Config config_;
    Stats stats_;
    double lowest_entry_ = DBL_MAX;
    double spacing_ = 0.0;
    double total_volume_ = 0.0;
    bool initialized_ = false;

public:
    UpWhileDownOnly(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!initialized_) {
            CalculateSpacing(tick, engine);
            initialized_ = true;
        }

        UpdateVolume(engine);
        stats_.max_volume = std::max(stats_.max_volume, total_volume_);

        if (lowest_entry_ >= DBL_MAX || tick.ask <= lowest_entry_ - spacing_) {
            ExecuteEntry(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }

private:
    void CalculateSpacing(const Tick& tick, TickBasedEngine& engine) {
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);
        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double loss_per_lot = survive_dist * config_.contract_size;
        double cost_per_trade = (margin_per_lot + loss_per_lot) * config_.lot_size;

        double equity = engine.GetEquity();
        double budget = equity * 0.50;

        int num_trades = (int)(budget / cost_per_trade);
        num_trades = std::max(num_trades, 1);

        spacing_ = survive_dist / num_trades;
        spacing_ = std::max(spacing_, 1.0);
    }

    void UpdateVolume(TickBasedEngine& engine) {
        total_volume_ = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                total_volume_ += trade->lot_size;
            }
        }
    }

    void ExecuteEntry(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);
        double potential_loss = total_volume_ * survive_dist * config_.contract_size;
        double new_loss = config_.lot_size * survive_dist * config_.contract_size;
        double margin_needed = (tick.ask * config_.contract_size * config_.lot_size) / config_.leverage;

        if (potential_loss + new_loss + margin_needed >= equity * 0.95) {
            return;
        }

        Trade* trade = engine.OpenMarketOrder("BUY", config_.lot_size, 0.0, 0.0);
        if (trade) {
            lowest_entry_ = tick.ask;
            stats_.entries++;
            stats_.volume += config_.lot_size;
        }
    }
};

// ============================================================================
// Strategy 3: Combined (Harmony)
// ============================================================================
class HarmonyCombined {
public:
    struct Config {
        double survive_pct = 13.0;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double down_lot_size = 0.01;
        double up_spacing_pct = 0.5;
    };

    struct Stats {
        int up_entries = 0;
        int down_entries = 0;
        double up_volume = 0.0;
        double down_volume = 0.0;
        double max_volume = 0.0;
    };

private:
    Config config_;
    Stats stats_;
    double last_up_entry_ = 0.0;
    double lowest_entry_ = DBL_MAX;
    double down_spacing_ = 0.0;
    double total_volume_ = 0.0;
    bool initialized_ = false;

public:
    HarmonyCombined(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!initialized_) {
            CalculateDownSpacing(tick, engine);
            initialized_ = true;
        }

        UpdateVolume(engine);
        stats_.max_volume = std::max(stats_.max_volume, total_volume_);

        // Up while up
        double up_spacing = tick.ask * (config_.up_spacing_pct / 100.0);
        if (last_up_entry_ == 0.0 || tick.ask >= last_up_entry_ + up_spacing) {
            ExecuteUpWhileUp(tick, engine);
        }

        // Up while down
        if (lowest_entry_ >= DBL_MAX || tick.ask <= lowest_entry_ - down_spacing_) {
            ExecuteUpWhileDown(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }

private:
    void CalculateDownSpacing(const Tick& tick, TickBasedEngine& engine) {
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);
        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double loss_per_lot = survive_dist * config_.contract_size;
        double cost_per_trade = (margin_per_lot + loss_per_lot) * config_.down_lot_size;

        double equity = engine.GetEquity();
        double budget = equity * 0.50;

        int num_trades = (int)(budget / cost_per_trade);
        num_trades = std::max(num_trades, 1);

        down_spacing_ = survive_dist / num_trades;
        down_spacing_ = std::max(down_spacing_, 1.0);
    }

    void UpdateVolume(TickBasedEngine& engine) {
        total_volume_ = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                total_volume_ += trade->lot_size;
            }
        }
    }

    double CalculateUpLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);
        double potential_loss = total_volume_ * survive_dist * config_.contract_size;
        double available = equity - potential_loss;

        if (available <= 0) return 0.0;

        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double loss_per_lot = survive_dist * config_.contract_size;

        double lots = available / (margin_per_lot + loss_per_lot);
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;
        return std::max(lots, 0.0);
    }

    void ExecuteUpWhileUp(const Tick& tick, TickBasedEngine& engine) {
        double lots = CalculateUpLotSize(tick, engine);
        if (lots < config_.min_volume) return;

        Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
        if (trade) {
            last_up_entry_ = tick.ask;
            stats_.up_entries++;
            stats_.up_volume += lots;
        }
    }

    void ExecuteUpWhileDown(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);
        double potential_loss = total_volume_ * survive_dist * config_.contract_size;
        double new_loss = config_.down_lot_size * survive_dist * config_.contract_size;
        double margin_needed = (tick.ask * config_.contract_size * config_.down_lot_size) / config_.leverage;

        if (potential_loss + new_loss + margin_needed >= equity * 0.95) {
            return;
        }

        Trade* trade = engine.OpenMarketOrder("BUY", config_.down_lot_size, 0.0, 0.0);
        if (trade) {
            lowest_entry_ = tick.ask;
            stats_.down_entries++;
            stats_.down_volume += config_.down_lot_size;
        }
    }
};

// ============================================================================
// Test structures
// ============================================================================
enum StrategyType { UP_WHILE_UP, UP_WHILE_DOWN, COMBINED };

struct TestConfig {
    StrategyType type;
    double survive_pct;
    std::string label;
};

struct TestResult {
    std::string label;
    StrategyType type;
    double survive_pct;
    double final_balance;
    double return_mult;
    double max_dd;
    int entries;
    double volume;
    double max_volume;
    double swap_cost;
};

std::mutex g_mutex;
std::atomic<int> g_done(0);

void RunTest(const TestConfig& cfg, const TickBacktestConfig& base_config,
             std::vector<TestResult>& results, int total) {
    TickBacktestConfig config = base_config;
    TickBasedEngine engine(config);

    TestResult result;
    result.label = cfg.label;
    result.type = cfg.type;
    result.survive_pct = cfg.survive_pct;

    if (cfg.type == UP_WHILE_UP) {
        UpWhileUpOnly::Config strat_cfg;
        strat_cfg.survive_pct = cfg.survive_pct;
        strat_cfg.spacing_pct = 0.5;
        UpWhileUpOnly strategy(strat_cfg);

        engine.RunWithTicks(g_shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto stats = strategy.GetStats();
        result.entries = stats.entries;
        result.volume = stats.volume;
        result.max_volume = stats.max_volume;
    }
    else if (cfg.type == UP_WHILE_DOWN) {
        UpWhileDownOnly::Config strat_cfg;
        strat_cfg.survive_pct = cfg.survive_pct;
        UpWhileDownOnly strategy(strat_cfg);

        engine.RunWithTicks(g_shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto stats = strategy.GetStats();
        result.entries = stats.entries;
        result.volume = stats.volume;
        result.max_volume = stats.max_volume;
    }
    else {
        HarmonyCombined::Config strat_cfg;
        strat_cfg.survive_pct = cfg.survive_pct;
        strat_cfg.up_spacing_pct = 0.5;
        HarmonyCombined strategy(strat_cfg);

        engine.RunWithTicks(g_shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto stats = strategy.GetStats();
        result.entries = stats.up_entries + stats.down_entries;
        result.volume = stats.up_volume + stats.down_volume;
        result.max_volume = stats.max_volume;
    }

    auto eng_results = engine.GetResults();
    result.final_balance = eng_results.final_balance;
    result.return_mult = eng_results.final_balance / base_config.initial_balance;
    result.max_dd = eng_results.max_drawdown_pct;
    result.swap_cost = eng_results.total_swap_charged;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        results.push_back(result);
        int done = ++g_done;
        if (done % 5 == 0 || done == total) {
            std::cout << "\r  Progress: " << done << "/" << total << std::flush;
        }
    }
}

int main() {
    std::cout << "=== Strategy Comparison: Up While Up vs Up While Down vs Combined ===" << std::endl;
    std::cout << std::endl;

    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    double initial_balance = 10000.0;
    TickBacktestConfig base_config;
    base_config.symbol = "XAUUSD";
    base_config.initial_balance = initial_balance;
    base_config.account_currency = "USD";
    base_config.contract_size = 100.0;
    base_config.leverage = 500.0;
    base_config.margin_rate = 1.0;
    base_config.pip_size = 0.01;
    base_config.swap_long = -66.99;
    base_config.swap_short = 41.2;
    base_config.swap_mode = 1;
    base_config.swap_3days = 3;
    base_config.verbose = false;

    // Create test configs
    std::vector<TestConfig> configs;
    std::vector<double> survive_values = {12.0, 13.0, 14.0, 15.0, 16.0, 18.0, 20.0};

    for (double surv : survive_values) {
        configs.push_back({UP_WHILE_UP, surv, "UpUp_s" + std::to_string((int)surv)});
        configs.push_back({UP_WHILE_DOWN, surv, "UpDn_s" + std::to_string((int)surv)});
        configs.push_back({COMBINED, surv, "Comb_s" + std::to_string((int)surv)});
    }

    std::cout << "Running " << configs.size() << " tests..." << std::endl;

    std::vector<TestResult> results;
    std::vector<std::thread> threads;
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::atomic<size_t> idx(0);

    auto worker = [&]() {
        while (true) {
            size_t i = idx.fetch_add(1);
            if (i >= configs.size()) break;
            RunTest(configs[i], base_config, results, (int)configs.size());
        }
    };

    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    std::cout << std::endl;
    std::cout << "Completed in "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::high_resolution_clock::now() - start).count()
              << " seconds" << std::endl << std::endl;

    // Sort by survive%, then by type
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.survive_pct != b.survive_pct) return a.survive_pct < b.survive_pct;
        return a.type < b.type;
    });

    // Print results
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=== Results by Survive% ===" << std::endl;
    std::cout << std::left
              << std::setw(14) << "Strategy"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Entries"
              << std::setw(10) << "Volume"
              << std::setw(12) << "MaxVol"
              << std::setw(12) << "SwapCost"
              << std::endl;
    std::cout << std::string(78, '-') << std::endl;

    double current_surv = 0;
    for (const auto& r : results) {
        if (r.survive_pct != current_surv) {
            if (current_surv != 0) std::cout << std::endl;
            std::cout << "--- Survive " << (int)r.survive_pct << "% ---" << std::endl;
            current_surv = r.survive_pct;
        }

        std::string type_str;
        if (r.type == UP_WHILE_UP) type_str = "UpWhileUp";
        else if (r.type == UP_WHILE_DOWN) type_str = "UpWhileDown";
        else type_str = "Combined";

        std::cout << std::left
                  << std::setw(14) << type_str
                  << std::setw(10) << (std::to_string(r.return_mult).substr(0, 6) + "x")
                  << std::setw(10) << (std::to_string(r.max_dd).substr(0, 5) + "%")
                  << std::setw(10) << r.entries
                  << std::setw(10) << r.volume
                  << std::setw(12) << r.max_volume
                  << std::setw(12) << ("$" + std::to_string((int)r.swap_cost))
                  << std::endl;
    }

    // Summary
    std::cout << std::endl;
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Swap cost for XAUUSD BUY: $66.99/lot/day" << std::endl;
    std::cout << "Without exits, positions are held ~365 days." << std::endl;
    std::cout << "Expected swap at 1 lot avg: ~$24,451/year (more than $10k starting equity)" << std::endl;

    return 0;
}
