/**
 * Accuracy Test for Optimized Functions
 * Verifies that optimizations don't introduce numerical errors
 *
 * Build: g++ -O3 -march=native -mavx512f -mavx2 -mfma -std=c++17 -I include tests/test_accuracy.cpp -o test_accuracy.exe
 */

#include "../include/simd_intrinsics.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <iomanip>
#include <limits>

// Original scalar implementations for reference
namespace reference {
    double sum(const double* data, size_t n) {
        double result = 0;
        for (size_t i = 0; i < n; ++i) result += data[i];
        return result;
    }

    double max_val(const double* data, size_t n) {
        double result = data[0];
        for (size_t i = 1; i < n; ++i)
            if (data[i] > result) result = data[i];
        return result;
    }

    double min_val(const double* data, size_t n) {
        double result = data[0];
        for (size_t i = 1; i < n; ++i)
            if (data[i] < result) result = data[i];
        return result;
    }

    void calculate_pnl(const double* entry, const double* lots, double price,
                       double contract, double* output, size_t n, bool is_buy) {
        for (size_t i = 0; i < n; ++i) {
            double diff = is_buy ? (price - entry[i]) : (entry[i] - price);
            output[i] = diff * lots[i] * contract;
        }
    }

    double total_margin(const double* lots, const double* prices, size_t n,
                        double contract, double leverage) {
        double total = 0;
        for (size_t i = 0; i < n; ++i) {
            total += lots[i] * prices[i] * contract / leverage;
        }
        return total;
    }

    // Original timestamp parsing (with allocations)
    long parse_timestamp_original(const std::string& ts) {
        if (ts.size() < 19) return 0;
        int year = std::stoi(ts.substr(0, 4));
        int month = std::stoi(ts.substr(5, 2));
        int day = std::stoi(ts.substr(8, 2));
        int hour = std::stoi(ts.substr(11, 2));
        int minute = std::stoi(ts.substr(14, 2));
        int second = std::stoi(ts.substr(17, 2));
        int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    }

    // Original day of week (with allocations)
    int get_day_of_week_original(const std::string& date_str) {
        int year = std::stoi(date_str.substr(0, 4));
        int month = std::stoi(date_str.substr(5, 2));
        int day = std::stoi(date_str.substr(8, 2));
        if (month < 3) { month += 12; year--; }
        int century = year / 100;
        year = year % 100;
        int day_of_week = (day + (13 * (month + 1)) / 5 + year + year / 4 + century / 4 - 2 * century) % 7;
        day_of_week = (day_of_week + 6) % 7;
        return day_of_week;
    }
}

