/**
 * Unit test for SIMD optimizations in tick_based_engine
 * Tests the vectorized P/L and margin calculations
 */

#include "../include/simd_ops.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <random>

using namespace std::chrono;

// Scalar reference implementations
namespace scalar {
    double calculate_total_pnl(const double* entry, const double* lots,
                               double price, double contract, size_t n, bool is_buy) {
        double total = 0;
        for (size_t i = 0; i < n; ++i) {
            double diff = is_buy ? (price - entry[i]) : (entry[i] - price);
            total += diff * lots[i] * contract;
        }
        return total;
    }

    double calculate_total_margin(const double* lots, const double* prices,
                                  size_t n, double contract, double leverage) {
        double total = 0;
        for (size_t i = 0; i < n; ++i) {
            total += lots[i] * prices[i] * contract / leverage;
        }
        return total;
    }

    void iterate_positions(const double* prices, const double* lots, size_t n,
                           double& sum, double& min_val, double& max_val) {
        sum = 0;
        min_val = prices[0];
        max_val = prices[0];
        for (size_t i = 0; i < n; ++i) {
            sum += lots[i];
            if (prices[i] < min_val) min_val = prices[i];
            if (prices[i] > max_val) max_val = prices[i];
        }
    }
}

int main() {
    std::cout << "=== SIMD Unit Tests ===" << std::endl;
    simd::init();
    simd::print_cpu_features();
    std::cout << "\n";

    // Setup test data - simulate 100 BUY positions
    const size_t N = 100;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(1800.0, 2000.0);
    std::uniform_real_distribution<double> lot_dist(0.01, 0.5);

    std::vector<double> entry_prices(N);
    std::vector<double> lot_sizes(N);
    for (size_t i = 0; i < N; ++i) {
        entry_prices[i] = price_dist(rng);
        lot_sizes[i] = lot_dist(rng);
    }

    double current_price = 1950.0;
    double contract_size = 100.0;
    double leverage = 500.0;

    std::cout << "Test Setup: " << N << " positions" << std::endl;
    std::cout << "Current Price: $" << current_price << std::endl;
    std::cout << "\n";

    // Test 1: P/L Calculation
    std::cout << "=== Test 1: P/L Calculation ===" << std::endl;

    double scalar_pnl = scalar::calculate_total_pnl(
        entry_prices.data(), lot_sizes.data(), current_price,
        contract_size, N, true);

    std::vector<double> pnl_buffer(N);
    simd::calculate_pnl_batch(entry_prices.data(), lot_sizes.data(),
                              current_price, contract_size,
                              pnl_buffer.data(), N, true);
    double simd_pnl = simd::sum(pnl_buffer.data(), N);

    std::cout << "  Scalar P/L: $" << std::fixed << std::setprecision(2) << scalar_pnl << std::endl;
    std::cout << "  SIMD P/L:   $" << simd_pnl << std::endl;
    std::cout << "  Diff:       $" << std::abs(scalar_pnl - simd_pnl) << std::endl;
    std::cout << "  PASS: " << (std::abs(scalar_pnl - simd_pnl) < 0.01 ? "Yes" : "NO") << std::endl;
    std::cout << "\n";

    // Test 2: Margin Calculation
    std::cout << "=== Test 2: Margin Calculation ===" << std::endl;

    double scalar_margin = scalar::calculate_total_margin(
        lot_sizes.data(), entry_prices.data(), N, contract_size, leverage);

    double simd_margin = simd::total_margin_batch(
        lot_sizes.data(), entry_prices.data(), N, contract_size, leverage);

    std::cout << "  Scalar Margin: $" << scalar_margin << std::endl;
    std::cout << "  SIMD Margin:   $" << simd_margin << std::endl;
    std::cout << "  Diff:          $" << std::abs(scalar_margin - simd_margin) << std::endl;
    std::cout << "  PASS: " << (std::abs(scalar_margin - simd_margin) < 0.01 ? "Yes" : "NO") << std::endl;
    std::cout << "\n";

    // Test 3: Iterate (sum, min, max)
    std::cout << "=== Test 3: Iterate (sum, min, max) ===" << std::endl;

    double scalar_sum, scalar_min, scalar_max;
    scalar::iterate_positions(entry_prices.data(), lot_sizes.data(), N,
                              scalar_sum, scalar_min, scalar_max);

    double simd_sum = simd::sum(lot_sizes.data(), N);
    double simd_min = simd::min_value(entry_prices.data(), N);
    double simd_max = simd::max_value(entry_prices.data(), N);

    std::cout << "  Scalar: sum=" << scalar_sum << ", min=" << scalar_min << ", max=" << scalar_max << std::endl;
    std::cout << "  SIMD:   sum=" << simd_sum << ", min=" << simd_min << ", max=" << simd_max << std::endl;
    bool pass3 = std::abs(scalar_sum - simd_sum) < 0.001 &&
                 std::abs(scalar_min - simd_min) < 0.001 &&
                 std::abs(scalar_max - simd_max) < 0.001;
    std::cout << "  PASS: " << (pass3 ? "Yes" : "NO") << std::endl;
    std::cout << "\n";

    // Performance benchmark
    std::cout << "=== Performance Benchmark (10000 iterations) ===" << std::endl;
    const int ITERATIONS = 10000;

    // P/L benchmark - use accumulator to prevent optimization
    double scalar_accum = 0;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        scalar_accum += scalar::calculate_total_pnl(
            entry_prices.data(), lot_sizes.data(), current_price + i * 0.0001,
            contract_size, N, true);
    }
    auto end = high_resolution_clock::now();
    double scalar_pnl_time = duration_cast<microseconds>(end - start).count() / 1000.0;
    std::cout << "(scalar accum: " << scalar_accum << ")" << std::endl;

    double simd_accum = 0;
    start = high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        simd::calculate_pnl_batch(entry_prices.data(), lot_sizes.data(),
                                  current_price + i * 0.0001, contract_size,
                                  pnl_buffer.data(), N, true);
        simd_accum += simd::sum(pnl_buffer.data(), N);
    }
    end = high_resolution_clock::now();
    double simd_pnl_time = duration_cast<microseconds>(end - start).count() / 1000.0;
    std::cout << "(simd accum: " << simd_accum << ")" << std::endl;

    std::cout << "P/L Calculation (" << N << " positions):" << std::endl;
    std::cout << "  Scalar: " << scalar_pnl_time << " ms" << std::endl;
    std::cout << "  SIMD:   " << simd_pnl_time << " ms" << std::endl;
    std::cout << "  Speedup: " << scalar_pnl_time / simd_pnl_time << "x" << std::endl;
    std::cout << "\n";

    // Iterate benchmark - use accumulator
    double iter_accum = 0;
    start = high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        double s, mi, ma;
        scalar::iterate_positions(entry_prices.data(), lot_sizes.data(), N, s, mi, ma);
        iter_accum += s + mi + ma;
    }
    end = high_resolution_clock::now();
    double scalar_iter_time = duration_cast<microseconds>(end - start).count() / 1000.0;
    std::cout << "(scalar iter accum: " << iter_accum << ")" << std::endl;

    iter_accum = 0;
    start = high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        double s = simd::sum(lot_sizes.data(), N);
        double mi = simd::min_value(entry_prices.data(), N);
        double ma = simd::max_value(entry_prices.data(), N);
        iter_accum += s + mi + ma;
    }
    end = high_resolution_clock::now();
    double simd_iter_time = duration_cast<microseconds>(end - start).count() / 1000.0;
    std::cout << "(simd iter accum: " << iter_accum << ")" << std::endl;

    std::cout << "Iterate (sum/min/max " << N << " positions):" << std::endl;
    std::cout << "  Scalar: " << scalar_iter_time << " ms" << std::endl;
    std::cout << "  SIMD:   " << simd_iter_time << " ms" << std::endl;
    std::cout << "  Speedup: " << scalar_iter_time / simd_iter_time << "x" << std::endl;

    std::cout << "\n=== All Tests Complete ===" << std::endl;
    return 0;
}
