/**
 * SIMD Benchmark Test
 *
 * Measures performance of vectorized operations vs scalar
 * Build: cd build && cmake .. -G "MinGW Makefiles" && mingw32-make test_simd_benchmark
 */

#include "../include/simd_ops.h"
#include <iostream>
#include <chrono>
#include <random>
#include <iomanip>

using namespace std::chrono;

// Timing helper
template<typename Func>
double measure_ms(Func&& func, int iterations = 100) {
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count() / 1000.0 / iterations;
}

// Scalar reference implementations for comparison
namespace scalar {
    double sum(const double* data, size_t n) {
        double result = 0;
        for (size_t i = 0; i < n; ++i) result += data[i];
        return result;
    }

    double max(const double* data, size_t n) {
        double result = data[0];
        for (size_t i = 1; i < n; ++i)
            if (data[i] > result) result = data[i];
        return result;
    }

    void calculate_pnl(const double* entry, const double* lots, double price,
                       double contract, double* output, size_t n, bool is_buy) {
        for (size_t i = 0; i < n; ++i) {
            double diff = is_buy ? (price - entry[i]) : (entry[i] - price);
            output[i] = diff * lots[i] * contract;
        }
    }
}

int main() {
    // Initialize SIMD
    simd::init();
    simd::print_cpu_features();
    std::cout << "\n";

    // Setup random data
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(1800.0, 2000.0);
    std::uniform_real_distribution<double> lot_dist(0.01, 1.0);

    const size_t N = 1000000;  // 1M elements
    const size_t N_POSITIONS = 1000;  // Typical grid size
    const int ITERATIONS = 100;

    std::vector<double> prices(N);
    std::vector<double> output(N);
    std::vector<double> entry_prices(N_POSITIONS);
    std::vector<double> lot_sizes(N_POSITIONS);
    std::vector<double> pnl_output(N_POSITIONS);

    for (size_t i = 0; i < N; ++i) prices[i] = price_dist(rng);
    for (size_t i = 0; i < N_POSITIONS; ++i) {
        entry_prices[i] = price_dist(rng);
        lot_sizes[i] = lot_dist(rng);
    }

    std::cout << "=== Benchmark Results (averaged over " << ITERATIONS << " iterations) ===\n\n";
    std::cout << std::fixed << std::setprecision(3);

    // Test 1: Sum
    std::cout << "1. Sum of " << N << " doubles:\n";
    double scalar_sum_time = measure_ms([&]() {
        volatile double r = scalar::sum(prices.data(), N);
        (void)r;
    }, ITERATIONS);
    double simd_sum_time = measure_ms([&]() {
        volatile double r = simd::sum(prices.data(), N);
        (void)r;
    }, ITERATIONS);
    std::cout << "   Scalar:  " << scalar_sum_time << " ms\n";
    std::cout << "   SIMD:    " << simd_sum_time << " ms\n";
    std::cout << "   Speedup: " << scalar_sum_time / simd_sum_time << "x\n\n";

    // Test 2: Max
    std::cout << "2. Max of " << N << " doubles:\n";
    double scalar_max_time = measure_ms([&]() {
        volatile double r = scalar::max(prices.data(), N);
        (void)r;
    }, ITERATIONS);
    double simd_max_time = measure_ms([&]() {
        volatile double r = simd::max_value(prices.data(), N);  // Highway auto-dispatch
        (void)r;
    }, ITERATIONS);
    std::cout << "   Scalar:  " << scalar_max_time << " ms\n";
    std::cout << "   SIMD:    " << simd_max_time << " ms (Highway auto-dispatch)\n";
    std::cout << "   Speedup: " << scalar_max_time / simd_max_time << "x\n\n";

    // Test 3: P/L Calculation - use larger position count for measurable time
    const size_t LARGE_POSITIONS = 100000;
    std::vector<double> large_entry(LARGE_POSITIONS);
    std::vector<double> large_lots(LARGE_POSITIONS);
    std::vector<double> large_pnl(LARGE_POSITIONS);
    for (size_t i = 0; i < LARGE_POSITIONS; ++i) {
        large_entry[i] = price_dist(rng);
        large_lots[i] = lot_dist(rng);
    }

    std::cout << "3. P/L for " << LARGE_POSITIONS << " positions:\n";
    double scalar_pnl_time = measure_ms([&]() {
        scalar::calculate_pnl(large_entry.data(), large_lots.data(), 1950.0,
                              100.0, large_pnl.data(), LARGE_POSITIONS, true);
    }, ITERATIONS);
    double simd_pnl_time = measure_ms([&]() {
        simd::calculate_pnl_batch(large_entry.data(), large_lots.data(), 1950.0,
                                  100.0, large_pnl.data(), LARGE_POSITIONS, true);
    }, ITERATIONS);
    std::cout << "   Scalar:  " << scalar_pnl_time * 1000 << " us\n";
    std::cout << "   SIMD:    " << simd_pnl_time * 1000 << " us (Highway auto-dispatch)\n";
    std::cout << "   Speedup: " << scalar_pnl_time / simd_pnl_time << "x\n\n";

    // Test 4: Drawdown
    std::cout << "4. Max Drawdown of " << N << " equity points:\n";
    // Create equity curve (cumulative sum with some variance)
    std::vector<double> equity(N);
    equity[0] = 10000;
    for (size_t i = 1; i < N; ++i) {
        equity[i] = equity[i-1] + (price_dist(rng) - 1900) * 0.1;
    }
    double simd_dd_time = measure_ms([&]() {
        volatile double r = simd::max_drawdown(equity.data(), N);
        (void)r;
    }, ITERATIONS);
    std::cout << "   SIMD:    " << simd_dd_time << " ms\n\n";

    // Verify correctness
    std::cout << "=== Correctness Check ===\n";
    double scalar_sum = scalar::sum(prices.data(), 1000);
    double simd_sum_result = simd::sum(prices.data(), 1000);
    std::cout << "Sum (1000 elements):\n";
    std::cout << "   Scalar: " << scalar_sum << "\n";
    std::cout << "   SIMD:   " << simd_sum_result << "\n";
    std::cout << "   Diff:   " << std::abs(scalar_sum - simd_sum_result) << "\n\n";

    std::cout << "Benchmark complete.\n";
    return 0;
}
