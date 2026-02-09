#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include "../include/simple_price_level_breakout.h"

// Function to load CSV price data
std::vector<Bar> LoadPriceDataFromCSV(const std::string& filename) {
    std::vector<Bar> bars;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open file: " << filename << std::endl;
        std::cerr << "Please export EURUSD H1 data from MT5 for January 2024" << std::endl;
        std::cerr << "Format: Timestamp,Open,High,Low,Close" << std::endl;
        return bars;
    }

    std::string line;
    // Skip header if present
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string timestamp, open_str, high_str, low_str, close_str;

        // MT5 CSV export uses TAB delimiters
        std::getline(ss, timestamp, '\t');
        std::getline(ss, open_str, '\t');
        std::getline(ss, high_str, '\t');
        std::getline(ss, low_str, '\t');
        std::getline(ss, close_str, '\t');

        Bar bar;
        bar.timestamp = timestamp;
        bar.open = std::stod(open_str);
        bar.high = std::stod(high_str);
        bar.low = std::stod(low_str);
        bar.close = std::stod(close_str);

        bars.push_back(bar);
    }

    file.close();
    return bars;
}

// Function to create sample test data if CSV not available
std::vector<Bar> CreateSampleTestData() {
    std::vector<Bar> bars;

    std::cout << "Creating sample test data (simulated EURUSD movement)..." << std::endl;

    // Starting price around 1.1950
    double price = 1.1950;

    // Create 100 bars with price movement
    for (int i = 0; i < 100; ++i) {
        Bar bar;
        bar.timestamp = "2024-01-01 " + std::to_string(i) + ":00:00";

        // Simulate price movement
        if (i < 20) {
            // Move up towards long trigger (1.2000)
            price += 0.0003;
        } else if (i >= 20 && i < 40) {
            // Breakout and move up (trigger long)
            price += 0.0005;
        } else if (i >= 40 && i < 60) {
            // Move down (could hit TP or reverse)
            price -= 0.0002;
        } else if (i >= 60 && i < 80) {
            // Move down towards short trigger (1.1900)
            price -= 0.0004;
        } else {
            // Recovery
            price += 0.0002;
        }

        bar.open = price;
        bar.high = price + 0.0003;
        bar.low = price - 0.0003;
        bar.close = price + (i % 2 == 0 ? 0.0001 : -0.0001);

        bars.push_back(bar);
    }

    std::cout << "Created " << bars.size() << " sample bars" << std::endl;
    return bars;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "MT5 Comparison Test - C++ Backtest" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Try to load real data from CSV first
    std::cout << "Attempting to load EURUSD H1 data from CSV..." << std::endl;
    std::vector<Bar> price_data = LoadPriceDataFromCSV("EURUSD_H1_202401.csv");

    // If no CSV data, create sample data
    if (price_data.empty()) {
        std::cout << "\nNo CSV file found. Using sample test data." << std::endl;
        std::cout << "To use real MT5 data:" << std::endl;
        std::cout << "1. Export EURUSD H1 data from MT5 (Jan 2024)" << std::endl;
        std::cout << "2. Save as: EURUSD_H1_202401.csv" << std::endl;
        std::cout << "3. Format: Timestamp,Open,High,Low,Close\n" << std::endl;

        price_data = CreateSampleTestData();
    } else {
        std::cout << "Successfully loaded " << price_data.size() << " bars from CSV\n" << std::endl;
    }

    if (price_data.empty()) {
        std::cerr << "ERROR: No price data available!" << std::endl;
        return 1;
    }

    // Create strategy instance with same parameters as MT5 EA
    // Adjusted for January 2024 EURUSD range (1.08-1.10)
    SimplePriceLevelBreakout strategy(
        1.0950,  // Long trigger level (within Jan 2024 range)
        1.0900,  // Short trigger level (within Jan 2024 range)
        0.10,    // Lot size
        50,      // Stop loss (pips)
        100      // Take profit (pips)
    );

    // Load price data
    strategy.LoadPriceData(price_data);

    // Run backtest
    BacktestResults results = strategy.RunBacktest();

    // Save results to file for comparison
    std::cout << "\nSaving results to mt5_comparison_cpp_results.txt..." << std::endl;

    std::ofstream output("mt5_comparison_cpp_results.txt");
    output << "C++ Backtest Engine Results" << std::endl;
    output << "===========================\n" << std::endl;
    output << "Initial Balance: $" << std::fixed << std::setprecision(2) << results.initial_balance << std::endl;
    output << "Final Balance: $" << results.final_balance << std::endl;
    output << "Total P/L: $" << results.total_profit_loss << std::endl;
    output << "Total Trades: " << results.total_trades << std::endl;
    output << "Winning Trades: " << results.winning_trades << std::endl;
    output << "Losing Trades: " << results.losing_trades << std::endl;

    if (results.total_trades > 0) {
        double win_rate = (double)results.winning_trades / results.total_trades * 100.0;
        output << "Win Rate: " << std::fixed << std::setprecision(1) << win_rate << "%" << std::endl;
    }

    output << "\nTrade Details:" << std::endl;
    output << "ID,Direction,Entry Time,Entry Price,Exit Price,Exit Reason,P/L" << std::endl;

    for (const auto& trade : results.trades) {
        output << trade.id << ","
               << trade.direction << ","
               << trade.entry_time << ","
               << std::fixed << std::setprecision(5) << trade.entry_price << ","
               << trade.exit_price << ","
               << trade.exit_reason << ","
               << std::fixed << std::setprecision(2) << trade.profit_loss
               << std::endl;
    }

    output.close();

    std::cout << "Results saved successfully!" << std::endl;
    std::cout << "\n========================================" << std::endl;
    std::cout << "Next Steps:" << std::endl;
    std::cout << "1. Run SimplePriceLevelBreakout EA in MT5" << std::endl;
    std::cout << "2. Export MT5 results" << std::endl;
    std::cout << "3. Compare with mt5_comparison_cpp_results.txt" << std::endl;
    std::cout << "========================================\n" << std::endl;

    return 0;
}
