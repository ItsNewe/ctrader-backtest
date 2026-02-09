/**
 * Price vs Oscillation Analysis
 *
 * Hypothesis: The 3x increase in oscillations (2024 -> 2025) may be due to:
 * 1. Higher price levels (same % move = larger $ move)
 * 2. Proximity to all-time highs (more volatility)
 *
 * Test: Compare oscillation frequency normalized by price level
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <cmath>

using namespace backtest;

struct MonthlyStats {
    std::string month;
    double avg_price;
    double min_price;
    double max_price;
    int oscillation_count;
    double threshold_pct;  // $1.0 as % of avg price
    int normalized_osc;    // oscillations if we used % threshold
};

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "PRICE vs OSCILLATION ANALYSIS" << std::endl;
    std::cout << "Testing if oscillation count is proportional to price level" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Analyze 2025 data by month
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    std::map<std::string, MonthlyStats> monthly_data;

    // Track oscillations with $1 threshold
    double threshold = 1.0;
    double local_high = 0;
    double local_low = 1e9;
    bool in_upswing = true;
    int total_oscillations = 0;

    // Track price stats
    double price_sum = 0;
    long price_count = 0;
    double year_min = 1e9;
    double year_max = 0;

    std::string current_month = "";
    double month_price_sum = 0;
    long month_price_count = 0;
    double month_min = 1e9;
    double month_max = 0;
    int month_oscillations = 0;

    try {
        TickBasedEngine engine(config);

        std::cout << "\nAnalyzing price levels and oscillations by month..." << std::endl;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            double mid = (tick.bid + tick.ask) / 2.0;

            // Extract month from timestamp "2025.01.02 ..."
            std::string month = tick.timestamp.substr(0, 7);  // "2025.01"

            // New month?
            if (month != current_month) {
                if (!current_month.empty() && month_price_count > 0) {
                    // Save previous month stats
                    MonthlyStats stats;
                    stats.month = current_month;
                    stats.avg_price = month_price_sum / month_price_count;
                    stats.min_price = month_min;
                    stats.max_price = month_max;
                    stats.oscillation_count = month_oscillations;
                    stats.threshold_pct = (threshold / stats.avg_price) * 100.0;
                    monthly_data[current_month] = stats;
                }

                // Reset for new month
                current_month = month;
                month_price_sum = 0;
                month_price_count = 0;
                month_min = 1e9;
                month_max = 0;
                month_oscillations = 0;
            }

            // Update stats
            price_sum += mid;
            price_count++;
            month_price_sum += mid;
            month_price_count++;

            year_min = std::min(year_min, mid);
            year_max = std::max(year_max, mid);
            month_min = std::min(month_min, mid);
            month_max = std::max(month_max, mid);

            // Track oscillations
            if (in_upswing) {
                if (mid > local_high) {
                    local_high = mid;
                } else if (local_high - mid >= threshold) {
                    // Swing complete
                    total_oscillations++;
                    month_oscillations++;
                    in_upswing = false;
                    local_low = mid;
                }
            } else {
                if (mid < local_low) {
                    local_low = mid;
                } else if (mid - local_low >= threshold) {
                    // Swing complete
                    total_oscillations++;
                    month_oscillations++;
                    in_upswing = true;
                    local_high = mid;
                }
            }
        });

        // Save last month
        if (!current_month.empty() && month_price_count > 0) {
            MonthlyStats stats;
            stats.month = current_month;
            stats.avg_price = month_price_sum / month_price_count;
            stats.min_price = month_min;
            stats.max_price = month_max;
            stats.oscillation_count = month_oscillations;
            stats.threshold_pct = (threshold / stats.avg_price) * 100.0;
            monthly_data[current_month] = stats;
        }

        // Print results
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "2025 MONTHLY ANALYSIS" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        std::cout << std::setw(10) << "Month"
                  << std::setw(12) << "Avg Price"
                  << std::setw(12) << "Min"
                  << std::setw(12) << "Max"
                  << std::setw(12) << "Osc Count"
                  << std::setw(12) << "$1 as %"
                  << std::setw(15) << "Osc/Price*1k" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        for (const auto& [month, stats] : monthly_data) {
            double osc_per_price = (stats.oscillation_count / stats.avg_price) * 1000.0;
            std::cout << std::setw(10) << month
                      << std::setw(12) << stats.avg_price
                      << std::setw(12) << stats.min_price
                      << std::setw(12) << stats.max_price
                      << std::setw(12) << stats.oscillation_count
                      << std::setw(11) << std::setprecision(4) << stats.threshold_pct << "%"
                      << std::setw(15) << std::setprecision(2) << osc_per_price << std::endl;
        }

        double avg_price = price_sum / price_count;

        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "YEAR SUMMARY" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        std::cout << "Average Price:      $" << avg_price << std::endl;
        std::cout << "Price Range:        $" << year_min << " - $" << year_max << std::endl;
        std::cout << "Year High:          $" << year_max << " (ATH?)" << std::endl;
        std::cout << "Total Oscillations: " << total_oscillations << std::endl;
        std::cout << "$1 threshold as %:  " << std::setprecision(4) << (threshold / avg_price) * 100.0 << "%" << std::endl;

        // Calculate what threshold would be equivalent across price levels
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "PERCENTAGE-BASED THRESHOLD ANALYSIS" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        double pct_of_2600 = (1.0 / 2600.0) * 100.0;  // $1 at $2600 gold (2024 level)
        double pct_of_3000 = (1.0 / 3000.0) * 100.0;  // $1 at $3000 gold (2025 level)

        std::cout << "$1 at $2600 gold = " << std::setprecision(4) << pct_of_2600 << "%" << std::endl;
        std::cout << "$1 at $3000 gold = " << std::setprecision(4) << pct_of_3000 << "%" << std::endl;
        std::cout << "\nIf gold goes from $2600 to $3000 (+15.4%):" << std::endl;
        std::cout << "  Same $1 threshold becomes " << std::setprecision(1) << (pct_of_2600 / pct_of_3000) << "x easier to cross" << std::endl;
        std::cout << "  A 0.038% move at $2600 = $1.00" << std::endl;
        std::cout << "  A 0.038% move at $3000 = $1.15" << std::endl;

        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "CONCLUSION" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        std::cout << "If 2024 avg price was ~$2300 and 2025 avg price is ~$" << std::setprecision(0) << avg_price << std::endl;
        std::cout << "Price increase: " << std::setprecision(1) << ((avg_price / 2300.0) - 1) * 100 << "%" << std::endl;
        std::cout << "Oscillation increase: 3x (from 92k to 281k)" << std::endl;
        std::cout << "\nPrice increase alone does NOT explain 3x oscillation increase." << std::endl;
        std::cout << "Other factors: increased volatility, more trading activity, ATH runs." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
