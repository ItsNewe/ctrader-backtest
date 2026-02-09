/**
 * Minimal Working Example: MarginManager & SwapManager Integration
 *
 * This demonstrates how to use the MT5-validated classes in a simple backtest.
 * Compile: g++ -std=c++17 -I../include validation/example_integration.cpp -o example_integration
 */

#include "margin_manager.h"
#include "swap_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <ctime>

// Simple position structure
struct Position {
    std::string symbol;
    double lot_size;
    bool is_buy;
    double open_price;
    time_t open_time;
    double margin;
    double swap_accumulated;

    Position(const std::string& sym, double lots, bool buy, double price, time_t t, double m)
        : symbol(sym), lot_size(lots), is_buy(buy), open_price(price),
          open_time(t), margin(m), swap_accumulated(0.0) {}
};

// Simple backtest engine
class SimpleBacktestEngine {
private:
    double account_balance_;
    double account_equity_;
    double current_margin_used_;
    int leverage_;
    SwapManager swap_manager_;
    std::vector<Position> positions_;

    // Symbol specifications
    const double CONTRACT_SIZE = 100000.0;
    const double SWAP_LONG = -0.5;   // Points per lot per day
    const double SWAP_SHORT = 0.3;   // Points per lot per day
    const double POINT_VALUE = 0.00001;

public:
    SimpleBacktestEngine(double initial_balance, int leverage)
        : account_balance_(initial_balance),
          account_equity_(initial_balance),
          current_margin_used_(0.0),
          leverage_(leverage),
          swap_manager_(0)  // Swap at midnight
    {
        std::cout << "=== Backtest Engine Initialized ===" << std::endl;
        std::cout << "Initial Balance: $" << std::fixed << std::setprecision(2)
                  << initial_balance << std::endl;
        std::cout << "Leverage: 1:" << leverage << std::endl;
        std::cout << std::endl;
    }

    bool OpenPosition(const std::string& symbol, double lot_size, bool is_buy,
                     double price, time_t timestamp) {
        // Calculate required margin
        double required_margin = MarginManager::CalculateMargin(
            lot_size, CONTRACT_SIZE, price, leverage_
        );

        // Check if sufficient margin
        if (!MarginManager::HasSufficientMargin(
            account_equity_, current_margin_used_, required_margin, 100.0
        )) {
            std::cout << "❌ INSUFFICIENT MARGIN" << std::endl;
            std::cout << "   Required: $" << required_margin << std::endl;
            std::cout << "   Available: $" << GetFreeMargin() << std::endl;
            std::cout << "   Margin Level: " << GetMarginLevel() << "%" << std::endl;
            return false;
        }

        // Open position
        positions_.emplace_back(symbol, lot_size, is_buy, price, timestamp, required_margin);
        current_margin_used_ += required_margin;

        std::cout << "✓ POSITION OPENED" << std::endl;
        std::cout << "   " << symbol << " " << (is_buy ? "BUY" : "SELL")
                  << " " << lot_size << " lots @ " << price << std::endl;
        std::cout << "   Margin: $" << required_margin << std::endl;
        std::cout << "   Free Margin: $" << GetFreeMargin() << std::endl;
        std::cout << std::endl;

        return true;
    }

    void ClosePosition(size_t index, double close_price) {
        if (index >= positions_.size()) return;

        Position& pos = positions_[index];

        // Calculate profit
        double price_diff = pos.is_buy ?
            (close_price - pos.open_price) :
            (pos.open_price - close_price);
        double profit = pos.lot_size * CONTRACT_SIZE * price_diff;

        // Add accumulated swap
        profit += pos.swap_accumulated;

        // Update account
        account_balance_ += profit;
        account_equity_ = account_balance_;
        current_margin_used_ -= pos.margin;

        std::cout << "✓ POSITION CLOSED" << std::endl;
        std::cout << "   " << pos.symbol << " " << (pos.is_buy ? "BUY" : "SELL")
                  << " " << pos.lot_size << " lots @ " << close_price << std::endl;
        std::cout << "   Profit: $" << std::setprecision(2) << profit << std::endl;
        std::cout << "   (Swap: $" << pos.swap_accumulated << ")" << std::endl;
        std::cout << "   Balance: $" << account_balance_ << std::endl;
        std::cout << std::endl;

        // Remove position
        positions_.erase(positions_.begin() + index);
    }

    void ProcessTime(time_t current_time) {
        // Check if swap should be applied
        if (swap_manager_.ShouldApplySwap(current_time)) {
            ApplySwap(current_time);
        }
    }

    void ApplySwap(time_t current_time) {
        if (positions_.empty()) return;

        int day_of_week = SwapManager::GetDayOfWeek(current_time);

        std::cout << "⏰ APPLYING DAILY SWAP" << std::endl;

        struct tm* timeinfo = gmtime(&current_time);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
        std::cout << "   Time: " << buffer;

        const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        std::cout << " (" << days[day_of_week] << ")";
        if (day_of_week == 3) std::cout << " [TRIPLE SWAP]";
        std::cout << std::endl;

        double total_swap = 0.0;

        for (auto& pos : positions_) {
            double swap = SwapManager::CalculateSwap(
                pos.lot_size,
                pos.is_buy,
                SWAP_LONG,
                SWAP_SHORT,
                POINT_VALUE,
                CONTRACT_SIZE,
                day_of_week
            );

            pos.swap_accumulated += swap;
            account_balance_ += swap;
            total_swap += swap;

            std::cout << "   " << pos.symbol << " " << (pos.is_buy ? "BUY" : "SELL")
                      << " " << pos.lot_size << " lots: $"
                      << std::setprecision(4) << swap << std::endl;
        }

        std::cout << "   Total: $" << std::setprecision(2) << total_swap << std::endl;
        std::cout << "   Balance: $" << account_balance_ << std::endl;
        std::cout << std::endl;

        account_equity_ = account_balance_;
    }

