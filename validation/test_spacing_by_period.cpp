#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace backtest;

struct Period {
    std::string name;
    std::string start;
    std::string end;
};

void testPeriod(const Period& period, const std::vector<double>& spacings) {
    std::cout << "\n=== " << period.name << " (" << period.start << " - " << period.end << ") ===" << std::endl;
    std::cout << "Spacing   Return   Trades" << std::endl;
    std::cout << std::string(30, '-') << std::endl;

    double best_return = 0;
    double best_spacing = 0;

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
        config.start_date = period.start;
        config.end_date = period.end;
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

            std::cout << "$" << std::fixed << std::setprecision(2) << std::setw(5) << spacing
                      << "    " << std::setprecision(2) << std::setw(5) << ret << "x"
                      << "    " << std::setw(6) << res.total_trades
                      << std::endl;

            if (ret > best_return) {
                best_return = ret;
                best_spacing = spacing;
            }

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    std::cout << "Best: $" << std::setprecision(2) << best_spacing << " -> " << best_return << "x" << std::endl;
}

int main() {
    std::vector<double> spacings = {0.20, 0.25, 0.30, 0.40, 0.50, 1.00, 2.00, 5.00};

    std::vector<Period> periods = {
        {"Q1 2025", "2025.01.01", "2025.03.31"},
        {"Q2 2025", "2025.04.01", "2025.06.30"},
        {"Q3 2025", "2025.07.01", "2025.09.30"},
        {"Q4 2025", "2025.10.01", "2025.12.30"},
        {"Jan 2025", "2025.01.01", "2025.01.31"},
        {"Apr 2025", "2025.04.01", "2025.04.30"},
        {"Aug 2025", "2025.08.01", "2025.08.31"},
        {"Nov 2025", "2025.11.01", "2025.11.30"}
    };

    std::cout << "=== Spacing Sweep by Time Period ===" << std::endl;

    for (const auto& period : periods) {
        testPeriod(period, spacings);
    }

    return 0;
}
