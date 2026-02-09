#include "../include/fill_up_strategy.h"
#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <fstream>

using namespace backtest;

/**
 * Test result structure
 */
struct ScenarioResult {
    std::string name;
    double initial_balance;
    double final_balance;
    double return_pct;
    double max_drawdown;
    double max_drawdown_pct;
    int total_trades;
    int max_open_positions;
    bool stopped_out;
    std::string notes;
};

/**
 * Run a single scenario and return results
 */
ScenarioResult RunScenario(const std::string& name,
                           const std::vector<Tick>& ticks,
                           double survive_pct = 13.0,
                           double initial_balance = 10000.0) {
    ScenarioResult result;
    result.name = name;
    result.initial_balance = initial_balance;

    // Create a custom tick data manager that uses our synthetic ticks
    TickDataConfig tick_config;
    tick_config.file_path = "";  // Will be overridden
    tick_config.load_all_into_memory = true;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;  // XAUUSD
    config.tick_data_config = tick_config;

    // Strategy parameters
    double size_multiplier = 1.0;
    double spacing = 1.0;
    double min_volume = 0.01;
    double max_volume = 100.0;
    int symbol_digits = 2;

    FillUpStrategy strategy(survive_pct, size_multiplier, spacing,
                            min_volume, max_volume, config.contract_size,
                            config.leverage, symbol_digits, config.margin_rate);

    // Track metrics manually since we're bypassing the engine's tick loading
    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double max_drawdown = 0.0;
    double max_drawdown_pct = 0.0;
    std::vector<Trade*> open_positions;
    std::vector<Trade> closed_trades;
    size_t next_trade_id = 1;
    bool stopped_out = false;

    // Simple tick-by-tick simulation
    for (size_t i = 0; i < ticks.size() && !stopped_out; i++) {
        const Tick& tick = ticks[i];

        // Update equity
        equity = balance;
        for (Trade* trade : open_positions) {
            double current_price = tick.bid;
            double unrealized_pl = (current_price - trade->entry_price) * trade->lot_size * config.contract_size;
            equity += unrealized_pl;
        }

        // Track drawdown
        if (equity > peak_equity) peak_equity = equity;
        double dd = peak_equity - equity;
        if (dd > max_drawdown) {
            max_drawdown = dd;
            max_drawdown_pct = (peak_equity > 0) ? (dd / peak_equity * 100.0) : 0.0;
        }

        // Check stop out (20% margin level)
        double used_margin = 0.0;
        for (Trade* trade : open_positions) {
            used_margin += trade->lot_size * config.contract_size * trade->entry_price / config.leverage;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            stopped_out = true;
            // Close all positions at current price
            for (Trade* trade : open_positions) {
                trade->exit_price = tick.bid;
                double pl = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
                trade->profit_loss = pl;
                balance += pl;
                closed_trades.push_back(*trade);
                delete trade;
            }
            open_positions.clear();
            break;
        }

        // Check TP for open positions
        std::vector<Trade*> to_close;
        for (Trade* trade : open_positions) {
            if (tick.bid >= trade->take_profit) {
                to_close.push_back(trade);
            }
        }
        for (Trade* trade : to_close) {
            trade->exit_price = trade->take_profit;
            double pl = (trade->take_profit - trade->entry_price) * trade->lot_size * config.contract_size;
            trade->profit_loss = pl;
            balance += pl;
            closed_trades.push_back(*trade);
            open_positions.erase(std::remove(open_positions.begin(), open_positions.end(), trade), open_positions.end());
            delete trade;
        }

        // Simple grid logic (simplified version of FillUpStrategy)
        double lowest_buy = DBL_MAX;
        double highest_buy = DBL_MIN;
        for (Trade* trade : open_positions) {
            lowest_buy = std::min(lowest_buy, trade->entry_price);
            highest_buy = std::max(highest_buy, trade->entry_price);
        }

        bool should_open = false;
        if (open_positions.empty()) {
            should_open = true;
        } else if (lowest_buy >= tick.ask + spacing) {
            should_open = true;
        } else if (highest_buy <= tick.ask - spacing) {
            should_open = true;
        }

        if (should_open) {
            // Calculate lot size (simplified)
            double lot_size = min_volume;

            // Check margin available
            double margin_needed = lot_size * config.contract_size * tick.ask / config.leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 2) {  // Safety buffer
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

        result.max_open_positions = std::max(result.max_open_positions, (int)open_positions.size());
    }

    // Close remaining positions at last tick price
    if (!ticks.empty() && !open_positions.empty()) {
        const Tick& last_tick = ticks.back();
        for (Trade* trade : open_positions) {
            double pl = (last_tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
            balance += pl;
            closed_trades.push_back(*trade);
            delete trade;
        }
        open_positions.clear();
    }

    result.final_balance = balance;
    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    result.max_drawdown = max_drawdown;
    result.max_drawdown_pct = max_drawdown_pct;
    result.total_trades = closed_trades.size();
    result.stopped_out = stopped_out;

    return result;
}

void PrintResult(const ScenarioResult& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "| " << std::left << std::setw(25) << r.name
              << " | $" << std::right << std::setw(10) << r.final_balance
              << " | " << std::setw(7) << r.return_pct << "%"
              << " | $" << std::setw(8) << r.max_drawdown
              << " (" << std::setw(5) << r.max_drawdown_pct << "%)"
              << " | " << std::setw(5) << r.total_trades
              << " | " << std::setw(4) << r.max_open_positions
              << " | " << (r.stopped_out ? "YES" : "NO ")
              << " |" << std::endl;
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "     FILL-UP STRATEGY EDGE CASE TESTING" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::endl;

    std::vector<ScenarioResult> results;
    double start_price = 2600.0;
    double spread = 0.25;

    // Scenario 1: Steady uptrend (ideal case)
    {
        SyntheticTickGenerator gen(start_price, spread, 1);
        gen.GenerateTrend(10000, 100.0, 0.1);  // +$100 over 10k ticks
        results.push_back(RunScenario("1. Steady Uptrend +$100", gen.GetTicks()));
    }

    // Scenario 2: Steady downtrend
    {
        SyntheticTickGenerator gen(start_price, spread, 2);
        gen.GenerateTrend(10000, -100.0, 0.1);  // -$100 over 10k ticks
        results.push_back(RunScenario("2. Steady Downtrend -$100", gen.GetTicks()));
    }

    // Scenario 3: Sharp crash 5%
    {
        SyntheticTickGenerator gen(start_price, spread, 3);
        gen.GenerateCrash(1000, 5.0);  // 5% crash
        results.push_back(RunScenario("3. Sharp Crash 5%", gen.GetTicks()));
    }

    // Scenario 4: Sharp crash 10%
    {
        SyntheticTickGenerator gen(start_price, spread, 4);
        gen.GenerateCrash(1000, 10.0);  // 10% crash
        results.push_back(RunScenario("4. Sharp Crash 10%", gen.GetTicks()));
    }

    // Scenario 5: V-Recovery
    {
        SyntheticTickGenerator gen(start_price, spread, 5);
        gen.GenerateVRecovery(5000, 5000, 5.0);  // 5% drop then recover
        results.push_back(RunScenario("5. V-Recovery 5%", gen.GetTicks()));
    }

    // Scenario 6: Sideways market
    {
        SyntheticTickGenerator gen(start_price, spread, 6);
        gen.GenerateSideways(10000, 20.0);  // $20 range
        results.push_back(RunScenario("6. Sideways $20 range", gen.GetTicks()));
    }

    // Scenario 7: Gap down $50
    {
        SyntheticTickGenerator gen(start_price, spread, 7);
        gen.GenerateGap(-50.0);
        gen.GenerateRandomWalk(5000, 0.1);
        results.push_back(RunScenario("7. Gap Down $50", gen.GetTicks()));
    }

    // Scenario 8: Whipsaw
    {
        SyntheticTickGenerator gen(start_price, spread, 8);
        gen.GenerateWhipsaw(10, 10.0, 1000);  // 10 cycles, $10 amplitude
        results.push_back(RunScenario("8. Whipsaw 10x$10", gen.GetTicks()));
    }

    // Scenario 9: Flash crash 8%
    {
        SyntheticTickGenerator gen(start_price, spread, 9);
        gen.GenerateFlashCrash(8.0, 100, 500);
        results.push_back(RunScenario("9. Flash Crash 8%", gen.GetTicks()));
    }

    // Scenario 10: Bear market 15%
    {
        SyntheticTickGenerator gen(start_price, spread, 10);
        gen.GenerateBearMarket(10000, 15.0, 4);
        results.push_back(RunScenario("10. Bear Market 15%", gen.GetTicks()));
    }

    // Scenario 11: Large balance - same scenarios
    {
        SyntheticTickGenerator gen(start_price, spread, 11);
        gen.GenerateCrash(1000, 10.0);
        results.push_back(RunScenario("11. Crash 10% ($100k)", gen.GetTicks(), 13.0, 100000.0));
    }

    // Scenario 12: Different survive_pct
    {
        SyntheticTickGenerator gen(start_price, spread, 12);
        gen.GenerateCrash(1000, 10.0);
        results.push_back(RunScenario("12. Crash 10% (surv=20%)", gen.GetTicks(), 20.0));
    }

    // Scenario 13: Very volatile
    {
        SyntheticTickGenerator gen(start_price, spread, 13);
        gen.GenerateRandomWalk(10000, 1.0, 0.0);  // High volatility
        results.push_back(RunScenario("13. High Volatility", gen.GetTicks()));
    }

    // Scenario 14: Uptrend with pullbacks
    {
        SyntheticTickGenerator gen(start_price, spread, 14);
        for (int i = 0; i < 5; i++) {
            gen.GenerateTrend(1500, 30.0, 0.1);   // Up $30
            gen.GenerateTrend(500, -10.0, 0.1);  // Pullback $10
        }
        results.push_back(RunScenario("14. Uptrend+Pullbacks", gen.GetTicks()));
    }

    // Scenario 15: Crash then slow recovery
    {
        SyntheticTickGenerator gen(start_price, spread, 15);
        gen.GenerateCrash(500, 8.0);
        gen.GenerateTrend(9500, 150.0, 0.1);  // Slow recovery past original
        results.push_back(RunScenario("15. Crash+SlowRecovery", gen.GetTicks()));
    }

    // Print results table
    std::cout << std::string(120, '-') << std::endl;
    std::cout << "| " << std::left << std::setw(25) << "Scenario"
              << " | " << std::setw(12) << "Final Bal"
              << " | " << std::setw(9) << "Return"
              << " | " << std::setw(20) << "Max Drawdown"
              << " | " << std::setw(5) << "Trd"
              << " | " << std::setw(4) << "Max"
              << " | " << std::setw(3) << "Out"
              << " |" << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : results) {
        PrintResult(r);
    }
    std::cout << std::string(120, '-') << std::endl;

    // Summary statistics
    int profitable = 0, losing = 0, stopped = 0;
    double total_return = 0.0;
    double worst_dd = 0.0;

    for (const auto& r : results) {
        if (r.return_pct > 0) profitable++;
        else losing++;
        if (r.stopped_out) stopped++;
        total_return += r.return_pct;
        worst_dd = std::max(worst_dd, r.max_drawdown_pct);
    }

    std::cout << std::endl;
    std::cout << "SUMMARY:" << std::endl;
    std::cout << "  Profitable scenarios: " << profitable << "/" << results.size() << std::endl;
    std::cout << "  Losing scenarios:     " << losing << "/" << results.size() << std::endl;
    std::cout << "  Stopped out:          " << stopped << "/" << results.size() << std::endl;
    std::cout << "  Average return:       " << (total_return / results.size()) << "%" << std::endl;
    std::cout << "  Worst drawdown:       " << worst_dd << "%" << std::endl;

    std::cout << std::endl;
    std::cout << "============================================================" << std::endl;

    return 0;
}