    double GetBalance() const { return account_balance_; }
    double GetEquity() const { return account_equity_; }
    double GetMarginUsed() const { return current_margin_used_; }

    double GetFreeMargin() const {
        return MarginManager::GetFreeMargin(account_equity_, current_margin_used_);
    }

    double GetMarginLevel() const {
        return MarginManager::GetMarginLevel(account_equity_, current_margin_used_);
    }

    size_t GetOpenPositions() const { return positions_.size(); }

    void PrintStatus() {
        std::cout << "--- Account Status ---" << std::endl;
        std::cout << "Balance: $" << std::setprecision(2) << account_balance_ << std::endl;
        std::cout << "Equity: $" << account_equity_ << std::endl;
        std::cout << "Margin Used: $" << current_margin_used_ << std::endl;
        std::cout << "Free Margin: $" << GetFreeMargin() << std::endl;
        std::cout << "Margin Level: " << std::setprecision(2) << GetMarginLevel() << "%" << std::endl;
        std::cout << "Open Positions: " << positions_.size() << std::endl;
        std::cout << std::endl;
    }
};

int main() {
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "MINIMAL INTEGRATION EXAMPLE" << std::endl;
    std::cout << "Demonstrating MarginManager & SwapManager" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::endl;

    // Initialize engine
    SimpleBacktestEngine engine(10000.0, 500);

    // Simulate a simple backtest scenario
    std::cout << "=== Day 1: Opening Position ===" << std::endl;
    time_t day1 = 1733097600;  // 2025-12-02 00:00:00

    // Open position
    engine.OpenPosition("EURUSD", 0.01, true, 1.15958, day1);
    engine.PrintStatus();

    // Simulate time passing - check for swap at midnight
    std::cout << "=== Day 1: 23:00 (before swap) ===" << std::endl;
    engine.ProcessTime(day1 + 23 * 3600);

    // Next day - swap should apply
    std::cout << "=== Day 2: 00:00 (swap time) ===" << std::endl;
    time_t day2 = day1 + 24 * 3600;
    engine.ProcessTime(day2);
    engine.PrintStatus();

    // Day 3 - another swap
    std::cout << "=== Day 3: 00:00 (swap time) ===" << std::endl;
    time_t day3 = day2 + 24 * 3600;
    engine.ProcessTime(day3);
    engine.PrintStatus();

    // Close position
    std::cout << "=== Day 3: Closing Position ===" << std::endl;
    engine.ClosePosition(0, 1.16000);
    engine.PrintStatus();

    // Try to open larger position
    std::cout << "=== Testing Margin Limits ===" << std::endl;
    std::cout << "Attempting to open 1.0 lot position..." << std::endl;
    engine.OpenPosition("EURUSD", 1.0, true, 1.16000, day3);

    std::cout << "Attempting to open 0.1 lot position..." << std::endl;
    engine.OpenPosition("EURUSD", 0.1, true, 1.16000, day3);
    engine.PrintStatus();

    // Open another small position
    std::cout << "=== Opening Another Small Position ===" << std::endl;
    engine.OpenPosition("EURUSD", 0.05, false, 1.16000, day3);
    engine.PrintStatus();

    // Wednesday - triple swap test
    std::cout << "=== Day 4 (Wednesday): Triple Swap ===" << std::endl;
    time_t day4 = day3 + 24 * 3600;  // This should be Wednesday
    engine.ProcessTime(day4);
    engine.PrintStatus();

    // Close all positions
    std::cout << "=== Closing All Positions ===" << std::endl;
    while (engine.GetOpenPositions() > 0) {
        engine.ClosePosition(0, 1.16050);
    }

    // Final summary
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "FINAL RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    engine.PrintStatus();

    double final_balance = engine.GetBalance();
    double profit = final_balance - 10000.0;

    std::cout << "Initial Balance: $10,000.00" << std::endl;
    std::cout << "Final Balance: $" << std::setprecision(2) << final_balance << std::endl;
    std::cout << "Total Profit: $" << profit << std::endl;
    std::cout << std::endl;

    std::cout << "✓ Integration example complete!" << std::endl;
    std::cout << std::endl;
    std::cout << "This demonstrates:" << std::endl;
    std::cout << "  - Margin calculation before opening positions" << std::endl;
    std::cout << "  - Margin limit enforcement" << std::endl;
    std::cout << "  - Daily swap application at midnight" << std::endl;
    std::cout << "  - Triple swap on Wednesday" << std::endl;
    std::cout << "  - Proper margin release on position close" << std::endl;
    std::cout << std::endl;

    return 0;
}
