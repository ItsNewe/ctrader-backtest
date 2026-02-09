/**
 * Risk Management Research
 *
 * Tests:
 * 1. Monte Carlo simulation for risk of ruin probability
 * 2. Hard equity stop (close all at X% DD)
 * 3. Profit withdrawal impact on survivability
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

using namespace backtest;

//=============================================================================
// Part 1: Monte Carlo Simulation
//=============================================================================

struct MCResult {
    int simulations;
    int ruined;
    double ruin_probability;
    double avg_final_balance;
    double median_final_balance;
    double worst_case;
    double best_case;
};

// Collect daily returns from actual backtest
std::vector<double> CollectDailyReturns() {
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
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    std::vector<double> daily_returns;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        std::string last_day = "";
        double day_start_equity = config.initial_balance;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);

            // Extract day from timestamp
            std::string current_day = tick.timestamp.substr(0, 10);

            if (current_day != last_day && !last_day.empty()) {
                // New day - record previous day's return
                double day_end_equity = eng.GetEquity();
                double daily_return = (day_end_equity - day_start_equity) / day_start_equity;
                daily_returns.push_back(daily_return);
                day_start_equity = day_end_equity;
            }
            last_day = current_day;
        });

    } catch (const std::exception& e) {
        std::cerr << "Error collecting daily returns: " << e.what() << std::endl;
    }

    return daily_returns;
}

MCResult RunMonteCarlo(const std::vector<double>& daily_returns, int num_simulations = 1000, int days = 252) {
    MCResult result;
    result.simulations = num_simulations;
    result.ruined = 0;

    std::vector<double> final_balances;
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<> dist(0, daily_returns.size() - 1);

    double initial = 10000.0;
    double ruin_threshold = initial * 0.1;  // 90% loss = ruin

    for (int sim = 0; sim < num_simulations; sim++) {
        double balance = initial;
        bool ruined = false;

        for (int day = 0; day < days && !ruined; day++) {
            // Random sample from historical daily returns
            int idx = dist(rng);
            double daily_return = daily_returns[idx];
            balance *= (1.0 + daily_return);

            if (balance < ruin_threshold) {
                ruined = true;
                result.ruined++;
            }
        }

        final_balances.push_back(balance);
    }

    // Calculate statistics
    std::sort(final_balances.begin(), final_balances.end());
    result.ruin_probability = (double)result.ruined / num_simulations * 100;

    double sum = 0;
    for (double b : final_balances) sum += b;
    result.avg_final_balance = sum / num_simulations;

    result.median_final_balance = final_balances[num_simulations / 2];
    result.worst_case = final_balances[0];
    result.best_case = final_balances[num_simulations - 1];

    return result;
}

//=============================================================================
// Part 2: Hard Equity Stop Test
//=============================================================================

struct EquityStopResult {
    double dd_threshold;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
    int stop_triggered;
    std::string stop_date;
};

EquityStopResult RunEquityStopTest(double dd_threshold_pct) {
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
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    EquityStopResult result;
    result.dd_threshold = dd_threshold_pct;
    result.stop_triggered = 0;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        double peak = config.initial_balance;
        double max_dd = 0;
        bool stopped = false;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            if (stopped) return;

            strategy.OnTick(tick, eng);

            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;

            // Check hard equity stop
            if (dd >= dd_threshold_pct && !stopped) {
                // Close all positions
                auto positions = eng.GetOpenPositions();
                for (Trade* t : positions) {
                    eng.ClosePosition(t, "Equity Stop");
                }
                stopped = true;
                result.stop_triggered = 1;
                result.stop_date = tick.timestamp.substr(0, 10);
            }
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.return_x = 0;
    }

    return result;
}

//=============================================================================
// Part 3: Profit Withdrawal Test
//=============================================================================

struct WithdrawalResult {
    std::string strategy;
    double final_balance;
    double total_withdrawn;
    double total_value;  // balance + withdrawn
    double max_dd_pct;
    bool survived;
};

WithdrawalResult RunWithdrawalTest(const std::string& name, double withdrawal_pct, int withdrawal_frequency_days) {
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
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    WithdrawalResult result;
    result.strategy = name;
    result.total_withdrawn = 0;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        double peak = config.initial_balance;
        double max_dd = 0;
        std::string last_day = "";
        int days_since_withdrawal = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);

            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;

            // Check for day change
            std::string current_day = tick.timestamp.substr(0, 10);
            if (current_day != last_day) {
                days_since_withdrawal++;
                last_day = current_day;

                // Withdrawal check
                if (withdrawal_pct > 0 && days_since_withdrawal >= withdrawal_frequency_days) {
                    double balance = eng.GetBalance();
                    double profit = balance - config.initial_balance;

                    if (profit > 0) {
                        double withdrawal = profit * withdrawal_pct;
                        // Simulate withdrawal by adjusting our tracking
                        // (Engine doesn't support actual withdrawal, so we track externally)
                        result.total_withdrawn += withdrawal;
                        days_since_withdrawal = 0;
                    }
                }
            }
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.total_value = result.final_balance + result.total_withdrawn;
        result.max_dd_pct = max_dd;
        result.survived = res.final_balance > config.initial_balance * 0.1;

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.survived = false;
    }

    return result;
}

//=============================================================================
// Main
//=============================================================================

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "RISK MANAGEMENT RESEARCH" << std::endl;
    std::cout << "FillUpOscillation ADAPTIVE_SPACING Mode" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    //=========================================================================
    // Part 1: Monte Carlo
    //=========================================================================
    std::cout << "\n" << std::string(80, '-') << std::endl;
    std::cout << "PART 1: MONTE CARLO SIMULATION" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    std::cout << "\nCollecting daily returns from 2025 backtest..." << std::endl;
    std::vector<double> daily_returns = CollectDailyReturns();
    std::cout << "Collected " << daily_returns.size() << " daily returns" << std::endl;

    // Calculate daily return statistics
    double sum = 0, sum_sq = 0;
    double min_ret = 1, max_ret = -1;
    for (double r : daily_returns) {
        sum += r;
        sum_sq += r * r;
        min_ret = std::min(min_ret, r);
        max_ret = std::max(max_ret, r);
    }
    double mean_ret = sum / daily_returns.size();
    double var = (sum_sq / daily_returns.size()) - (mean_ret * mean_ret);
    double std_ret = std::sqrt(var);

    std::cout << "\nDaily Return Statistics:" << std::endl;
    std::cout << "  Mean:     " << (mean_ret * 100) << "%" << std::endl;
    std::cout << "  Std Dev:  " << (std_ret * 100) << "%" << std::endl;
    std::cout << "  Min:      " << (min_ret * 100) << "%" << std::endl;
    std::cout << "  Max:      " << (max_ret * 100) << "%" << std::endl;

    std::cout << "\nRunning 1000 Monte Carlo simulations (252 trading days each)..." << std::endl;
    MCResult mc = RunMonteCarlo(daily_returns, 1000, 252);

    std::cout << "\nMonte Carlo Results:" << std::endl;
    std::cout << "  Simulations:        " << mc.simulations << std::endl;
    std::cout << "  Ruin Probability:   " << mc.ruin_probability << "%" << std::endl;
    std::cout << "  Avg Final Balance:  $" << mc.avg_final_balance << std::endl;
    std::cout << "  Median Final:       $" << mc.median_final_balance << std::endl;
    std::cout << "  Worst Case:         $" << mc.worst_case << std::endl;
    std::cout << "  Best Case:          $" << mc.best_case << std::endl;

    //=========================================================================
    // Part 2: Hard Equity Stop
    //=========================================================================
    std::cout << "\n" << std::string(80, '-') << std::endl;
    std::cout << "PART 2: HARD EQUITY STOP TEST" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    std::vector<double> dd_thresholds = {30.0, 40.0, 50.0, 60.0, 70.0, 100.0};  // 100 = no stop

    std::cout << "\n" << std::setw(12) << "DD Thresh"
              << std::setw(12) << "Final Bal"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(12) << "Triggered"
              << std::setw(15) << "Stop Date" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (double thresh : dd_thresholds) {
        std::cout << "Testing " << thresh << "% DD stop..." << std::flush;
        EquityStopResult res = RunEquityStopTest(thresh);

        std::cout << "\r" << std::setw(11) << thresh << "%"
                  << std::setw(11) << "$" << res.final_balance
                  << std::setw(9) << res.return_x << "x"
                  << std::setw(9) << res.max_dd_pct << "%"
                  << std::setw(12) << (res.stop_triggered ? "YES" : "NO")
                  << std::setw(15) << (res.stop_triggered ? res.stop_date : "-") << std::endl;
    }

    //=========================================================================
    // Part 3: Profit Withdrawal
    //=========================================================================
    std::cout << "\n" << std::string(80, '-') << std::endl;
    std::cout << "PART 3: PROFIT WITHDRAWAL TEST" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    // Note: This is a simplified simulation - actual withdrawal would need engine support
    std::cout << "\nNote: Withdrawal simulation tracks profits but doesn't reduce margin exposure.\n" << std::endl;

    std::vector<std::pair<std::string, std::pair<double, int>>> withdrawal_strategies = {
        {"No Withdrawal", {0.0, 0}},
        {"Monthly 25%", {0.25, 21}},
        {"Monthly 50%", {0.50, 21}},
        {"Weekly 25%", {0.25, 5}},
        {"Quarterly 50%", {0.50, 63}}
    };

    std::cout << std::setw(18) << "Strategy"
              << std::setw(12) << "Final Bal"
              << std::setw(12) << "Withdrawn"
              << std::setw(12) << "Total Val"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Survived" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& ws : withdrawal_strategies) {
        std::cout << "Testing " << ws.first << "..." << std::flush;
        WithdrawalResult res = RunWithdrawalTest(ws.first, ws.second.first, ws.second.second);

        std::cout << "\r" << std::setw(18) << res.strategy
                  << std::setw(11) << "$" << res.final_balance
                  << std::setw(11) << "$" << res.total_withdrawn
                  << std::setw(11) << "$" << res.total_value
                  << std::setw(9) << res.max_dd_pct << "%"
                  << std::setw(10) << (res.survived ? "YES" : "NO") << std::endl;
    }

    //=========================================================================
    // Summary
    //=========================================================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "RISK MANAGEMENT SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "\n1. MONTE CARLO:" << std::endl;
    if (mc.ruin_probability < 5) {
        std::cout << "   [GOOD] Low risk of ruin (" << mc.ruin_probability << "%)" << std::endl;
    } else if (mc.ruin_probability < 20) {
        std::cout << "   [CAUTION] Moderate risk of ruin (" << mc.ruin_probability << "%)" << std::endl;
    } else {
        std::cout << "   [WARNING] High risk of ruin (" << mc.ruin_probability << "%) - reduce position size" << std::endl;
    }

    std::cout << "\n2. EQUITY STOP:" << std::endl;
    std::cout << "   Review the table above to choose appropriate DD threshold" << std::endl;
    std::cout << "   Lower threshold = more protection, but may cut winning trades" << std::endl;

    std::cout << "\n3. PROFIT WITHDRAWAL:" << std::endl;
    std::cout << "   Regular profit withdrawal reduces compounding but locks in gains" << std::endl;
    std::cout << "   Consider monthly 25-50% withdrawal for risk reduction" << std::endl;

    return 0;
}
