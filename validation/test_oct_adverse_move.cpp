#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <cfloat>

using namespace backtest;

int main() {
    std::cout << "=== October 2025 Adverse Move Analysis ===" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;  // Stream mode

    TickDataManager manager(tick_config);

    double max_drawdown_pct = 0.0;
    double peak_at_max_dd = 0.0;
    double trough_at_max_dd = 0.0;
    std::string peak_time;
    std::string trough_time;

    // Track from Oct 17 onwards
    Tick tick;
    bool started = false;
    double local_peak = 0.0;
    std::string local_peak_time;

    long tick_count = 0;

    while (manager.GetNextTick(tick)) {
        if (tick.timestamp < "2025.10.17") continue;

        tick_count++;
        if (tick_count % 1000000 == 0) {
            std::cout << "Processing tick " << tick_count << "..." << std::endl;
        }

        if (!started) {
            local_peak = tick.bid;
            local_peak_time = tick.timestamp;
            started = true;
        }

        // Update local peak
        if (tick.bid > local_peak) {
            local_peak = tick.bid;
            local_peak_time = tick.timestamp;
        }

        // Calculate drawdown from local peak
        double dd_pct = (local_peak - tick.bid) / local_peak * 100.0;

        if (dd_pct > max_drawdown_pct) {
            max_drawdown_pct = dd_pct;
            peak_at_max_dd = local_peak;
            trough_at_max_dd = tick.bid;
            peak_time = local_peak_time;
            trough_time = tick.timestamp;
        }
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::endl;
    std::cout << "Maximum adverse move from Oct 17 onwards:" << std::endl;
    std::cout << "  Peak: $" << peak_at_max_dd << " at " << peak_time << std::endl;
    std::cout << "  Trough: $" << trough_at_max_dd << " at " << trough_time << std::endl;
    std::cout << "  Drop: $" << (peak_at_max_dd - trough_at_max_dd) << std::endl;
    std::cout << "  Drawdown: " << max_drawdown_pct << "%" << std::endl;
    std::cout << std::endl;

    // Analysis
    std::cout << "=== ANALYSIS ===" << std::endl;
    std::cout << "survive_pct needed to survive this move: >" << max_drawdown_pct << "%" << std::endl;
    std::cout << std::endl;

    if (max_drawdown_pct > 12.0) {
        std::cout << "CONCLUSION: survive=12% CANNOT survive Oct-Dec 2025 from fresh start" << std::endl;
        std::cout << "  survive=12% handles moves up to ~12%, but max move was "
                  << max_drawdown_pct << "%" << std::endl;
    }

    return 0;
}
