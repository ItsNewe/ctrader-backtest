/**
 * Multi-Instrument Grid Test - OPTIMIZED VERSION
 *
 * Performance optimizations from TickBasedEngine:
 * - SIMD batch P/L calculations (AVX2)
 * - SIMD batch margin calculations
 * - Pre-allocated buffers (no per-tick allocations)
 * - Parallel parameter sweep (OpenMP)
 * - Object pooling for positions
 *
 * Tests XAUUSD + XAGUSD from same account
 * Period: 2025.01.01 - 2026.01.29
 */

#include "../include/simd_intrinsics.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cfloat>

// std::thread for parallel execution (OpenMP has linking issues on MinGW)

using namespace backtest;

// Compact position struct for cache efficiency
struct Position {
    double entry_price;
    double lot_size;
};

// Per-symbol state with pre-allocated buffers and dirty flag optimization
struct SymbolState {
    std::string symbol;
    double contract_size;
    double survive_down;
    double min_volume;
    double max_volume;
    double volume_of_open_trades;
    double checked_last_open_price;

    // Positions stored contiguously for SIMD
    std::vector<Position> positions;

    // SIMD cache dirty flag - only refresh when positions change
    mutable bool cache_dirty = true;

    // Pre-allocated SIMD buffers (avoid per-tick allocation)
    mutable std::vector<double> entry_prices_cache;
    mutable std::vector<double> lot_sizes_cache;
    mutable std::vector<double> pnl_cache;

    void InvalidateCache() {
        cache_dirty = true;
    }

    void RefreshCache() const {
        if (!cache_dirty) return;  // Skip if cache is still valid

        size_t n = positions.size();
        if (entry_prices_cache.size() < n) {
            entry_prices_cache.resize(n);
            lot_sizes_cache.resize(n);
            pnl_cache.resize(n);
        }
        for (size_t i = 0; i < n; i++) {
            entry_prices_cache[i] = positions[i].entry_price;
            lot_sizes_cache[i] = positions[i].lot_size;
        }
        cache_dirty = false;
    }

    void ReserveCapacity(size_t n) {
        positions.reserve(n);
        entry_prices_cache.reserve(n);
        lot_sizes_cache.reserve(n);
        pnl_cache.reserve(n);
    }

    void AddPosition(double entry_price, double lot_size) {
        Position pos;
        pos.entry_price = entry_price;
        pos.lot_size = lot_size;
        positions.push_back(pos);
        volume_of_open_trades += lot_size;
        InvalidateCache();
    }

    bool RemovePosition(size_t idx, double close_price, double& profit_out) {
        if (idx >= positions.size()) return false;
        const auto& p = positions[idx];
        profit_out = (close_price - p.entry_price) * p.lot_size * contract_size;
        volume_of_open_trades -= p.lot_size;
        positions.erase(positions.begin() + idx);
        InvalidateCache();
        return true;
    }
};

struct TestResult {
    double au_survive, ag_survive;
    double final_equity;
    double ret;
    double max_dd_pct;
    int total_trades;
    int au_entries, ag_entries;
    int positions_closed;
    bool stop_out;
};

class OptimizedMultiSymbolEngine {
public:
    double initial_balance;
    double balance;
    double peak_equity;
    double max_dd_pct;
    bool stop_out;
    double margin_stop_out_level;
    double leverage;
    int account_limit_orders;
    int positions_closed;

    SymbolState gold;
    SymbolState silver;

    // Pre-allocated price buffers for margin calculation
    mutable std::vector<double> margin_prices_buffer_;

    static constexpr size_t SIMD_THRESHOLD = 8;

    OptimizedMultiSymbolEngine(double init_bal, double au_survive, double ag_survive) {
        initial_balance = init_bal;
        balance = init_bal;
        peak_equity = init_bal;
        max_dd_pct = 0;
        stop_out = false;
        margin_stop_out_level = 20.0;
        leverage = 500.0;
        account_limit_orders = 200;
        positions_closed = 0;

        // Gold setup
        gold.symbol = "XAUUSD";
        gold.contract_size = 100.0;
        gold.survive_down = au_survive;
        gold.min_volume = 0.01;
        gold.max_volume = 100.0;
        gold.volume_of_open_trades = 0;
        gold.checked_last_open_price = DBL_MIN;
        gold.ReserveCapacity(256);

        // Silver setup
        silver.symbol = "XAGUSD";
        silver.contract_size = 5000.0;
        silver.survive_down = ag_survive;
        silver.min_volume = 0.01;
        silver.max_volume = 100.0;
        silver.volume_of_open_trades = 0;
        silver.checked_last_open_price = DBL_MIN;
        silver.ReserveCapacity(256);

        margin_prices_buffer_.reserve(512);
    }

