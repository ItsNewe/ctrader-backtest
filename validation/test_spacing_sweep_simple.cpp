#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== Spacing Sweep (FIXED mode) ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Spacing   Return   MaxDD%   Trades    Swap($)" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    std::vector<double> spacings = {0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.50, 0.75, 1.00};

    for (double spacing : spacings) {
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
        config.verbose = false;

        try {
            TickBasedEngine engine(config);
            
            FillUpOscillation strategy(
                13.0, spacing, 0.01, 10.0, 100.0, 500.0,
                FillUpOscillation::BASELINE,
                0.1, 30.0, 1.0
            );

            engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto res = engine.GetResults();
            double ret = res.final_balance / 10000.0;
            double peak = res.final_balance + res.max_drawdown;
            double dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;

            std::cout << "$" << std::fixed << std::setprecision(2) << std::setw(5) << spacing
                      << "    " << std::setprecision(2) << std::setw(5) << ret << "x"
                      << "    " << std::setprecision(0) << std::setw(3) << dd_pct << "%"
                      << "    " << std::setw(6) << res.total_trades
                      << "    " << std::setprecision(0) << std::setw(6) << res.total_swap_charged
                      << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "Error at $" << spacing << ": " << e.what() << std::endl;
        }
    }
    return 0;
}
