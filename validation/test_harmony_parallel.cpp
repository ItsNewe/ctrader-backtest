/**
 * Harmony Strategy Parallel Test
 *
 * Loads tick data ONCE into shared memory, then tests multiple configurations
 * in parallel to find why the combined strategy fails when individual ones work.
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

// Global shared tick data
std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data into memory..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
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
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Loaded " << g_shared_ticks.size() << " ticks in "
              << duration.count() << " seconds" << std::endl;
}

/**
 * HarmonyStrategy - Combines "up while up" + "up while down"
 *
 * Uses SAME lot sizing formula as original strategies.
 */
class HarmonyStrategy {
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
        double start_price = 0.0;
        double highest_price = 0.0;
        double lowest_price = DBL_MAX;
        double max_total_volume = 0.0;
        double max_equity = 0.0;
        double min_equity = DBL_MAX;
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
    HarmonyStrategy(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!initialized_) {
            stats_.start_price = tick.ask;
            stats_.highest_price = tick.ask;
            stats_.lowest_price = tick.ask;
            CalculateDownSpacing(tick, engine);
            initialized_ = true;
        }

        stats_.highest_price = std::max(stats_.highest_price, tick.ask);
        stats_.lowest_price = std::min(stats_.lowest_price, tick.ask);

        UpdateVolume(engine);
        stats_.max_total_volume = std::max(stats_.max_total_volume, total_volume_);

        double equity = engine.GetEquity();
        stats_.max_equity = std::max(stats_.max_equity, equity);
        stats_.min_equity = std::min(stats_.min_equity, equity);

        // "Up while up": price above last entry + spacing
        double up_spacing = tick.ask * (config_.up_spacing_pct / 100.0);
        if (last_up_entry_ == 0.0 || tick.ask >= last_up_entry_ + up_spacing) {
            ExecuteUpWhileUp(tick, engine);
        }

        // "Up while down": price dropped below lowest entry - spacing
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

    /**
     * ORIGINAL formula from up_while_up:
     * trade_size = (equity - survive_distance * previous_volume * contract_size)
     *            / (margin_per_lot + survive_distance * contract_size)
     */
    double CalculateUpLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);

        // Potential loss from existing positions
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

struct TestConfig {
    double survive_pct;
    double up_spacing_pct;
    std::string label;
};

struct TestResult {
    std::string label;
    double survive_pct;
    double up_spacing_pct;
    double final_balance;
    double return_mult;
    double max_dd;
    int up_entries;
    int down_entries;
    double up_volume;
    double down_volume;
    double max_volume;
    double high_price;
    double low_price;
    double max_equity;
    double min_equity;
};

std::mutex g_results_mutex;
std::atomic<int> g_completed(0);

void RunSingleTest(const TestConfig& cfg, const TickBacktestConfig& base_config,
                   std::vector<TestResult>& results, int total_configs) {
    TickBacktestConfig config = base_config;
    TickBasedEngine engine(config);

    HarmonyStrategy::Config strat_cfg;
    strat_cfg.survive_pct = cfg.survive_pct;
    strat_cfg.up_spacing_pct = cfg.up_spacing_pct;

    HarmonyStrategy strategy(strat_cfg);

    engine.RunWithTicks(g_shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto eng_results = engine.GetResults();
    auto stats = strategy.GetStats();

    TestResult result;
    result.label = cfg.label;
    result.survive_pct = cfg.survive_pct;
    result.up_spacing_pct = cfg.up_spacing_pct;
    result.final_balance = eng_results.final_balance;
    result.return_mult = eng_results.final_balance / base_config.initial_balance;
    result.max_dd = eng_results.max_drawdown_pct;
    result.up_entries = stats.up_entries;
    result.down_entries = stats.down_entries;
    result.up_volume = stats.up_volume;
    result.down_volume = stats.down_volume;
    result.max_volume = stats.max_total_volume;
    result.high_price = stats.highest_price;
    result.low_price = stats.lowest_price;
    result.max_equity = stats.max_equity;
    result.min_equity = stats.min_equity;

    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        results.push_back(result);
        int done = ++g_completed;
        if (done % 5 == 0 || done == total_configs) {
            std::cout << "\r  Progress: " << done << "/" << total_configs << std::flush;
        }
    }
}

