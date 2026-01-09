// Simple PositionValidator Test - Minimal version for quick verification
#include <iostream>
#include <cmath>

// Inline position validator functions for testing
namespace test {

bool ValidateLotSize(double volume, double min, double max, double step) {
    if (volume < min || volume > max) return false;
    double remainder = std::fmod(volume - min, step);
    return std::fabs(remainder) < 0.0001 || std::fabs(remainder - step) < 0.0001;
}

bool ValidateStopDistance(double entry, double stop, bool is_below, int min_points, double point) {
    if (stop == 0.0) return true;
    double distance = is_below ? (entry - stop) : (stop - entry);
    int points = static_cast<int>(distance / point + 0.5);
    return points >= min_points;
}

bool ValidateMargin(double required, double available) {
    return required <= available;
}

}  // namespace test

int main() {
    int passed = 0, failed = 0;

    std::cout << "Simple Position Validator Tests\n";
    std::cout << "================================\n\n";

    // Test 1: Valid lot size
    if (test::ValidateLotSize(0.10, 0.01, 100.0, 0.01)) {
        std::cout << "✅ PASS: Valid lot 0.10\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: Valid lot 0.10\n";
        failed++;
    }

    // Test 2: Below minimum
    if (!test::ValidateLotSize(0.005, 0.01, 100.0, 0.01)) {
        std::cout << "✅ PASS: Below minimum rejected\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: Below minimum rejected\n";
        failed++;
    }

    // Test 3: Above maximum
    if (!test::ValidateLotSize(150.0, 0.01, 100.0, 0.01)) {
        std::cout << "✅ PASS: Above maximum rejected\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: Above maximum rejected\n";
        failed++;
    }

    // Test 4: Invalid step
    if (!test::ValidateLotSize(0.015, 0.01, 100.0, 0.01)) {
        std::cout << "✅ PASS: Invalid step rejected\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: Invalid step rejected\n";
        failed++;
    }

    // Test 5: Valid SL distance
    if (test::ValidateStopDistance(1.20000, 1.19900, true, 10, 0.00001)) {
        std::cout << "✅ PASS: Valid SL distance (100 points)\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: Valid SL distance (100 points)\n";
        failed++;
    }

    // Test 6: SL too close
    if (!test::ValidateStopDistance(1.20000, 1.19995, true, 10, 0.00001)) {
        std::cout << "✅ PASS: SL too close rejected (5 < 10 points)\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: SL too close rejected (5 < 10 points)\n";
        failed++;
    }

    // Test 7: Sufficient margin
    if (test::ValidateMargin(100.0, 1000.0)) {
        std::cout << "✅ PASS: Sufficient margin\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: Sufficient margin\n";
        failed++;
    }

    // Test 8: Insufficient margin
    if (!test::ValidateMargin(1000.0, 500.0)) {
        std::cout << "✅ PASS: Insufficient margin rejected\n";
        passed++;
    } else {
        std::cout << "❌ FAIL: Insufficient margin rejected\n";
        failed++;
    }

    std::cout << "\n================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    if (failed == 0) {
        std::cout << "🎉 ALL TESTS PASSED!\n";
        return 0;
    } else {
        std::cout << "⚠️  SOME TESTS FAILED\n";
        return 1;
    }
}
