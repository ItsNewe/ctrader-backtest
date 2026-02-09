#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

// Debug version of FillUpStrategy with detailed logging
class DebugFillUpStrategy : public FillUpStrategy {
public:
    using FillUpStrategy::FillUpStrategy;

    int sizing_call_count = 0;

    void OnTickDebug(const Tick& tick, TickBasedEngine& engine) {
        OnTick(tick, engine);

        // Log every 1000th sizing calculation
        if (sizing_call_count > 0 && sizing_call_count % 1000 == 0) {
            std::cout << "\n=== Sizing Debug (call #" << sizing_call_count << ") ===" << std::endl;
            std::cout << "Equity: $" << engine.GetEquity() << std::endl;
            std::cout << "Balance: $" << engine.GetBalance() << std::endl;
            std::cout << "Open Positions: " << engine.GetOpenPositions().size() << std::endl;
            std::cout << "Last Trade Size: " << GetMaxTradeSize() << " lots" << std::endl;
        }
    }
};

int main() {
    std::cout << "=== Fill-Up Position Sizing Debug Test ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Configure for first 100K ticks only
    TickDataConfig tick_config;
    tick_config.file_path = "XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.account_currency = "USD";
    config.commission_per_lot = 0.0;
    config.slippage_pips = 0.0;
    config.use_bid_ask_spread = true;
    config.tick_data_config = tick_config;

    // Strategy parameters
    double survive = 13.0;
    double size = 1.0;
    double spacing = 1.0;
    double min_volume = 0.01;
    double max_volume = 100.0;
    double contract_size = 100.0;
    double leverage = 500.0;
    int symbol_digits = 2;
    double margin_rate = 1.0;

    try {
        TickBasedEngine engine(config);
        Debug FillUpStrategy strategy(survive, size, spacing, min_volume, max_volume,
                                       contract_size, leverage, symbol_digits, margin_rate);

        std::cout << "\nProcessing first 100,000 ticks with debug output..." << std::endl;

        int tick_count = 0;
        int max_ticks = 100000;

        engine.Run([&strategy, &tick_count, max_ticks](const Tick& tick, TickBasedEngine& engine) {
            if (tick_count >= max_ticks) return;

            strategy.OnTickDebug(tick, engine);
            tick_count++;

            if (tick_count % 10000 == 0) {
                std::cout << "\nTick " << tick_count << ": "
                          << "Equity=$" << engine.GetEquity()
                          << ", MaxLotSize=" << strategy.GetMaxTradeSize()
                          << std::endl;
            }
        });

        auto results = engine.GetResults();
        std::cout << "\n=== Results After 100K Ticks ===" << std::endl;
        std::cout << "Final Balance: $" << results.final_balance << std::endl;
        std::cout << "Max Trade Size: " << strategy.GetMaxTradeSize() << " lots" << std::endl;
        std::cout << "Total Trades: " << results.total_trades << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