int main() {
    std::cout << "=== Harmony Strategy Parallel Test ===" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Find actual price range
    double min_price = DBL_MAX, max_price = 0;
    for (const auto& t : g_shared_ticks) {
        min_price = std::min(min_price, t.bid);
        max_price = std::max(max_price, t.ask);
    }
    double max_drop_pct = (max_price - min_price) / max_price * 100.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Price range: $" << min_price << " - $" << max_price << std::endl;
    std::cout << "Max drop from high: " << max_drop_pct << "%" << std::endl;
    std::cout << std::endl;

    // Base config
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

    // Create test configurations
    std::vector<TestConfig> configs;
    std::vector<double> survive_values = {12.0, 13.0, 14.0, 15.0, 16.0, 18.0, 20.0};
    std::vector<double> spacing_values = {0.25, 0.5, 1.0, 2.0, 3.0};

    for (double surv : survive_values) {
        for (double sp : spacing_values) {
            TestConfig cfg;
            cfg.survive_pct = surv;
            cfg.up_spacing_pct = sp;
            std::ostringstream oss;
            oss << "s" << (int)surv << "_sp" << std::fixed << std::setprecision(2) << sp;
            cfg.label = oss.str();
            configs.push_back(cfg);
        }
    }

    std::cout << "Running " << configs.size() << " configurations..." << std::endl;

    // Run tests in parallel
    std::vector<TestResult> results;
    std::vector<std::thread> threads;

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::atomic<size_t> config_idx(0);

    auto worker = [&]() {
        while (true) {
            size_t idx = config_idx.fetch_add(1);
            if (idx >= configs.size()) break;
            RunSingleTest(configs[idx], base_config, results, (int)configs.size());
        }
    };

    auto start = std::chrono::high_resolution_clock::now();

    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds" << std::endl;
    std::cout << std::endl;

    // Sort by return
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        return a.return_mult > b.return_mult;
    });

    // Print results
    std::cout << "=== Results (sorted by return) ===" << std::endl;
    std::cout << std::left << std::setw(16) << "Config"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(8) << "UpEnt"
              << std::setw(8) << "DnEnt"
              << std::setw(10) << "UpVol"
              << std::setw(10) << "MaxVol"
              << std::setw(10) << "HighPx"
              << std::setw(10) << "MinEq"
              << std::endl;
    std::cout << std::string(92, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(16) << r.label
                  << std::setw(10) << (std::to_string(r.return_mult).substr(0, 5) + "x")
                  << std::setw(10) << (std::to_string(r.max_dd).substr(0, 5) + "%")
                  << std::setw(8) << r.up_entries
                  << std::setw(8) << r.down_entries
                  << std::setw(10) << r.up_volume
                  << std::setw(10) << r.max_volume
                  << std::setw(10) << (int)r.high_price
                  << std::setw(10) << (int)r.min_equity
                  << std::endl;
    }

    // Analysis
    std::cout << std::endl;
    std::cout << "=== Analysis ===" << std::endl;
    std::cout << "Max drop from high: " << max_drop_pct << "%" << std::endl;

    int survived = 0, failed = 0;
    for (const auto& r : results) {
        if (r.return_mult > 0.5 && r.max_dd < 99.0) survived++;
        else failed++;
    }
    std::cout << "Survived (>0.5x, <99% DD): " << survived << ", Failed: " << failed << std::endl;

    // Show configs where survive_pct > max_drop
    std::cout << std::endl;
    std::cout << "Configs where survive% > " << max_drop_pct << "% (should all survive):" << std::endl;
    for (const auto& r : results) {
        if (r.survive_pct > max_drop_pct) {
            std::cout << "  " << r.label << ": " << r.return_mult << "x, DD=" << r.max_dd << "%, HighPx=$" << (int)r.high_price << std::endl;
        }
    }

    return 0;
}
