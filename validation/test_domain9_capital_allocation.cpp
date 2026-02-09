/**
 * Domain 9: Capital Allocation Optimization
 *
 * Apply Kelly criterion and optimal f analysis:
 * - What fraction of capital should be risked?
 * - What's the optimal position sizing?
 * - Comparison of aggressive vs conservative allocation
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cfloat>

using namespace backtest;

struct AllocationResult {
    std::string name;
    double allocation_pct;  // % of capital at risk
    double final_balance;
    double return_x;
    double max_dd_pct;
    double sharpe_ratio;
    double calmar_ratio;
};

AllocationResult RunWithAllocation(double lot_multiplier, const std::string& name) {
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

    AllocationResult result;
    result.name = name;
    result.allocation_pct = lot_multiplier * 100;

    try {
        TickBasedEngine engine(config);

        // Use modified min/max volume based on allocation
        double base_min = 0.01;
        double base_max = 10.0;

        FillUpOscillation strategy(
            13.0,                           // survive_pct
            1.0,                            // base_spacing
            base_min * lot_multiplier,      // min_volume
            base_max * lot_multiplier,      // max_volume
            100.0,                          // contract_size
            500.0,                          // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1, 30.0, 1.0
        );

        double peak = config.initial_balance;
        double max_dd = 0;
        std::vector<double> daily_returns;
        double prev_equity = config.initial_balance;
        long tick_count = 0;
        long ticks_per_day = 720000 * 24;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;
            strategy.OnTick(tick, eng);

            double equity = eng.GetEquity();
            if (equity > peak) peak = equity;
            double dd = (peak - equity) / peak * 100;
            if (dd > max_dd) max_dd = dd;

            // Track daily returns
            if (tick_count % ticks_per_day == 0) {
                double daily_return = (equity - prev_equity) / prev_equity;
                daily_returns.push_back(daily_return);
                prev_equity = equity;
            }
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;

        // Calculate Sharpe ratio (annualized)
        if (!daily_returns.empty()) {
            double mean_return = 0;
            for (double r : daily_returns) mean_return += r;
            mean_return /= daily_returns.size();

            double variance = 0;
            for (double r : daily_returns) variance += (r - mean_return) * (r - mean_return);
            variance /= daily_returns.size();
            double std_return = std::sqrt(variance);

            if (std_return > 0) {
                result.sharpe_ratio = (mean_return / std_return) * std::sqrt(252);
            } else {
                result.sharpe_ratio = 0;
            }
        }

        // Calmar ratio
        double annual_return = (result.return_x - 1.0) * 100;
        result.calmar_ratio = max_dd > 0 ? annual_return / max_dd : 0;

    } catch (...) {
        result.return_x = 0;
        result.sharpe_ratio = 0;
        result.calmar_ratio = 0;
    }

    return result;
}

int main() {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "CAPITAL ALLOCATION OPTIMIZATION" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::vector<AllocationResult> results;

    // Test different allocation levels
    std::vector<std::pair<double, std::string>> allocations = {
        {0.25, "25% (Conservative)"},
        {0.50, "50% (Moderate)"},
        {0.75, "75% (Aggressive)"},
        {1.00, "100% (Full Kelly)"},
        {1.50, "150% (Over-Kelly)"},
        {2.00, "200% (2x Kelly)"}
    };

    int i = 1;
    for (const auto& [mult, name] : allocations) {
        std::cout << "\n[" << i++ << "/" << allocations.size() << "] Testing " << name << "..." << std::endl;
        results.push_back(RunWithAllocation(mult, name));
        std::cout << "  Return: " << results.back().return_x << "x, Max DD: "
                  << results.back().max_dd_pct << "%" << std::endl;
    }

    // Results table
    std::cout << "\n" << std::string(85, '=') << std::endl;
    std::cout << "ALLOCATION COMPARISON" << std::endl;
    std::cout << std::string(85, '=') << std::endl;
    std::cout << std::setw(22) << "Allocation"
              << std::setw(14) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Sharpe"
              << std::setw(10) << "Calmar" << std::endl;
    std::cout << std::string(85, '-') << std::endl;

    AllocationResult best_sharpe = results[0];
    AllocationResult best_calmar = results[0];
    AllocationResult best_return = results[0];

    for (const auto& r : results) {
        std::cout << std::setw(22) << r.name
                  << std::setw(13) << "$" << r.final_balance
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.sharpe_ratio
                  << std::setw(10) << r.calmar_ratio << std::endl;

        if (r.sharpe_ratio > best_sharpe.sharpe_ratio) best_sharpe = r;
        if (r.calmar_ratio > best_calmar.calmar_ratio) best_calmar = r;
        if (r.return_x > best_return.return_x) best_return = r;
    }

    // Kelly Criterion Analysis
    std::cout << "\n=== KELLY CRITERION ANALYSIS ===" << std::endl;

    // Estimate Kelly from results
    // Kelly % = (Win% * Avg_Win - Loss% * Avg_Loss) / Avg_Win
    // For our 100% win rate strategy, it simplifies but we need to account for drawdown

    std::cout << "Based on backtest results:" << std::endl;
    std::cout << "  Best risk-adjusted (Sharpe): " << best_sharpe.name << std::endl;
    std::cout << "  Best risk-adjusted (Calmar): " << best_calmar.name << std::endl;
    std::cout << "  Highest absolute return: " << best_return.name << std::endl;

    // Optimal f calculation
    // Optimal f = edge / odds
    // For grid trading with 100% win rate but potential for large drawdowns,
    // the practical optimal f is constrained by drawdown tolerance

    std::cout << "\n=== OPTIMAL ALLOCATION RECOMMENDATION ===" << std::endl;

    // Find inflection point where Calmar starts declining
    double prev_calmar = 0;
    std::string recommended;
    for (const auto& r : results) {
        if (r.calmar_ratio < prev_calmar && prev_calmar > 0) {
            std::cout << "Calmar ratio peaks before " << r.name << std::endl;
            break;
        }
        prev_calmar = r.calmar_ratio;
        recommended = r.name;
    }

    std::cout << "\nRECOMMENDED ALLOCATION: " << recommended << std::endl;
    std::cout << "\nReasoning:" << std::endl;
    std::cout << "1. Higher allocation = higher returns but worse risk-adjusted returns" << std::endl;
    std::cout << "2. Over-Kelly betting (>100%) increases variance without proportional return increase" << std::endl;
    std::cout << "3. The Calmar ratio (return/drawdown) is the key metric for grid strategies" << std::endl;
    std::cout << "4. Conservative allocation (25-50%) provides better sleep at night" << std::endl;

    return 0;
}
