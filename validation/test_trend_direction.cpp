#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

void analyzeTrend(const std::string& file_path, const std::string& year) {
    TickDataConfig config;
    config.file_path = file_path;
    config.format = TickDataFormat::MT5_CSV;
    config.load_all_into_memory = false;

    TickDataManager manager(config);

    double first_price = 0, last_price = 0;
    double year_high = 0, year_low = 999999;
    
    Tick tick;
    while (manager.GetNextTick(tick)) {
        double mid = (tick.bid + tick.ask) / 2.0;
        if (first_price == 0) first_price = mid;
        last_price = mid;
        year_high = std::max(year_high, mid);
        year_low = std::min(year_low, mid);
    }

    double trend_pct = (last_price - first_price) / first_price * 100.0;
    double range_pct = (year_high - year_low) / first_price * 100.0;
    
    std::cout << "=== " << year << " ===" << std::endl;
    std::cout << "Start price: $" << std::fixed << std::setprecision(2) << first_price << std::endl;
    std::cout << "End price:   $" << last_price << std::endl;
    std::cout << "Year high:   $" << year_high << std::endl;
    std::cout << "Year low:    $" << year_low << std::endl;
    std::cout << "Net trend:   " << std::setprecision(1) << (trend_pct >= 0 ? "+" : "") << trend_pct << "%" << std::endl;
    std::cout << "Range:       " << range_pct << "%" << std::endl;
    std::cout << std::endl;
}

int main() {
    analyzeTrend("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv", "2024");
    analyzeTrend("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", "2025");
    return 0;
}
