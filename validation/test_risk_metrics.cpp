/**
 * Risk-Adjusted Performance Metrics Calculator
 *
 * Runs the optimal FillUpOscillation strategy on XAUUSD 2025 and calculates
 * comprehensive risk metrics:
 * - Total Return
 * - Annualized Return
 * - Volatility (StdDev of daily returns, annualized)
 * - Sharpe Ratio (assuming 5% risk-free rate)
 * - Sortino Ratio (downside deviation only)
 * - Max Drawdown
 * - Calmar Ratio
 * - Recovery Factor
 * - Profit Factor
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <numeric>

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"

using namespace backtest;

// Structure to hold daily equity snapshot
struct DailySnapshot {
    std::string date;
    double equity;
    double balance;
};

// Structure to hold all risk metrics
struct RiskMetrics {
    // Basic returns
    double total_return;           // (final - initial) / initial
    double annualized_return;      // (1 + total_return)^(365/days) - 1

    // Risk measures
    double volatility;             // Annualized std dev of daily returns
    double downside_deviation;     // Annualized std dev of negative returns
    double max_drawdown;           // Maximum peak-to-trough decline
    double max_drawdown_pct;       // Max drawdown as percentage

    // Risk-adjusted returns
    double sharpe_ratio;           // (annualized_return - rf) / volatility
    double sortino_ratio;          // annualized_return / downside_deviation
    double calmar_ratio;           // annualized_return / max_drawdown_pct
    double recovery_factor;        // total_profit / max_drawdown

    // Profitability
    double profit_factor;          // gross_profit / abs(gross_loss)
    double gross_profit;
    double gross_loss;

    // Additional stats
    int trading_days;
    int total_trades;
    double win_rate;
};

// Calculate standard deviation
double CalculateStdDev(const std::vector<double>& values, double mean) {
    if (values.size() < 2) return 0.0;

    double sum_sq = 0.0;
    for (double val : values) {
        double diff = val - mean;
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq / (values.size() - 1));  // Sample std dev
}

// Calculate downside deviation (only negative returns)
double CalculateDownsideDeviation(const std::vector<double>& returns, double threshold = 0.0) {
    std::vector<double> negative_returns;
    for (double r : returns) {
        if (r < threshold) {
            negative_returns.push_back(r - threshold);
        }
    }

    if (negative_returns.size() < 2) return 0.0001;  // Avoid division by zero

    double sum_sq = 0.0;
    for (double val : negative_returns) {
        sum_sq += val * val;
    }
    return std::sqrt(sum_sq / negative_returns.size());
}

// Extract date from timestamp (YYYY.MM.DD from "YYYY.MM.DD HH:MM:SS")
std::string ExtractDate(const std::string& timestamp) {
    if (timestamp.length() >= 10) {
        return timestamp.substr(0, 10);
    }
    return timestamp;
}

int main() {
    std::cout << "=== Risk-Adjusted Performance Metrics Calculator ===" << std::endl;
    std::cout << "Strategy: FillUpOscillation (ADAPTIVE_SPACING mode)" << std::endl;
    std::cout << "Parameters: spacing=1.5, lookback=1h, survive=13%" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    // Configure tick data source
    TickDataConfig tick_config;
    #ifdef _WIN32
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    #else
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    #endif
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    // Configure backtest -  XAUUSD
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.commission_per_lot = 0.0;
    config.slippage_pips = 0.0;
    config.use_bid_ask_spread = true;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;

    // Full year 2025
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";

    // Swap rates from 
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;

    config.tick_data_config = tick_config;

    // Strategy parameters (optimal configuration)
    double survive_pct = 13.0;
    double base_spacing = 1.5;
    double min_volume = 0.01;
    double max_volume = 10.0;
    double contract_size = 100.0;
    double leverage = 500.0;
    double volatility_lookback_hours = 1.0;

    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Symbol: " << config.symbol << std::endl;
    std::cout << "  Period: 2025.01.01 - 2025.12.29" << std::endl;
    std::cout << "  Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "  Survive Down: " << survive_pct << "%" << std::endl;
    std::cout << "  Base Spacing: $" << base_spacing << std::endl;
    std::cout << "  Lookback: " << volatility_lookback_hours << "h" << std::endl;

    // Data collection
    std::vector<DailySnapshot> daily_snapshots;
    std::string current_day = "";
    double day_end_equity = 0.0;
    double day_end_balance = 0.0;

    // Track peak equity for max drawdown
    double peak_equity = config.initial_balance;
    double max_drawdown = 0.0;
    double max_drawdown_pct = 0.0;

    try {
        TickBasedEngine engine(config);

        // Create strategy with ADAPTIVE_SPACING mode
        FillUpOscillation strategy(
            survive_pct,
            base_spacing,
            min_volume,
            max_volume,
            contract_size,
            leverage,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,                      // antifragile_scale (not used in this mode)
            30.0,                     // velocity_threshold (not used)
            volatility_lookback_hours // lookback hours
        );

        std::cout << "\n--- Running Backtest ---\n" << std::endl;

        // Custom callback to track daily equity
        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            // Run strategy
            strategy.OnTick(tick, eng);

            // Track daily equity
            std::string tick_date = ExtractDate(tick.timestamp);
            double equity = eng.GetEquity();
            double balance = eng.GetBalance();

            // Update peak and drawdown
            if (equity > peak_equity) {
                peak_equity = equity;
            }
            double current_dd = peak_equity - equity;
            double current_dd_pct = (peak_equity > 0) ? (current_dd / peak_equity * 100.0) : 0.0;
            if (current_dd > max_drawdown) {
                max_drawdown = current_dd;
                max_drawdown_pct = current_dd_pct;
            }

            // Store end-of-day equity
            if (tick_date != current_day) {
                if (!current_day.empty()) {
                    // Save previous day's closing equity
                    daily_snapshots.push_back({current_day, day_end_equity, day_end_balance});
                }
                current_day = tick_date;
            }
            day_end_equity = equity;
            day_end_balance = balance;
        });

        // Save last day
        if (!current_day.empty()) {
            daily_snapshots.push_back({current_day, day_end_equity, day_end_balance});
        }

        // Get engine results
        auto results = engine.GetResults();

        std::cout << "\n=== Basic Results ===" << std::endl;
        std::cout << "Initial Balance:  $" << std::setprecision(2) << results.initial_balance << std::endl;
        std::cout << "Final Balance:    $" << results.final_balance << std::endl;
        std::cout << "Total P/L:        $" << results.total_profit_loss << std::endl;
        std::cout << "Total Trades:     " << results.total_trades << std::endl;
        std::cout << "Total Swap:       $" << results.total_swap_charged << std::endl;
        std::cout << "Trading Days:     " << daily_snapshots.size() << std::endl;

        // Calculate daily returns
        std::vector<double> daily_returns;
        for (size_t i = 1; i < daily_snapshots.size(); i++) {
            double prev_equity = daily_snapshots[i-1].equity;
            double curr_equity = daily_snapshots[i].equity;
            if (prev_equity > 0) {
                double daily_return = (curr_equity - prev_equity) / prev_equity;
                daily_returns.push_back(daily_return);
            }
        }

        // Calculate risk metrics
        RiskMetrics metrics;

        // Basic returns
        metrics.total_return = (results.final_balance - results.initial_balance) / results.initial_balance;
        int trading_days = (int)daily_snapshots.size();
        metrics.trading_days = trading_days;

        // Annualized return: (1 + total_return)^(365/days) - 1
        if (trading_days > 0) {
            metrics.annualized_return = std::pow(1.0 + metrics.total_return, 365.0 / trading_days) - 1.0;
        } else {
            metrics.annualized_return = 0.0;
        }

        // Daily return statistics
        double mean_daily_return = 0.0;
        if (!daily_returns.empty()) {
            mean_daily_return = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0) / daily_returns.size();
        }

        // Volatility (annualized)
        double daily_std_dev = CalculateStdDev(daily_returns, mean_daily_return);
        metrics.volatility = daily_std_dev * std::sqrt(252.0);  // Annualize

        // Downside deviation (annualized)
        double daily_downside = CalculateDownsideDeviation(daily_returns, 0.0);
        metrics.downside_deviation = daily_downside * std::sqrt(252.0);

        // Max drawdown
        metrics.max_drawdown = max_drawdown;
        metrics.max_drawdown_pct = max_drawdown_pct;

        // Sharpe Ratio (assuming 5% risk-free rate)
        double risk_free_rate = 0.05;
        if (metrics.volatility > 0.0001) {
            metrics.sharpe_ratio = (metrics.annualized_return - risk_free_rate) / metrics.volatility;
        } else {
            metrics.sharpe_ratio = 0.0;
        }

        // Sortino Ratio
        if (metrics.downside_deviation > 0.0001) {
            metrics.sortino_ratio = (metrics.annualized_return - risk_free_rate) / metrics.downside_deviation;
        } else {
            metrics.sortino_ratio = 0.0;
        }

        // Calmar Ratio (annualized return / max drawdown %)
        if (metrics.max_drawdown_pct > 0.0001) {
            metrics.calmar_ratio = (metrics.annualized_return * 100.0) / metrics.max_drawdown_pct;
        } else {
            metrics.calmar_ratio = 0.0;
        }

        // Recovery Factor (total profit / max drawdown)
        if (metrics.max_drawdown > 0.0001) {
            metrics.recovery_factor = results.total_profit_loss / metrics.max_drawdown;
        } else {
            metrics.recovery_factor = 0.0;
        }

        // Profit Factor
        metrics.gross_profit = 0.0;
        metrics.gross_loss = 0.0;
        for (const auto& trade : engine.GetClosedTrades()) {
            if (trade.profit_loss > 0) {
                metrics.gross_profit += trade.profit_loss;
            } else {
                metrics.gross_loss += trade.profit_loss;
            }
        }

        if (std::abs(metrics.gross_loss) > 0.0001) {
            metrics.profit_factor = metrics.gross_profit / std::abs(metrics.gross_loss);
        } else {
            metrics.profit_factor = (metrics.gross_profit > 0) ? 9999.0 : 0.0;
        }

        metrics.total_trades = results.total_trades;
        metrics.win_rate = results.win_rate;

        // Print formatted results
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "           RISK-ADJUSTED PERFORMANCE METRICS" << std::endl;
        std::cout << std::string(60, '=') << std::endl;

        std::cout << std::setprecision(4);
        std::cout << "\n  RETURNS" << std::endl;
        std::cout << "  " << std::string(56, '-') << std::endl;
        std::cout << "  Total Return:           " << std::setw(12) << (metrics.total_return * 100.0) << " %" << std::endl;
        std::cout << "  Annualized Return:      " << std::setw(12) << (metrics.annualized_return * 100.0) << " %" << std::endl;

        std::cout << "\n  RISK MEASURES" << std::endl;
        std::cout << "  " << std::string(56, '-') << std::endl;
        std::cout << "  Volatility (Annual):    " << std::setw(12) << (metrics.volatility * 100.0) << " %" << std::endl;
        std::cout << "  Downside Deviation:     " << std::setw(12) << (metrics.downside_deviation * 100.0) << " %" << std::endl;
        std::cout << "  Max Drawdown:           " << std::setw(12) << std::setprecision(2) << metrics.max_drawdown << " $" << std::endl;
        std::cout << "  Max Drawdown:           " << std::setw(12) << std::setprecision(4) << metrics.max_drawdown_pct << " %" << std::endl;

        std::cout << "\n  RISK-ADJUSTED RATIOS" << std::endl;
        std::cout << "  " << std::string(56, '-') << std::endl;
        std::cout << "  Sharpe Ratio:           " << std::setw(12) << std::setprecision(4) << metrics.sharpe_ratio << std::endl;
        std::cout << "  Sortino Ratio:          " << std::setw(12) << metrics.sortino_ratio << std::endl;
        std::cout << "  Calmar Ratio:           " << std::setw(12) << metrics.calmar_ratio << std::endl;
        std::cout << "  Recovery Factor:        " << std::setw(12) << metrics.recovery_factor << std::endl;

        std::cout << "\n  PROFITABILITY" << std::endl;
        std::cout << "  " << std::string(56, '-') << std::endl;
        std::cout << "  Profit Factor:          " << std::setw(12) << std::setprecision(4) << metrics.profit_factor << std::endl;
        std::cout << "  Gross Profit:           " << std::setw(12) << std::setprecision(2) << metrics.gross_profit << " $" << std::endl;
        std::cout << "  Gross Loss:             " << std::setw(12) << metrics.gross_loss << " $" << std::endl;

        std::cout << "\n  ADDITIONAL STATS" << std::endl;
        std::cout << "  " << std::string(56, '-') << std::endl;
        std::cout << "  Trading Days:           " << std::setw(12) << metrics.trading_days << std::endl;
        std::cout << "  Total Trades:           " << std::setw(12) << metrics.total_trades << std::endl;
        std::cout << "  Win Rate:               " << std::setw(12) << std::setprecision(2) << metrics.win_rate << " %" << std::endl;

        std::cout << "\n" << std::string(60, '=') << std::endl;

        // Interpretation
        std::cout << "\n  INTERPRETATION" << std::endl;
        std::cout << "  " << std::string(56, '-') << std::endl;

        if (metrics.sharpe_ratio > 2.0) {
            std::cout << "  Sharpe > 2.0: Excellent risk-adjusted returns" << std::endl;
        } else if (metrics.sharpe_ratio > 1.0) {
            std::cout << "  Sharpe > 1.0: Good risk-adjusted returns" << std::endl;
        } else if (metrics.sharpe_ratio > 0.5) {
            std::cout << "  Sharpe 0.5-1.0: Acceptable risk-adjusted returns" << std::endl;
        } else {
            std::cout << "  Sharpe < 0.5: Poor risk-adjusted returns" << std::endl;
        }

        if (metrics.sortino_ratio > metrics.sharpe_ratio) {
            std::cout << "  Sortino > Sharpe: Downside risk well managed" << std::endl;
        }

        if (metrics.calmar_ratio > 1.0) {
            std::cout << "  Calmar > 1.0: Good return relative to max drawdown" << std::endl;
        }

        if (metrics.profit_factor > 1.5) {
            std::cout << "  Profit Factor > 1.5: Strong edge in trading" << std::endl;
        }

        std::cout << "\n" << std::string(60, '=') << std::endl;

        // Strategy-specific stats
        std::cout << "\n  STRATEGY STATS" << std::endl;
        std::cout << "  " << std::string(56, '-') << std::endl;
        std::cout << "  Final Spacing:          $" << std::setprecision(2) << strategy.GetCurrentSpacing() << std::endl;
        std::cout << "  Spacing Changes:        " << strategy.GetAdaptiveSpacingChanges() << std::endl;
        std::cout << "  Velocity Pauses:        " << strategy.GetVelocityPauseCount() << std::endl;
        std::cout << "  Max Velocity Seen:      $" << strategy.GetMaxVelocity() << "/hr" << std::endl;
        std::cout << "  Strategy Peak Equity:   $" << std::setprecision(2) << strategy.GetPeakEquity() << std::endl;

        if (engine.IsStopOutOccurred()) {
            std::cout << "\n  !!! WARNING: MARGIN STOP-OUT OCCURRED !!!" << std::endl;
        }

        std::cout << "\nTest completed successfully." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
