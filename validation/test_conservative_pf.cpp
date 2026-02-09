#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

void TestStartDate(const std::string& start_date) {
    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
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
    config.start_date = start_date;
    config.end_date = "2025.12.29";
    config.tick_data_config = tick_config;

    StrategyCombinedJu::Config sc;
    sc.survive_pct = 13.0;
    sc.base_spacing = 1.5;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 100.0;
    sc.leverage = 500.0;
    sc.volatility_lookback_hours = 4.0;
    sc.typical_vol_pct = 0.55;
    sc.tp_mode = StrategyCombinedJu::LINEAR;
    sc.tp_sqrt_scale = 0.5;
    sc.tp_linear_scale = 0.3;
    sc.tp_min = 1.0;
    sc.enable_velocity_filter = true;
    sc.velocity_window = 10;
    sc.velocity_threshold_pct = 0.01;
    sc.sizing_mode = StrategyCombinedJu::UNIFORM;
    sc.force_min_volume_entry = false;

    StrategyCombinedJu strategy(sc);
    TickBasedEngine engine(config);

    engine.Run([&strategy](const Tick& t, TickBasedEngine& e) {
        strategy.OnTick(t, e);
    });

    auto results = engine.GetResults();

    double ret = results.final_balance / 10000.0;

    // Calculate profit factor from wins/losses
    double gross_profit = results.winning_trades * results.average_win;
    double gross_loss = results.losing_trades * std::abs(results.average_loss);
    double pf = (gross_loss > 0) ? (gross_profit / gross_loss) : 0.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Start: " << start_date << std::endl;
    std::cout << "  Return: " << ret << "x ($" << results.final_balance << ")" << std::endl;
    std::cout << "  MaxDD: " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "  Trades: " << results.total_trades
              << " (W:" << results.winning_trades << " L:" << results.losing_trades << ")" << std::endl;
    std::cout << "  Avg Win: $" << results.average_win
              << " | Avg Loss: $" << results.average_loss << std::endl;
    std::cout << "  Gross Profit: $" << gross_profit
              << " | Gross Loss: $" << gross_loss << std::endl;
    std::cout << "  Profit Factor: " << pf << std::endl;
    std::cout << "  Swap: $" << results.total_swap_charged << std::endl;
    std::cout << "  Win Rate: " << results.win_rate << "%" << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Conservative Preset Profit Factor Test ===" << std::endl;
    std::cout << "Config: survive=13%, spacing=$1.5, UNIFORM sizing, velocity ON" << std::endl;
    std::cout << std::endl;

    TestStartDate("2025.01.01");
    TestStartDate("2025.04.01");
    TestStartDate("2025.07.01");
    TestStartDate("2025.10.17");

    return 0;
}