// Optimized timestamp parsing (zero-allocation)
namespace optimized {
    long parse_timestamp(const std::string& ts) {
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

    int get_day_of_week(const std::string& date_str) {
        if (date_str.size() < 10) return 0;
        int year = (date_str[0] - '0') * 1000 + (date_str[1] - '0') * 100 +
                   (date_str[2] - '0') * 10 + (date_str[3] - '0');
        int month = (date_str[5] - '0') * 10 + (date_str[6] - '0');
        int day = (date_str[8] - '0') * 10 + (date_str[9] - '0');
        if (month < 3) { month += 12; year--; }
        int century = year / 100;
        year = year % 100;
        int day_of_week = (day + (13 * (month + 1)) / 5 + year + year / 4 + century / 4 - 2 * century) % 7;
        day_of_week = (day_of_week + 6) % 7;
        return day_of_week;
    }
}

bool check_close(double a, double b, double tol = 1e-10) {
    if (a == 0 && b == 0) return true;
    double rel_diff = std::abs(a - b) / std::max(std::abs(a), std::abs(b));
    return rel_diff < tol;
}

int main() {
    std::cout << "=== Accuracy Verification Tests ===" << std::endl;
    std::cout << std::fixed << std::setprecision(10);

    simd::init();
    simd::print_cpu_features();
    std::cout << "\n";

    // Setup test data with various edge cases
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(1800.0, 2000.0);
    std::uniform_real_distribution<double> lot_dist(0.01, 1.0);

    const std::vector<size_t> test_sizes = {1, 3, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 1000, 10000};

    int total_tests = 0;
    int passed_tests = 0;

    std::cout << "=== 1. Sum Accuracy ===" << std::endl;
    for (size_t N : test_sizes) {
        std::vector<double> data(N);
        for (size_t i = 0; i < N; ++i) data[i] = price_dist(rng);

        double ref = reference::sum(data.data(), N);
        double simd_result = simd::sum(data.data(), N);

        total_tests++;
        if (check_close(ref, simd_result)) {
            passed_tests++;
        } else {
            std::cout << "  FAIL N=" << N << ": ref=" << ref << " simd=" << simd_result
                      << " diff=" << std::abs(ref - simd_result) << std::endl;
        }
    }
    std::cout << "  Sum tests: " << passed_tests << "/" << total_tests << " passed" << std::endl;

    int sum_passed = passed_tests;
    std::cout << "\n=== 2. Max Accuracy ===" << std::endl;
    for (size_t N : test_sizes) {
        std::vector<double> data(N);
        for (size_t i = 0; i < N; ++i) data[i] = price_dist(rng);

        double ref = reference::max_val(data.data(), N);
        double simd_result = simd::max_value(data.data(), N);

        total_tests++;
        if (check_close(ref, simd_result, 1e-15)) {
            passed_tests++;
        } else {
            std::cout << "  FAIL N=" << N << ": ref=" << ref << " simd=" << simd_result << std::endl;
        }
    }
    std::cout << "  Max tests: " << (passed_tests - sum_passed) << "/" << test_sizes.size() << " passed" << std::endl;

    int max_passed = passed_tests;
    std::cout << "\n=== 3. Min Accuracy ===" << std::endl;
    for (size_t N : test_sizes) {
        std::vector<double> data(N);
        for (size_t i = 0; i < N; ++i) data[i] = price_dist(rng);

        double ref = reference::min_val(data.data(), N);
        double simd_result = simd::min_value(data.data(), N);

        total_tests++;
        if (check_close(ref, simd_result, 1e-15)) {
            passed_tests++;
        } else {
            std::cout << "  FAIL N=" << N << ": ref=" << ref << " simd=" << simd_result << std::endl;
        }
    }
    std::cout << "  Min tests: " << (passed_tests - max_passed) << "/" << test_sizes.size() << " passed" << std::endl;

    int min_passed = passed_tests;
    std::cout << "\n=== 4. P/L Batch Accuracy ===" << std::endl;
    for (size_t N : test_sizes) {
        std::vector<double> entry(N), lots(N);
        std::vector<double> ref_pnl(N), simd_pnl(N);
        for (size_t i = 0; i < N; ++i) {
            entry[i] = price_dist(rng);
            lots[i] = lot_dist(rng);
        }

        double current_price = 1950.0;
        double contract = 100.0;

        // Test BUY
        reference::calculate_pnl(entry.data(), lots.data(), current_price, contract, ref_pnl.data(), N, true);
        simd::calculate_pnl_batch(entry.data(), lots.data(), current_price, contract, simd_pnl.data(), N, true);

        bool all_match = true;
        for (size_t i = 0; i < N; ++i) {
            if (!check_close(ref_pnl[i], simd_pnl[i])) {
                all_match = false;
                std::cout << "  FAIL BUY N=" << N << " i=" << i << ": ref=" << ref_pnl[i] << " simd=" << simd_pnl[i] << std::endl;
                break;
            }
        }
        total_tests++;
        if (all_match) passed_tests++;

        // Test SELL
        reference::calculate_pnl(entry.data(), lots.data(), current_price, contract, ref_pnl.data(), N, false);
        simd::calculate_pnl_batch(entry.data(), lots.data(), current_price, contract, simd_pnl.data(), N, false);

        all_match = true;
        for (size_t i = 0; i < N; ++i) {
            if (!check_close(ref_pnl[i], simd_pnl[i])) {
                all_match = false;
                std::cout << "  FAIL SELL N=" << N << " i=" << i << ": ref=" << ref_pnl[i] << " simd=" << simd_pnl[i] << std::endl;
                break;
            }
        }
        total_tests++;
        if (all_match) passed_tests++;
    }
    std::cout << "  P/L tests: " << (passed_tests - min_passed) << "/" << (test_sizes.size() * 2) << " passed" << std::endl;

    int pnl_passed = passed_tests;
    std::cout << "\n=== 5. Margin Calculation Accuracy ===" << std::endl;
    for (size_t N : test_sizes) {
        std::vector<double> lots(N), prices(N);
        for (size_t i = 0; i < N; ++i) {
            lots[i] = lot_dist(rng);
            prices[i] = price_dist(rng);
        }

        double contract = 100.0;
        double leverage = 500.0;

        double ref = reference::total_margin(lots.data(), prices.data(), N, contract, leverage);
        double simd_result = simd::total_margin_batch_avx2_optimized(lots.data(), prices.data(), N, contract, leverage);

        total_tests++;
        if (check_close(ref, simd_result)) {
            passed_tests++;
        } else {
            std::cout << "  FAIL N=" << N << ": ref=" << ref << " simd=" << simd_result
                      << " diff=" << std::abs(ref - simd_result) << std::endl;
        }
    }
    std::cout << "  Margin tests: " << (passed_tests - pnl_passed) << "/" << test_sizes.size() << " passed" << std::endl;

    int margin_passed = passed_tests;
    std::cout << "\n=== 6. Timestamp Parsing Accuracy ===" << std::endl;
    std::vector<std::string> test_timestamps = {
        "2025.01.01 00:00:00.000",
        "2025.01.15 12:30:45.123",
        "2025.02.28 23:59:59.999",
        "2025.03.01 00:00:00.000",
        "2024.02.29 12:00:00.000",  // Leap year
        "2025.06.15 09:15:30.500",
        "2025.12.31 23:59:59.999",
    };

    for (const auto& ts : test_timestamps) {
        long ref = reference::parse_timestamp_original(ts);
        long opt = optimized::parse_timestamp(ts);

        total_tests++;
        if (ref == opt) {
            passed_tests++;
        } else {
            std::cout << "  FAIL ts=" << ts << ": ref=" << ref << " opt=" << opt << std::endl;
        }
    }
    std::cout << "  Timestamp tests: " << (passed_tests - margin_passed) << "/" << test_timestamps.size() << " passed" << std::endl;

    int ts_passed = passed_tests;
    std::cout << "\n=== 7. Day-of-Week Accuracy ===" << std::endl;
    std::vector<std::string> test_dates = {
        "2025.01.01",  // Wednesday
        "2025.01.05",  // Sunday
        "2025.01.06",  // Monday
        "2025.02.14",  // Friday
        "2025.03.15",  // Saturday
        "2024.02.29",  // Thursday (leap year)
        "2025.12.25",  // Thursday
    };

    for (const auto& date : test_dates) {
        int ref = reference::get_day_of_week_original(date);
        int opt = optimized::get_day_of_week(date);

        total_tests++;
        if (ref == opt) {
            passed_tests++;
        } else {
            std::cout << "  FAIL date=" << date << ": ref=" << ref << " opt=" << opt << std::endl;
        }
    }
    std::cout << "  Day-of-week tests: " << (passed_tests - ts_passed) << "/" << test_dates.size() << " passed" << std::endl;

    // Summary
    std::cout << "\n=== SUMMARY ===" << std::endl;
    std::cout << "Total tests: " << total_tests << std::endl;
    std::cout << "Passed: " << passed_tests << std::endl;
    std::cout << "Failed: " << (total_tests - passed_tests) << std::endl;

    if (passed_tests == total_tests) {
        std::cout << "\n*** ALL ACCURACY TESTS PASSED ***" << std::endl;
        return 0;
    } else {
        std::cout << "\n*** SOME TESTS FAILED ***" << std::endl;
        return 1;
    }
}