    // SIMD-optimized equity calculation
    double GetEquity(double au_bid, double ag_bid) const {
        double unrealized = 0;

        // Gold positions - use SIMD if enough positions
        if (gold.positions.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            gold.RefreshCache();
            simd::calculate_pnl_batch(
                gold.entry_prices_cache.data(),
                gold.lot_sizes_cache.data(),
                au_bid,
                gold.contract_size,
                gold.pnl_cache.data(),
                gold.positions.size(),
                true  // BUY positions
            );
            unrealized += simd::sum(gold.pnl_cache.data(), gold.positions.size());
        } else {
            for (const auto& p : gold.positions) {
                unrealized += (au_bid - p.entry_price) * p.lot_size * gold.contract_size;
            }
        }

        // Silver positions
        if (silver.positions.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            silver.RefreshCache();
            simd::calculate_pnl_batch(
                silver.entry_prices_cache.data(),
                silver.lot_sizes_cache.data(),
                ag_bid,
                silver.contract_size,
                silver.pnl_cache.data(),
                silver.positions.size(),
                true
            );
            unrealized += simd::sum(silver.pnl_cache.data(), silver.positions.size());
        } else {
            for (const auto& p : silver.positions) {
                unrealized += (ag_bid - p.entry_price) * p.lot_size * silver.contract_size;
            }
        }

        return balance + unrealized;
    }

    // SIMD-optimized margin calculation
    double GetUsedMargin(double au_ask, double ag_ask) const {
        double margin = 0;

        // Gold margin
        if (gold.positions.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            gold.RefreshCache();
            // Fill price buffer
            size_t n = gold.positions.size();
            if (margin_prices_buffer_.size() < n) {
                margin_prices_buffer_.resize(n);
            }
            std::fill(margin_prices_buffer_.begin(), margin_prices_buffer_.begin() + n, au_ask);

            margin += simd::total_margin_batch_avx2_optimized(
                gold.lot_sizes_cache.data(),
                margin_prices_buffer_.data(),
                n,
                gold.contract_size,
                leverage
            );
        } else {
            for (const auto& p : gold.positions) {
                margin += p.lot_size * gold.contract_size * au_ask / leverage;
            }
        }

        // Silver margin
        if (silver.positions.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            silver.RefreshCache();
            size_t n = silver.positions.size();
            if (margin_prices_buffer_.size() < n) {
                margin_prices_buffer_.resize(n);
            }
            std::fill(margin_prices_buffer_.begin(), margin_prices_buffer_.begin() + n, ag_ask);

            margin += simd::total_margin_batch_avx2_optimized(
                silver.lot_sizes_cache.data(),
                margin_prices_buffer_.data(),
                n,
                silver.contract_size,
                leverage
            );
        } else {
            for (const auto& p : silver.positions) {
                margin += p.lot_size * silver.contract_size * ag_ask / leverage;
            }
        }

        return margin;
    }

    int TotalPositions() const {
        return gold.positions.size() + silver.positions.size();
    }

    bool CloseSmallestProfitable(double au_bid, double ag_bid) {
        double min_vol = DBL_MAX;
        int best_idx = -1;
        bool best_is_gold = false;

        for (size_t i = 0; i < gold.positions.size(); i++) {
            const auto& p = gold.positions[i];
            double profit = (au_bid - p.entry_price) * p.lot_size * gold.contract_size;
            if (profit > 0 && p.lot_size < min_vol) {
                min_vol = p.lot_size;
                best_idx = i;
                best_is_gold = true;
            }
        }

        for (size_t i = 0; i < silver.positions.size(); i++) {
            const auto& p = silver.positions[i];
            double profit = (ag_bid - p.entry_price) * p.lot_size * silver.contract_size;
            if (profit > 0 && p.lot_size < min_vol) {
                min_vol = p.lot_size;
                best_idx = i;
                best_is_gold = false;
            }
        }

        if (best_idx >= 0) {
            double profit;
            if (best_is_gold) {
                gold.RemovePosition(best_idx, au_bid, profit);
            } else {
                silver.RemovePosition(best_idx, ag_bid, profit);
            }
            balance += profit;
            positions_closed++;
            return true;
        }
        return false;
    }

