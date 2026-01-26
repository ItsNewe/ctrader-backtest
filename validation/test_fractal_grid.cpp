/**
 * Fractal Self-Similarity Multi-Scale Grid Test
 *
 * CONCEPT: Market patterns look similar at different timeframes (self-similarity).
 * Run multiple FillUpOscillation instances with different spacings simultaneously:
 *   - MICRO: tiny spacing (0.02-0.04%) captures minute-level oscillations
 *   - MESO: medium spacing (0.08-0.20%) captures hour-level oscillations
 *   - MACRO: large spacing (0.30-1.00%) captures day-level oscillations
 *
 * Each scale operates independently but shares the same engine/account.
 * Position sizes scale with grid level (smaller spacing = smaller lots).
 * Total survive_pct is shared across all scales.
 *
 * Uses PARALLEL pattern: Load tick data ONCE, test all configs simultaneously.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <memory>

using namespace backtest;

// ============================================================================
// Shared tick data - loaded ONCE, used by ALL threads
// ============================================================================
std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data into memory (one-time)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    g_shared_ticks.reserve(52000000);  // Pre-allocate for ~52M ticks

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
    std::cout << "Memory usage: ~" << (g_shared_ticks.size() * sizeof(Tick) / 1024 / 1024)
              << " MB" << std::endl << std::endl;
}

// ============================================================================
// Task and Result structures
// ============================================================================
struct ScaleConfig {
    double spacing_pct;    // Spacing as % of price
    double lot_fraction;   // Fraction of total lot allocation for this scale
};

struct FractalTask {
    int num_scales;
    std::vector<ScaleConfig> scales;
    double survive_pct;    // Shared across all scales
    std::string label;
};

struct FractalResult {
    std::string label;
    int num_scales;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;
    bool stop_out;
    std::vector<int> trades_per_scale;  // Trades opened by each scale
};

// ============================================================================
// Multi-Scale Fractal Strategy
// Runs multiple FillUpOscillation instances with different spacings
// ============================================================================
class FractalGridStrategy {
public:
    FractalGridStrategy(const FractalTask& task, double contract_size, double leverage)
        : task_(task),
          contract_size_(contract_size),
          leverage_(leverage),
          peak_equity_(0.0),
          max_dd_pct_(0.0) {

        // Create a strategy instance for each scale
        // Each scale gets its own FillUpOscillation with percentage-based spacing
        for (size_t i = 0; i < task_.scales.size(); i++) {
            const auto& scale = task_.scales[i];

            // Setup adaptive config for percentage-based spacing
            FillUpOscillation::AdaptiveConfig adaptive_cfg;
            adaptive_cfg.pct_spacing = true;
            adaptive_cfg.typical_vol_pct = 0.55;  // 4h median range for gold
            adaptive_cfg.min_spacing_mult = 0.5;
            adaptive_cfg.max_spacing_mult = 3.0;
            adaptive_cfg.min_spacing_abs = 0.005;   // 0.005% min
            adaptive_cfg.max_spacing_abs = 5.0;     // 5% max
            adaptive_cfg.spacing_change_threshold = 0.01;

            // Calculate lot limits based on fraction
            // Base: min=0.01, max=10.0 for single scale
            // For multi-scale, each scale gets proportional lot allocation
            double scale_min_lot = 0.01;  // Always allow tiny lots
            double scale_max_lot = 10.0 * scale.lot_fraction;
            if (scale_max_lot < 0.01) scale_max_lot = 0.01;

            // Create strategy with percentage-based spacing
            // base_spacing is in % when pct_spacing=true
            auto strategy = std::make_unique<FillUpOscillation>(
                task_.survive_pct,           // Shared survive_pct
                scale.spacing_pct,           // Spacing as % of price
                scale_min_lot,               // min_volume
                scale_max_lot,               // max_volume (scaled)
                contract_size_,
                leverage_,
                FillUpOscillation::ADAPTIVE_SPACING,
                0.1,                         // antifragile_scale (unused)
                30.0,                        // velocity_threshold (unused)
                4.0,                         // volatility_lookback_hours
                adaptive_cfg
            );

            strategies_.push_back(std::move(strategy));
            trades_opened_by_scale_.push_back(0);
        }
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // Track drawdown
        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd_pct = (peak_equity_ > 0) ? (peak_equity_ - equity) / peak_equity_ * 100.0 : 0.0;
        if (dd_pct > max_dd_pct_) max_dd_pct_ = dd_pct;

        // Call each scale's strategy
        // They share the same engine, so positions are cumulative
        for (size_t i = 0; i < strategies_.size(); i++) {
            size_t pos_before = engine.GetOpenPositions().size();
            strategies_[i]->OnTick(tick, engine);
            size_t pos_after = engine.GetOpenPositions().size();

            // Track trades opened by this scale
            if (pos_after > pos_before) {
                trades_opened_by_scale_[i] += (int)(pos_after - pos_before);
            }
        }
    }

    double GetMaxDDPct() const { return max_dd_pct_; }
    const std::vector<int>& GetTradesPerScale() const { return trades_opened_by_scale_; }

private:
    FractalTask task_;
    double contract_size_;
    double leverage_;
    double peak_equity_;
    double max_dd_pct_;
    std::vector<std::unique_ptr<FillUpOscillation>> strategies_;
    std::vector<int> trades_opened_by_scale_;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<FractalTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const FractalTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(FractalTask& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !tasks_.empty() || done_; });
        if (tasks_.empty()) return false;
        task = tasks_.front();
        tasks_.pop();
        return true;
    }

    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        cv_.notify_all();
    }
};

// ============================================================================
// Global state
// ============================================================================
std::atomic<int> g_completed{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<FractalResult> g_results;

// ============================================================================
// Run single test with shared tick data
// ============================================================================
FractalResult run_test(const FractalTask& task, const std::vector<Tick>& ticks) {
    FractalResult r;
    r.label = task.label;
    r.num_scales = task.num_scales;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.stop_out = false;

    TickBacktestConfig cfg;
    cfg.symbol = "XAUUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 100.0;
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.01;
    cfg.swap_long = -66.99;
    cfg.swap_short = 41.2;
    cfg.swap_mode = 1;
    cfg.swap_3days = 3;
    cfg.start_date = "2025.01.01";
    cfg.end_date = "2025.12.30";
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);
        FractalGridStrategy strategy(task, cfg.contract_size, cfg.leverage);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = strategy.GetMaxDDPct();
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.stop_out = res.stop_out_occurred;
        r.trades_per_scale = strategy.GetTradesPerScale();
    } catch (const std::exception& e) {
        r.stop_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    FractalTask task;
    while (queue.pop(task)) {
        FractalResult r = run_test(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        int done = ++g_completed;
        if (done % 5 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "\rProgress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * done / total) << "%)" << std::flush;
        }
    }
}

// ============================================================================
// Generate all fractal configurations to test
// ============================================================================
std::vector<FractalTask> GenerateTasks() {
    std::vector<FractalTask> tasks;

    // ========================================================================
    // SINGLE SCALE BASELINES (reference)
    // ========================================================================
    std::vector<double> single_spacings = {0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.10, 0.15, 0.20, 0.30, 0.50};
    for (double sp : single_spacings) {
        FractalTask t;
        t.num_scales = 1;
        t.survive_pct = 13.0;
        t.scales = {{sp, 1.0}};  // Single scale gets 100% lot allocation
        std::ostringstream oss;
        oss << "SINGLE_" << std::fixed << std::setprecision(2) << sp << "%";
        t.label = oss.str();
        tasks.push_back(t);
    }

    // ========================================================================
    // DUAL SCALE CONFIGURATIONS
    // ========================================================================

    // Dual scale: 5:1 ratio (micro + meso)
    std::vector<std::pair<double, double>> dual_5x = {
        {0.02, 0.10},  // 0.02% + 0.10%
        {0.03, 0.15},  // 0.03% + 0.15%
        {0.04, 0.20},  // 0.04% + 0.20%
        {0.05, 0.25},  // 0.05% + 0.25%
        {0.06, 0.30},  // 0.06% + 0.30%
    };
    for (const auto& [micro, meso] : dual_5x) {
        // Try different lot allocations
        std::vector<std::pair<double, double>> allocations = {
            {0.3, 0.7},   // 30% micro, 70% meso
            {0.4, 0.6},   // 40% micro, 60% meso
            {0.5, 0.5},   // 50/50
            {0.6, 0.4},   // 60% micro, 40% meso
            {0.7, 0.3},   // 70% micro, 30% meso
        };
        for (const auto& [alloc_micro, alloc_meso] : allocations) {
            FractalTask t;
            t.num_scales = 2;
            t.survive_pct = 13.0;
            t.scales = {{micro, alloc_micro}, {meso, alloc_meso}};
            std::ostringstream oss;
            oss << "DUAL_" << std::fixed << std::setprecision(2) << micro
                << "+" << meso << "_"
                << std::setprecision(0) << (alloc_micro*100) << "/" << (alloc_meso*100);
            t.label = oss.str();
            tasks.push_back(t);
        }
    }

    // Dual scale: 10:1 ratio (micro + macro)
    std::vector<std::pair<double, double>> dual_10x = {
        {0.02, 0.20},
        {0.03, 0.30},
        {0.05, 0.50},
    };
    for (const auto& [micro, macro] : dual_10x) {
        std::vector<std::pair<double, double>> allocations = {
            {0.3, 0.7},
            {0.5, 0.5},
            {0.7, 0.3},
        };
        for (const auto& [alloc_micro, alloc_macro] : allocations) {
            FractalTask t;
            t.num_scales = 2;
            t.survive_pct = 13.0;
            t.scales = {{micro, alloc_micro}, {macro, alloc_macro}};
            std::ostringstream oss;
            oss << "DUAL10x_" << std::fixed << std::setprecision(2) << micro
                << "+" << macro << "_"
                << std::setprecision(0) << (alloc_micro*100) << "/" << (alloc_macro*100);
            t.label = oss.str();
            tasks.push_back(t);
        }
    }

    // ========================================================================
    // TRIPLE SCALE CONFIGURATIONS
    // ========================================================================

    // Triple scale: 5:1:25 ratio (micro + meso + macro)
    std::vector<std::tuple<double, double, double>> triple_configs = {
        {0.02, 0.10, 0.50},  // 0.02% / 0.10% / 0.50%
        {0.03, 0.15, 0.75},
        {0.04, 0.20, 1.00},
        {0.02, 0.08, 0.32},  // Closer ratios
        {0.03, 0.12, 0.48},
    };
    for (const auto& [micro, meso, macro] : triple_configs) {
        // Try different allocation strategies
        std::vector<std::tuple<double, double, double>> allocations = {
            {0.2, 0.3, 0.5},   // Macro-heavy
            {0.33, 0.33, 0.34}, // Equal
            {0.5, 0.3, 0.2},   // Micro-heavy
            {0.4, 0.4, 0.2},   // Balanced micro+meso
            {0.2, 0.5, 0.3},   // Meso-heavy
        };
        for (const auto& [a_micro, a_meso, a_macro] : allocations) {
            FractalTask t;
            t.num_scales = 3;
            t.survive_pct = 13.0;
            t.scales = {{micro, a_micro}, {meso, a_meso}, {macro, a_macro}};
            std::ostringstream oss;
            oss << "TRIPLE_" << std::fixed << std::setprecision(2) << micro
                << "+" << meso << "+" << macro << "_"
                << std::setprecision(0) << (a_micro*100) << "/" << (a_meso*100) << "/" << (a_macro*100);
            t.label = oss.str();
            tasks.push_back(t);
        }
    }

    // ========================================================================
    // SURVIVE PERCENT VARIATIONS
    // ========================================================================

    // Test different survive_pct with best dual/triple configs
    std::vector<double> survive_values = {10.0, 12.0, 15.0, 18.0};

    // Best dual from initial sweep (will verify)
    for (double surv : survive_values) {
        FractalTask t;
        t.num_scales = 2;
        t.survive_pct = surv;
        t.scales = {{0.03, 0.5}, {0.15, 0.5}};
        std::ostringstream oss;
        oss << "DUAL_0.03+0.15_50/50_s" << std::fixed << std::setprecision(0) << surv;
        t.label = oss.str();
        tasks.push_back(t);
    }

    // Best triple from initial sweep (will verify)
    for (double surv : survive_values) {
        FractalTask t;
        t.num_scales = 3;
        t.survive_pct = surv;
        t.scales = {{0.02, 0.33}, {0.10, 0.33}, {0.50, 0.34}};
        std::ostringstream oss;
        oss << "TRIPLE_0.02+0.10+0.50_equal_s" << std::fixed << std::setprecision(0) << surv;
        t.label = oss.str();
        tasks.push_back(t);
    }

    // ========================================================================
    // EXTREME RATIOS
    // ========================================================================

    // Very wide fractal ratios (25:1, 50:1)
    std::vector<std::pair<double, double>> extreme_dual = {
        {0.02, 0.50},  // 25:1 ratio
        {0.02, 1.00},  // 50:1 ratio
        {0.01, 0.50},  // 50:1 ratio with tiny micro
    };
    for (const auto& [micro, macro] : extreme_dual) {
        for (double alloc_micro : {0.3, 0.5, 0.7}) {
            FractalTask t;
            t.num_scales = 2;
            t.survive_pct = 13.0;
            t.scales = {{micro, alloc_micro}, {macro, 1.0 - alloc_micro}};
            std::ostringstream oss;
            oss << "EXTREME_" << std::fixed << std::setprecision(2) << micro
                << "+" << macro << "_"
                << std::setprecision(0) << (alloc_micro*100) << "/" << ((1.0-alloc_micro)*100);
            t.label = oss.str();
            tasks.push_back(t);
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  FRACTAL SELF-SIMILARITY MULTI-SCALE GRID TEST" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "CONCEPT: Run multiple grid strategies at different scales" << std::endl;
    std::cout << "  - MICRO: tiny spacing (0.02-0.04%) captures minute oscillations" << std::endl;
    std::cout << "  - MESO:  medium spacing (0.08-0.20%) captures hourly oscillations" << std::endl;
    std::cout << "  - MACRO: large spacing (0.30-1.00%) captures daily oscillations" << std::endl;
    std::cout << std::endl;
    std::cout << "Strategy: FillUpOscillation ADAPTIVE_SPACING (percentage-based)" << std::endl;
    std::cout << "Data: XAUUSD 2025 (full year)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data ONCE
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Step 2: Generate tasks
    auto tasks = GenerateTasks();
    int total = (int)tasks.size();

    // Step 3: Setup thread pool
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " worker threads" << std::endl;
    std::cout << "Testing " << total << " configurations..." << std::endl;
    std::cout << std::endl;

    // Step 4: Fill work queue
    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }

    // Step 5: Launch workers
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total, std::cref(g_shared_ticks));
    }

    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds ("
              << std::fixed << std::setprecision(1) << (double)duration.count() / total
              << "s per config, " << num_threads << " threads)" << std::endl;
    std::cout << std::endl;

    // Step 6: Sort by sharpe_proxy (best risk-adjusted return)
    std::sort(g_results.begin(), g_results.end(), [](const FractalResult& a, const FractalResult& b) {
        // Put stop-outs at bottom
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    // ========================================================================
    // RESULTS
    // ========================================================================

    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 30 CONFIGURATIONS (sorted by Sharpe proxy)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(38) << "Label"
              << std::right << std::setw(7) << "Scales"
              << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "Sharpe"
              << std::setw(6) << "SO"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)g_results.size()); i++) {
        const auto& r = g_results[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(38) << r.label
                  << std::right
                  << std::setw(7) << r.num_scales
                  << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(6) << (r.stop_out ? "YES" : "no")
                  << std::endl;
    }

    // ========================================================================
    // ANALYSIS BY SCALE COUNT
    // ========================================================================

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BEST BY SCALE COUNT" << std::endl;
    std::cout << "================================================================" << std::endl;

    for (int scale_count = 1; scale_count <= 3; scale_count++) {
        std::cout << std::endl << "=== " << scale_count << " SCALE";
        if (scale_count > 1) std::cout << "S";
        std::cout << " (TOP 5) ===" << std::endl;

        std::cout << std::left << std::setw(4) << "#"
                  << std::setw(38) << "Label"
                  << std::right << std::setw(9) << "Return"
                  << std::setw(8) << "MaxDD%"
                  << std::setw(8) << "Trades"
                  << std::setw(8) << "Sharpe"
                  << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        int count = 0;
        for (const auto& r : g_results) {
            if (r.num_scales == scale_count && !r.stop_out) {
                count++;
                std::cout << std::left << std::setw(4) << count
                          << std::setw(38) << r.label
                          << std::right << std::fixed
                          << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                          << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                          << std::setw(8) << r.total_trades
                          << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                          << std::endl;
                if (count >= 5) break;
            }
        }
    }

    // ========================================================================
    // COMPARISON: SINGLE vs DUAL vs TRIPLE
    // ========================================================================

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SINGLE vs DUAL vs TRIPLE COMPARISON" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find best of each type
    FractalResult best_single, best_dual, best_triple;
    bool found_single = false, found_dual = false, found_triple = false;

    for (const auto& r : g_results) {
        if (r.stop_out) continue;
        if (r.num_scales == 1 && !found_single) { best_single = r; found_single = true; }
        if (r.num_scales == 2 && !found_dual) { best_dual = r; found_dual = true; }
        if (r.num_scales == 3 && !found_triple) { best_triple = r; found_triple = true; }
    }

    std::cout << std::endl;
    std::cout << std::left << std::setw(15) << "Type"
              << std::setw(38) << "Config"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    if (found_single) {
        std::cout << std::left << std::setw(15) << "SINGLE"
                  << std::setw(38) << best_single.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << best_single.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << best_single.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << best_single.sharpe_proxy
                  << std::endl;
    }
    if (found_dual) {
        std::cout << std::left << std::setw(15) << "DUAL"
                  << std::setw(38) << best_dual.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << best_dual.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << best_dual.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << best_dual.sharpe_proxy
                  << std::endl;
    }
    if (found_triple) {
        std::cout << std::left << std::setw(15) << "TRIPLE"
                  << std::setw(38) << best_triple.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << best_triple.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << best_triple.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << best_triple.sharpe_proxy
                  << std::endl;
    }

    // ========================================================================
    // ANALYSIS: Does multi-scale help?
    // ========================================================================

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ANALYSIS: Does Multi-Scale Improve Results?" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Calculate averages by scale count
    double avg_return[4] = {0}, avg_dd[4] = {0}, avg_sharpe[4] = {0};
    int count_by_scale[4] = {0};

    for (const auto& r : g_results) {
        if (r.stop_out || r.num_scales > 3) continue;
        avg_return[r.num_scales] += r.return_mult;
        avg_dd[r.num_scales] += r.max_dd_pct;
        avg_sharpe[r.num_scales] += r.sharpe_proxy;
        count_by_scale[r.num_scales]++;
    }

    std::cout << std::endl;
    std::cout << std::left << std::setw(12) << "Scales"
              << std::right << std::setw(10) << "Avg Ret"
              << std::setw(10) << "Avg DD%"
              << std::setw(12) << "Avg Sharpe"
              << std::setw(12) << "Configs"
              << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    for (int s = 1; s <= 3; s++) {
        if (count_by_scale[s] > 0) {
            avg_return[s] /= count_by_scale[s];
            avg_dd[s] /= count_by_scale[s];
            avg_sharpe[s] /= count_by_scale[s];

            std::cout << std::left << std::setw(12) << s
                      << std::right << std::fixed
                      << std::setw(8) << std::setprecision(2) << avg_return[s] << "x"
                      << std::setw(9) << std::setprecision(1) << avg_dd[s] << "%"
                      << std::setw(12) << std::setprecision(2) << avg_sharpe[s]
                      << std::setw(12) << count_by_scale[s]
                      << std::endl;
        }
    }

    // ========================================================================
    // STOP-OUTS
    // ========================================================================

    int stop_out_count = 0;
    for (const auto& r : g_results) {
        if (r.stop_out) stop_out_count++;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  STOP-OUT ANALYSIS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Total configurations: " << g_results.size() << std::endl;
    std::cout << "Stop-outs: " << stop_out_count << " ("
              << std::fixed << std::setprecision(1)
              << (100.0 * stop_out_count / g_results.size()) << "%)" << std::endl;

    if (stop_out_count > 0) {
        std::cout << std::endl << "Stop-out configs (first 10):" << std::endl;
        int shown = 0;
        for (const auto& r : g_results) {
            if (r.stop_out) {
                std::cout << "  - " << r.label << std::endl;
                shown++;
                if (shown >= 10) break;
            }
        }
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;
    std::cout << std::endl;

    // Final verdict
    std::cout << "KEY QUESTIONS:" << std::endl;
    std::cout << "  1. Does multi-scale capture more oscillations? " << std::endl;
    if (found_single && found_dual && found_triple) {
        std::cout << "     -> Single best: " << best_single.total_trades << " trades" << std::endl;
        std::cout << "     -> Dual best:   " << best_dual.total_trades << " trades" << std::endl;
        std::cout << "     -> Triple best: " << best_triple.total_trades << " trades" << std::endl;
    }
    std::cout << std::endl;
    std::cout << "  2. Does it improve returns or risk-adjusted returns?" << std::endl;
    if (found_single && found_dual) {
        double return_improvement = (best_dual.return_mult - best_single.return_mult) / best_single.return_mult * 100.0;
        double sharpe_improvement = (best_dual.sharpe_proxy - best_single.sharpe_proxy) / best_single.sharpe_proxy * 100.0;
        std::cout << "     -> Dual vs Single: Return "
                  << (return_improvement >= 0 ? "+" : "") << std::fixed << std::setprecision(1)
                  << return_improvement << "%, Sharpe "
                  << (sharpe_improvement >= 0 ? "+" : "") << sharpe_improvement << "%" << std::endl;
    }
    if (found_single && found_triple) {
        double return_improvement = (best_triple.return_mult - best_single.return_mult) / best_single.return_mult * 100.0;
        double sharpe_improvement = (best_triple.sharpe_proxy - best_single.sharpe_proxy) / best_single.sharpe_proxy * 100.0;
        std::cout << "     -> Triple vs Single: Return "
                  << (return_improvement >= 0 ? "+" : "") << std::fixed << std::setprecision(1)
                  << return_improvement << "%, Sharpe "
                  << (sharpe_improvement >= 0 ? "+" : "") << sharpe_improvement << "%" << std::endl;
    }
    std::cout << std::endl;
    std::cout << "  3. Does it reduce DD through diversification?" << std::endl;
    if (found_single && found_dual && found_triple) {
        std::cout << "     -> Single best DD: " << std::fixed << std::setprecision(1) << best_single.max_dd_pct << "%" << std::endl;
        std::cout << "     -> Dual best DD:   " << best_dual.max_dd_pct << "%" << std::endl;
        std::cout << "     -> Triple best DD: " << best_triple.max_dd_pct << "%" << std::endl;
    }

    return 0;
}
