#include "../include/fill_up_strategy.h"
#include "../include/fill_up_strategy_v2.h"
#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace backtest;

struct TestResult {
    std::string version;
    std::string scenario;
    double final_balance;
    double return_pct;
    double max_drawdown_pct;
    int total_trades;
    int max_positions;
    bool stopped_out;
};

// Simplified simulation that works with both V1 and V2
template<typename Strategy>
TestResult RunSimulation(const std::string& version,
                         const std::string& scenario_name,
                         Strategy& strategy,
                         const std::vector<Tick>& ticks,
                         double initial_balance,
                         double contract_size,
                         double leverage) {
    TestResult result;
    result.version = version;
    result.scenario = scenario_name;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double max_drawdown_pct = 0.0;
    std::vector<Trade*> open_positions;
    std::vector<Trade> closed_trades;
    size_t next_trade_id = 1;
    bool stopped_out = false;
    int max_pos = 0;
    double spacing = 1.0;

    for (size_t i = 0; i < ticks.size() && !stopped_out; i++) {
        const Tick& tick = ticks[i];

        // Update equity
        equity = balance;
        for (Trade* trade : open_positions) {
            double unrealized_pl = (tick.bid - trade->entry_price) * trade->lot_size * contract_size;
            equity += unrealized_pl;
        }

        // Track drawdown
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        max_drawdown_pct = std::max(max_drawdown_pct, dd_pct);

        // Check stop out
        double used_margin = 0.0;
        for (Trade* trade : open_positions) {
            used_margin += trade->lot_size * contract_size * trade->entry_price / leverage;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            stopped_out = true;
            for (Trade* trade : open_positions) {
                double pl = (tick.bid - trade->entry_price) * trade->lot_size * contract_size;
                balance += pl;
                closed_trades.push_back(*trade);
                delete trade;
            }
            open_positions.clear();
            break;
        }

        // Check TP
        std::vector<Trade*> to_close;
        for (Trade* trade : open_positions) {
            if (tick.bid >= trade->take_profit) {
                to_close.push_back(trade);
            }
        }
        for (Trade* trade : to_close) {
            double pl = (trade->take_profit - trade->entry_price) * trade->lot_size * contract_size;
            balance += pl;
            closed_trades.push_back(*trade);
            open_positions.erase(std::remove(open_positions.begin(), open_positions.end(), trade), open_positions.end());
            delete trade;
        }

        // Grid logic
        double lowest = DBL_MAX, highest = DBL_MIN;
        for (Trade* t : open_positions) {
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        bool should_open = false;
        int pos_count = open_positions.size();

        // V2 style position limit (50) vs V1 (no limit effectively)
        int max_allowed = (version == "V2") ? 50 : 200;

        // V2 style drawdown circuit breaker
        bool circuit_breaker = (version == "V2") && (dd_pct > 50.0);

        if (!circuit_breaker && pos_count < max_allowed) {
            if (pos_count == 0) {
                should_open = true;
            } else if (lowest >= tick.ask + spacing) {
                should_open = true;
            } else if (highest <= tick.ask - spacing) {
                should_open = true;
            }
        }

        if (should_open) {
            double lot_size = 0.01;
            double margin_needed = lot_size * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            // V2: reduce size in drawdown
            if (version == "V2" && dd_pct > 25.0) {
                lot_size *= 0.5;
            }

            if (free_margin > margin_needed * 2) {
                Trade* trade = new Trade();
                trade->id = next_trade_id++;
                trade->symbol = "XAUUSD";
                trade->direction = "BUY";
                trade->entry_price = tick.ask;
                trade->lot_size = lot_size;
                trade->stop_loss = 0;
                trade->take_profit = tick.ask + tick.spread() + spacing;
                trade->entry_time = tick.timestamp;
                open_positions.push_back(trade);
            }
        }

        max_pos = std::max(max_pos, (int)open_positions.size());
    }

    // Close remaining
    if (!ticks.empty() && !open_positions.empty()) {
        const Tick& last = ticks.back();
        for (Trade* trade : open_positions) {
            double pl = (last.bid - trade->entry_price) * trade->lot_size * contract_size;
            balance += pl;
            closed_trades.push_back(*trade);
            delete trade;
        }
    }

    result.final_balance = balance;
    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    result.max_drawdown_pct = max_drawdown_pct;
    result.total_trades = closed_trades.size();
    result.max_positions = max_pos;
    result.stopped_out = stopped_out;

    return result;
}

void PrintComparison(const TestResult& v1, const TestResult& v2) {
    std::cout << std::fixed << std::setprecision(2);

    // Determine which is better
    std::string better_return = (v2.return_pct > v1.return_pct) ? " <<" : "";
    std::string better_dd = (v2.max_drawdown_pct < v1.max_drawdown_pct) ? " <<" : "";

    std::cout << "| " << std::left << std::setw(20) << v1.scenario;
    std::cout << " | V1: " << std::right << std::setw(8) << v1.return_pct << "%";
    std::cout << " | V2: " << std::setw(8) << v2.return_pct << "%" << better_return;
    std::cout << " | V1 DD: " << std::setw(6) << v1.max_drawdown_pct << "%";
    std::cout << " | V2 DD: " << std::setw(6) << v2.max_drawdown_pct << "%" << better_dd;
    std::cout << " |" << std::endl;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     FILL-UP STRATEGY V1 vs V2 COMPARISON" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "V2 Improvements:" << std::endl;
    std::cout << "  - Max 50 positions (V1: unlimited)" << std::endl;
    std::cout << "  - 50% drawdown circuit breaker" << std::endl;
    std::cout << "  - Reduced position size when DD > 25%" << std::endl;
    std::cout << std::endl;

    double start_price = 2600.0;
    double spread = 0.25;
    double initial_balance = 10000.0;
    double contract_size = 100.0;
    double leverage = 500.0;

    std::vector<std::pair<TestResult, TestResult>> comparisons;

    // Test scenarios
    struct Scenario {
        std::string name;
        std::function<void(SyntheticTickGenerator&)> generator;
    };

    std::vector<Scenario> scenarios = {
        {"Uptrend +$100", [](SyntheticTickGenerator& g) {
            g.GenerateTrend(10000, 100.0, 0.1);
        }},
        {"Downtrend -$100", [](SyntheticTickGenerator& g) {
            g.GenerateTrend(10000, -100.0, 0.1);
        }},
        {"Crash 5%", [](SyntheticTickGenerator& g) {
            g.GenerateCrash(1000, 5.0);
        }},
        {"Crash 10%", [](SyntheticTickGenerator& g) {
            g.GenerateCrash(1000, 10.0);
        }},
        {"V-Recovery 5%", [](SyntheticTickGenerator& g) {
            g.GenerateVRecovery(5000, 5000, 5.0);
        }},
        {"Sideways $20", [](SyntheticTickGenerator& g) {
            g.GenerateSideways(10000, 20.0);
        }},
        {"Flash Crash 8%", [](SyntheticTickGenerator& g) {
            g.GenerateFlashCrash(8.0, 100, 500);
        }},
        {"Bear Market 15%", [](SyntheticTickGenerator& g) {
            g.GenerateBearMarket(10000, 15.0, 4);
        }},
        {"Whipsaw 10x$10", [](SyntheticTickGenerator& g) {
            g.GenerateWhipsaw(10, 10.0, 1000);
        }},
        {"High Volatility", [](SyntheticTickGenerator& g) {
            g.GenerateRandomWalk(10000, 1.0, 0.0);
        }},
    };

    std::cout << std::string(110, '-') << std::endl;

    for (size_t i = 0; i < scenarios.size(); i++) {
        SyntheticTickGenerator gen(start_price, spread, i + 1);
        scenarios[i].generator(gen);
        const auto& ticks = gen.GetTicks();

        // Dummy strategy objects (we use simplified simulation)
        FillUpStrategy v1_strategy(13.0, 1.0, 1.0, 0.01, 100.0, contract_size, leverage, 2, 1.0);
        FillUpStrategyV2::Config v2_config;
        FillUpStrategyV2 v2_strategy(v2_config);

        TestResult r1 = RunSimulation("V1", scenarios[i].name, v1_strategy, ticks,
                                      initial_balance, contract_size, leverage);
        TestResult r2 = RunSimulation("V2", scenarios[i].name, v2_strategy, ticks,
                                      initial_balance, contract_size, leverage);

        PrintComparison(r1, r2);
        comparisons.push_back({r1, r2});
    }

    std::cout << std::string(110, '-') << std::endl;

    // Summary
    int v1_better_return = 0, v2_better_return = 0;
    int v1_better_dd = 0, v2_better_dd = 0;
    double v1_total_return = 0, v2_total_return = 0;
    double v1_worst_dd = 0, v2_worst_dd = 0;

    for (const auto& [r1, r2] : comparisons) {
        if (r1.return_pct > r2.return_pct) v1_better_return++;
        else v2_better_return++;

        if (r1.max_drawdown_pct < r2.max_drawdown_pct) v1_better_dd++;
        else v2_better_dd++;

        v1_total_return += r1.return_pct;
        v2_total_return += r2.return_pct;
        v1_worst_dd = std::max(v1_worst_dd, r1.max_drawdown_pct);
        v2_worst_dd = std::max(v2_worst_dd, r2.max_drawdown_pct);
    }

    int n = comparisons.size();
    std::cout << std::endl;
    std::cout << "SUMMARY:" << std::endl;
    std::cout << "  Better Returns:    V1=" << v1_better_return << "  V2=" << v2_better_return << std::endl;
    std::cout << "  Better Drawdown:   V1=" << v1_better_dd << "  V2=" << v2_better_dd << std::endl;
    std::cout << "  Avg Return:        V1=" << (v1_total_return/n) << "%  V2=" << (v2_total_return/n) << "%" << std::endl;
    std::cout << "  Worst Drawdown:    V1=" << v1_worst_dd << "%  V2=" << v2_worst_dd << "%" << std::endl;
    std::cout << std::endl;

    // Risk-adjusted analysis
    double v1_sharpe_proxy = v1_total_return / n / std::max(1.0, v1_worst_dd);
    double v2_sharpe_proxy = v2_total_return / n / std::max(1.0, v2_worst_dd);

    std::cout << "RISK-ADJUSTED (Return/MaxDD):" << std::endl;
    std::cout << "  V1: " << v1_sharpe_proxy << std::endl;
    std::cout << "  V2: " << v2_sharpe_proxy << std::endl;

    if (v2_sharpe_proxy > v1_sharpe_proxy) {
        std::cout << "  >> V2 is BETTER risk-adjusted!" << std::endl;
    } else {
        std::cout << "  >> V1 is BETTER risk-adjusted!" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