    double CalculateTradeSize(SymbolState& sym, double current_ask, double equity,
                              double used_margin) {
        double end_price = current_ask * ((100.0 - sym.survive_down) / 100.0);
        double distance = current_ask - end_price;
        double spread_and_commission = 0;  // Match extended test
        double initial_margin_rate = 1.0;  // Match extended test

        double numerator = 100.0 * equity * leverage
                         - 100.0 * sym.contract_size * std::fabs(distance) * sym.volume_of_open_trades * leverage
                         - leverage * margin_stop_out_level * used_margin;

        double denominator = sym.contract_size * (
            100.0 * std::fabs(distance) * leverage
            + 100.0 * spread_and_commission * leverage
            + current_ask * initial_margin_rate * margin_stop_out_level
        );

        if (denominator <= 0) return 0;

        double trade_size = numerator / denominator;
        trade_size = std::floor(trade_size * 100.0) / 100.0;  // Round to 2 decimals

        if (trade_size < sym.min_volume) return 0;
        return std::min(trade_size, sym.max_volume);
    }

    void ProcessSymbol(const std::string& symbol, double ask, double bid,
                      double other_ask, double other_bid) {
        if (stop_out) return;

        SymbolState& sym = (symbol == "XAUUSD") ? gold : silver;

        double au_ask = (symbol == "XAUUSD") ? ask : other_ask;
        double ag_ask = (symbol == "XAGUSD") ? ask : other_ask;
        double au_bid = (symbol == "XAUUSD") ? bid : other_bid;
        double ag_bid = (symbol == "XAGUSD") ? bid : other_bid;

        double equity = GetEquity(au_bid, ag_bid);
        double used_margin = GetUsedMargin(au_ask, ag_ask);

        double current_margin_level = (used_margin > 0) ? (equity / used_margin) * 100.0 : 0;

        if (used_margin > 0 && current_margin_level < margin_stop_out_level) {
            stop_out = true;
            return;
        }

        if (equity > peak_equity) peak_equity = equity;
        double dd = (peak_equity - equity) / peak_equity * 100.0;
        if (dd > max_dd_pct) max_dd_pct = dd;

        // "Up while up" entry logic
        if (sym.volume_of_open_trades == 0 || ask > sym.checked_last_open_price) {
            sym.checked_last_open_price = ask;

            // Recalculate volume
            sym.volume_of_open_trades = 0;
            for (const auto& p : sym.positions) {
                sym.volume_of_open_trades += p.lot_size;
            }

            bool should_open = false;
            if (sym.volume_of_open_trades == 0) {
                should_open = true;
            } else {
                double equity_at_target = (current_margin_level > 0) ?
                    equity * margin_stop_out_level / current_margin_level : equity;
                double equity_difference = equity - equity_at_target;
                double price_difference = equity_difference / (sym.volume_of_open_trades * sym.contract_size);
                double end_price = ask * ((100.0 - sym.survive_down) / 100.0);
                should_open = (ask - price_difference) < end_price;
            }

            if (should_open) {
                double trade_size = CalculateTradeSize(sym, ask, equity, used_margin);

                if (trade_size >= sym.min_volume) {
                    // Check position limit - must match extended test logic exactly
                    if (TotalPositions() >= account_limit_orders) {
                        if (!CloseSmallestProfitable(au_bid, ag_bid)) {
                            return;  // Exit if can't close a profitable position
                        }
                        // Recalculate after closing
                        equity = GetEquity(au_bid, ag_bid);
                        used_margin = GetUsedMargin(au_ask, ag_ask);
                        trade_size = CalculateTradeSize(sym, ask, equity, used_margin);
                        if (trade_size < sym.min_volume) return;
                    }

                    sym.AddPosition(ask, trade_size);  // Use optimized method that invalidates cache
                }
            }
        }
    }
};

// Merged tick for multi-instrument processing
struct MergedTick {
    uint64_t timestamp_ns;  // Numeric timestamp for fast comparison
    std::string symbol;
    double bid;
    double ask;
};

