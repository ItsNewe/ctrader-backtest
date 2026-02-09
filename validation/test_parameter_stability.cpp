/**
 * Parameter Stability Analysis
 *
 * Tests FillUpOscillation across parameter variations to ensure
 * we're not at a fragile local optimum.
 *
 * Parameters tested:
 * - survive_pct: 10%, 13%, 15%, 18%, 20%
 * - base_spacing: $1.0, $1.5, $2.0, $2.5
 * - volatility_lookback: 1h, 2h, 4h, 8h
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

using namespace backtest;

struct ParamConfig {
    double survive_pct;
    double base_spacing;
    double volatility_lookback;
};

struct ParamResult {
    ParamConfig config;
    double return_x;
    double max_dd_pct;
    int total_trades;
    bool stopped_out;
};

ParamResult RunConfig(const ParamConfig& cfg) {
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

    ParamResult result;
    result.config = cfg;
    result.stopped_out = false;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(
            cfg.survive_pct,
            cfg.base_spacing,
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,    // antifragile_scale
            30.0,   // velocity_threshold
            cfg.volatility_lookback
        );

        double peak = config.initial_balance;
        double max_dd = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;
        });

        auto res = engine.GetResults();
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;

        if (res.final_balance < config.initial_balance * 0.1) {
            result.stopped_out = true;
        }

    } catch (const std::exception& e) {
        result.return_x = 0;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "PARAMETER STABILITY ANALYSIS" << std::endl;
    std::cout << "FillUpOscillation ADAPTIVE_SPACING Mode" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    // Define parameter ranges
    std::vector<double> survive_pcts = {10.0, 13.0, 15.0, 18.0, 20.0};
    std::vector<double> spacings = {1.0, 1.5, 2.0, 2.5};
    std::vector<double> lookbacks = {1.0, 2.0, 4.0, 8.0};

    std::vector<ParamResult> all_results;

    int total_configs = survive_pcts.size() * spacings.size() * lookbacks.size();
    int current = 0;

    std::cout << "\nTesting " << total_configs << " parameter combinations...\n" << std::endl;

    // Test all combinations
    for (double survive : survive_pcts) {
        for (double spacing : spacings) {
            for (double lookback : lookbacks) {
                current++;
                ParamConfig cfg = {survive, spacing, lookback};

                std::cout << "[" << current << "/" << total_configs << "] "
                          << "survive=" << survive << "%, spacing=$" << spacing
                          << ", lookback=" << lookback << "h ... " << std::flush;

                ParamResult result = RunConfig(cfg);
                all_results.push_back(result);

                std::cout << result.return_x << "x"
                          << (result.stopped_out ? " STOPPED" : "") << std::endl;
            }
        }
    }

    // Sort by return
    std::sort(all_results.begin(), all_results.end(),
              [](const ParamResult& a, const ParamResult& b) {
                  if (a.stopped_out != b.stopped_out) return !a.stopped_out;
                  return a.return_x > b.return_x;
              });

    // Print top 10 results
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "TOP 10 CONFIGURATIONS (by return)" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    std::cout << std::setw(10) << "Survive"
              << std::setw(10) << "Spacing"
              << std::setw(10) << "Lookback"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Risk-Adj" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (int i = 0; i < std::min(10, (int)all_results.size()); i++) {
        const auto& r = all_results[i];
        double risk_adj = r.max_dd_pct > 0 ? r.return_x / (r.max_dd_pct / 100.0) : 0;
        std::cout << std::setw(9) << r.config.survive_pct << "%"
                  << std::setw(9) << "$" << r.config.base_spacing
                  << std::setw(9) << r.config.volatility_lookback << "h"
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << risk_adj << std::endl;
    }

    // Analyze stability around baseline (13%, $1.5, 4h)
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "STABILITY AROUND BASELINE (survive=13%, spacing=$1.5, lookback=4h)" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    // Find baseline
    ParamResult baseline;
    for (const auto& r : all_results) {
        if (r.config.survive_pct == 13.0 &&
            r.config.base_spacing == 1.5 &&
            r.config.volatility_lookback == 4.0) {
            baseline = r;
            break;
        }
    }

    std::cout << "\nBaseline: " << baseline.return_x << "x, DD=" << baseline.max_dd_pct << "%" << std::endl;

    // Find neighbors
    std::cout << "\nNeighboring configurations:" << std::endl;
    for (const auto& r : all_results) {
        bool is_neighbor = false;
        int diffs = 0;

        if (r.config.survive_pct != 13.0) diffs++;
        if (r.config.base_spacing != 1.5) diffs++;
        if (r.config.volatility_lookback != 4.0) diffs++;

        if (diffs == 1) {
            double pct_diff = ((r.return_x - baseline.return_x) / baseline.return_x) * 100;
            std::cout << "  survive=" << r.config.survive_pct << "%, "
                      << "spacing=$" << r.config.base_spacing << ", "
                      << "lookback=" << r.config.volatility_lookback << "h -> "
                      << r.return_x << "x (" << (pct_diff >= 0 ? "+" : "") << pct_diff << "%)"
                      << std::endl;
        }
    }

    // Calculate variance across all configs
    double sum_return = 0, sum_return_sq = 0;
    int valid_count = 0;
    int stopped_count = 0;

    for (const auto& r : all_results) {
        if (!r.stopped_out) {
            sum_return += r.return_x;
            sum_return_sq += r.return_x * r.return_x;
            valid_count++;
        } else {
            stopped_count++;
        }
    }

    double mean_return = sum_return / valid_count;
    double variance = (sum_return_sq / valid_count) - (mean_return * mean_return);
    double std_dev = std::sqrt(variance);

    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "OVERALL STATISTICS" << std::endl;
    std::cout << std::string(90, '=') << std::endl;
    std::cout << "Total configurations: " << total_configs << std::endl;
    std::cout << "Survived:            " << valid_count << " (" << (valid_count * 100.0 / total_configs) << "%)" << std::endl;
    std::cout << "Stopped out:         " << stopped_count << " (" << (stopped_count * 100.0 / total_configs) << "%)" << std::endl;
    std::cout << "Mean return:         " << mean_return << "x" << std::endl;
    std::cout << "Std deviation:       " << std_dev << std::endl;
    std::cout << "Coefficient of var:  " << (std_dev / mean_return * 100) << "%" << std::endl;

    std::cout << "\n" << std::string(90, '-') << std::endl;

    // Verdict
    double survival_rate = valid_count * 100.0 / total_configs;
    double cv = std_dev / mean_return * 100;

    if (survival_rate < 50) {
        std::cout << "VERDICT: FRAGILE - Less than 50% of configurations survive" << std::endl;
    } else if (cv > 50) {
        std::cout << "VERDICT: UNSTABLE - High variance across parameters (CV=" << cv << "%)" << std::endl;
    } else if (baseline.return_x < mean_return * 0.7) {
        std::cout << "VERDICT: SUBOPTIMAL - Baseline is below average performance" << std::endl;
    } else {
        std::cout << "VERDICT: STABLE - Strategy is robust across parameter variations" << std::endl;
        std::cout << "Survival rate: " << survival_rate << "%, CV: " << cv << "%" << std::endl;
    }

    return 0;
}