// Load and merge ticks from both instruments
std::vector<MergedTick> LoadAndMergeTicks() {
    std::vector<MergedTick> merged;
    merged.reserve(100000000);  // Pre-allocate for ~100M ticks

    auto start = std::chrono::high_resolution_clock::now();

    // Load Gold - use EXTENDED files to match test_grid_multi_extended.cpp
    std::cout << "Loading XAUUSD (extended)..." << std::flush;
    TickDataConfig au_config;
    au_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025_EXTENDED.csv";
    au_config.format = TickDataFormat::MT5_CSV;

    TickDataManager au_manager(au_config);
    au_manager.Reset();
    Tick tick;
    size_t au_count = 0;
    while (au_manager.GetNextTick(tick)) {
        MergedTick mt;
        mt.timestamp_ns = au_count++;  // Use index as proxy (will sort by string later)
        mt.symbol = "XAUUSD";
        mt.bid = tick.bid;
        mt.ask = tick.ask;
        // Store timestamp for sorting
        static thread_local std::string ts;
        ts = tick.timestamp;
        mt.timestamp_ns = std::hash<std::string>{}(ts);  // Simple hash
        merged.push_back(mt);
    }
    std::cout << " " << au_count << " ticks" << std::endl;

    // Load Silver - use EXTENDED files to match test_grid_multi_extended.cpp
    std::cout << "Loading XAGUSD (extended)..." << std::flush;
    TickDataConfig ag_config;
    ag_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025_EXTENDED.csv";
    ag_config.format = TickDataFormat::MT5_CSV;

    TickDataManager ag_manager(ag_config);
    ag_manager.Reset();
    size_t ag_count = 0;

    // Store timestamps for sorting
    std::vector<std::string> timestamps;
    timestamps.reserve(merged.size() + 30000000);

    // Re-read to get timestamps
    au_manager.Reset();
    while (au_manager.GetNextTick(tick)) {
        timestamps.push_back(tick.timestamp);
    }

    size_t au_final = timestamps.size();

    while (ag_manager.GetNextTick(tick)) {
        MergedTick mt;
        mt.symbol = "XAGUSD";
        mt.bid = tick.bid;
        mt.ask = tick.ask;
        merged.push_back(mt);
        timestamps.push_back(tick.timestamp);
        ag_count++;
    }
    std::cout << " " << ag_count << " ticks" << std::endl;

    // Create index array for sorting
    std::cout << "Sorting by timestamp..." << std::flush;
    std::vector<size_t> indices(merged.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;

    std::sort(indices.begin(), indices.end(), [&timestamps](size_t a, size_t b) {
        return timestamps[a] < timestamps[b];
    });

    // Reorder merged ticks
    std::vector<MergedTick> sorted;
    sorted.reserve(merged.size());
    for (size_t i : indices) {
        sorted.push_back(merged[i]);
    }
    merged = std::move(sorted);

    auto end = std::chrono::high_resolution_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << " done (" << dur.count() << "s)" << std::endl;
    std::cout << "Total: " << merged.size() << " ticks" << std::endl;

    return merged;
}

TestResult RunTest(const std::vector<MergedTick>& ticks, double au_survive, double ag_survive) {
    OptimizedMultiSymbolEngine engine(10000.0, au_survive, ag_survive);

    double last_au_ask = 2600.0, last_au_bid = 2600.0;
    double last_ag_ask = 30.0, last_ag_bid = 30.0;

    for (const auto& tick : ticks) {
        if (tick.symbol == "XAUUSD") {
            last_au_ask = tick.ask;
            last_au_bid = tick.bid;
            engine.ProcessSymbol("XAUUSD", tick.ask, tick.bid, last_ag_ask, last_ag_bid);
        } else {
            last_ag_ask = tick.ask;
            last_ag_bid = tick.bid;
            engine.ProcessSymbol("XAGUSD", tick.ask, tick.bid, last_au_ask, last_au_bid);
        }

        if (engine.stop_out) break;
    }

    TestResult result;
    result.au_survive = au_survive;
    result.ag_survive = ag_survive;
    result.stop_out = engine.stop_out;
    result.positions_closed = engine.positions_closed;

    if (!engine.stop_out) {
        result.final_equity = engine.GetEquity(last_au_bid, last_ag_bid);
        result.ret = result.final_equity / 10000.0;
        result.max_dd_pct = engine.max_dd_pct;
        result.au_entries = engine.gold.positions.size();
        result.ag_entries = engine.silver.positions.size();
        result.total_trades = result.au_entries + result.ag_entries + engine.positions_closed;
    }

    return result;
}

int main() {
    std::cout << "=== Multi-Instrument Grid Test (OPTIMIZED) ===" << std::endl;
    std::cout << "XAUUSD + XAGUSD from same account" << std::endl;
    std::cout << "Using SIMD (AVX2) and parallel execution" << std::endl;
    std::cout << std::endl;

    // Initialize SIMD
    simd::init();
    std::cout << "CPU Features: AVX2=" << simd::has_avx2()
              << " AVX512=" << simd::has_avx512() << std::endl;

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Threads: " << num_threads << " (std::thread)" << std::endl;
    std::cout << std::endl;

    auto load_start = std::chrono::high_resolution_clock::now();
    auto ticks = LoadAndMergeTicks();
    auto load_end = std::chrono::high_resolution_clock::now();

    if (ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return 1;
    }

    // Parameter sweep: Gold 12:1:150, Silver 18:1:25
    std::vector<double> au_survives;
    for (double s = 12; s <= 150; s += 1) au_survives.push_back(s);

    std::vector<double> ag_survives;
    for (double s = 18; s <= 25; s += 1) ag_survives.push_back(s);

    // Build parameter pairs
    std::vector<std::pair<double, double>> params;
    for (double au : au_survives) {
        for (double ag : ag_survives) {
            params.push_back({au, ag});
        }
    }

    int total = params.size();
    std::vector<TestResult> results(total);
    std::atomic<int> completed{0};
    std::mutex output_mutex;

    std::cout << "\nRunning " << total << " configurations (" << num_threads << " threads)..." << std::endl;

    auto sweep_start = std::chrono::high_resolution_clock::now();

    // Worker function for thread pool
    auto worker = [&](size_t start_idx, size_t end_idx) {
        for (size_t i = start_idx; i < end_idx; ++i) {
            results[i] = RunTest(ticks, params[i].first, params[i].second);

            int done = ++completed;
            if (done % 10 == 0 || done == total) {
                std::lock_guard<std::mutex> lock(output_mutex);
                std::cout << "  " << done << "/" << total << std::endl;
            }
        }
    };

    // Launch threads
    std::vector<std::thread> threads;
    size_t chunk_size = params.size() / num_threads;
    size_t remainder = params.size() % num_threads;
    size_t start_idx = 0;

    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t end_idx = start_idx + chunk_size + (t < remainder ? 1 : 0);
        threads.emplace_back(worker, start_idx, end_idx);
        start_idx = end_idx;
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    auto sweep_end = std::chrono::high_resolution_clock::now();

    // Sort by return
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.ret > b.ret;
    });

    // Print results
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== TOP 15 CONFIGURATIONS ===" << std::endl;
    std::cout << "AU%    AG%    Return     DD%     Trades  Closed  AU_pos  AG_pos" << std::endl;
    std::cout << "-----  -----  ---------  ------  ------  ------  ------  ------" << std::endl;

    for (int i = 0; i < 15 && i < (int)results.size(); i++) {
        const auto& r = results[i];
        if (r.stop_out) {
            std::cout << std::setw(5) << r.au_survive << "  "
                      << std::setw(5) << r.ag_survive << "  "
                      << "STOP-OUT" << std::endl;
        } else {
            std::cout << std::setw(5) << r.au_survive << "  "
                      << std::setw(5) << r.ag_survive << "  "
                      << std::setw(9) << r.ret << "x  "
                      << std::setw(6) << r.max_dd_pct << "%  "
                      << std::setw(6) << r.total_trades << "  "
                      << std::setw(6) << r.positions_closed << "  "
                      << std::setw(6) << r.au_entries << "  "
                      << std::setw(6) << r.ag_entries << std::endl;
        }
    }

    // Timing summary
    auto load_dur = std::chrono::duration_cast<std::chrono::seconds>(load_end - load_start);
    auto sweep_dur = std::chrono::duration_cast<std::chrono::seconds>(sweep_end - sweep_start);

    std::cout << "\n=== PERFORMANCE ===" << std::endl;
    std::cout << "Load time: " << load_dur.count() << "s" << std::endl;
    std::cout << "Sweep time: " << sweep_dur.count() << "s (" << total << " configs)" << std::endl;
    std::cout << "Time per config: " << (sweep_dur.count() * 1000.0 / total) << "ms" << std::endl;

    double ticks_per_sec = (ticks.size() * total) / (double)sweep_dur.count();
    std::cout << "Throughput: " << std::fixed << std::setprecision(0)
              << ticks_per_sec << " ticks/sec (aggregate)" << std::endl;

    // Best result
    if (!results.empty() && !results[0].stop_out) {
        const auto& best = results[0];
        std::cout << "\n=== BEST CONFIGURATION ===" << std::endl;
        std::cout << "XAUUSD survive = " << best.au_survive << "%" << std::endl;
        std::cout << "XAGUSD survive = " << best.ag_survive << "%" << std::endl;
        std::cout << "Return: " << best.ret << "x ($" << best.final_equity << ")" << std::endl;
        std::cout << "Max DD: " << best.max_dd_pct << "%" << std::endl;
        std::cout << "Trades: " << best.total_trades << " (closed: " << best.positions_closed << ")" << std::endl;
    }

    return 0;
}
