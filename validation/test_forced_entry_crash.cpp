//+------------------------------------------------------------------+
//| Test start-date sensitivity around October 2025 crash            |
//| First entry is ALWAYS immediate - tests survival at different    |
//| start dates and survive% values                                  |
//+------------------------------------------------------------------+
#include "../include/tick_based_engine.h"
#include "../include/fill_up_oscillation.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>

using namespace backtest;

struct TestResult {
    std::string start_date;
    double survive_pct;
    double final_balance;
    double max_dd_pct;
    double return_mult;
    int trades;
    bool stopped_out;
};

void RunTest(const std::string& start_date, double survive_pct,
             std::vector<TestResult>& results) {

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
    config.start_date = start_date;
    config.end_date = "2025.12.29";
    config.tick_data_config = tick_config;

    TestResult result;
    result.start_date = start_date;
    result.survive_pct = survive_pct;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(survive_pct, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.max_dd_pct = res.max_drawdown_pct;
        result.return_mult = res.final_balance / 10000.0;
        result.trades = res.total_trades;
        result.stopped_out = (res.final_balance < 1000);  // Effective stop-out
    } catch (...) {
        result.final_balance = 0;
        result.max_dd_pct = 100;
        result.return_mult = 0;
        result.trades = 0;
        result.stopped_out = true;
    }

    results.push_back(result);
}

int main() {
    std::cout << "========================================================================\n";
    std::cout << "START-DATE SENSITIVITY ANALYSIS\n";
    std::cout << "Testing FillUpOscillation at different start dates around Oct 2025\n";
    std::cout << "========================================================================\n\n";

    std::vector<TestResult> results;

    // Test start dates
    std::vector<std::string> dates = {
        "2025.01.01",  // Full year baseline
        "2025.09.15",  // Before Oct rally
        "2025.10.01",  // Early October
        "2025.10.10",  // Mid October
        "2025.10.15",  // Near peak
        "2025.10.17",  // USER REPORTED DATE
        "2025.10.20",  // After peak
        "2025.10.25",  // Mid correction
        "2025.10.30",  // End October
        "2025.11.01",  // November start
        "2025.11.15",  // Mid November
    };

    // Test survive percentages
    std::vector<double> survives = {12.0, 13.0, 15.0, 18.0, 20.0};

    std::cout << "Running " << dates.size() * survives.size() << " tests...\n\n";

    for (const auto& date : dates) {
        for (double surv : survives) {
            std::cout << "\r  Testing " << date << " survive=" << surv << "%...   " << std::flush;
            RunTest(date, surv, results);
        }
    }

    // Print results grouped by date
    std::cout << "\n\n========================================================================\n";
    std::cout << "RESULTS BY START DATE\n";
    std::cout << "========================================================================\n\n";

    std::string current_date = "";
    for (const auto& r : results) {
        if (r.start_date != current_date) {
            if (!current_date.empty()) std::cout << "\n";
            current_date = r.start_date;
            std::cout << "Start: " << current_date << "\n";
            std::cout << std::string(70, '-') << "\n";
            std::cout << std::left << std::setw(12) << "Survive%"
                      << std::right << std::setw(12) << "Final$"
                      << std::setw(10) << "Return"
                      << std::setw(10) << "MaxDD%"
                      << std::setw(10) << "Trades"
                      << std::setw(10) << "Status"
                      << std::endl;
        }

        std::cout << std::left << std::setw(12) << std::fixed << std::setprecision(0) << r.survive_pct
                  << std::right << std::setprecision(0) << std::setw(12) << r.final_balance
                  << std::setprecision(2) << std::setw(10) << r.return_mult << "x"
                  << std::setprecision(1) << std::setw(9) << r.max_dd_pct << "%"
                  << std::setprecision(0) << std::setw(10) << r.trades
                  << std::setw(10) << (r.stopped_out ? "STOP-OUT" : "OK")
                  << std::endl;
    }

    // Find minimum survive% needed for each date
    std::cout << "\n\n========================================================================\n";
    std::cout << "MINIMUM SURVIVE% NEEDED TO AVOID STOP-OUT\n";
    std::cout << "========================================================================\n\n";

    std::map<std::string, double> min_survive;
    for (const auto& r : results) {
        if (!r.stopped_out) {
            if (min_survive.find(r.start_date) == min_survive.end() ||
                r.survive_pct < min_survive[r.start_date]) {
                min_survive[r.start_date] = r.survive_pct;
            }
        }
    }

    for (const auto& date : dates) {
        auto it = min_survive.find(date);
        if (it != min_survive.end()) {
            std::cout << date << ": " << std::setprecision(0) << it->second << "% minimum\n";
        } else {
            std::cout << date << ": >20% needed (all tested stop-out)\n";
        }
    }

    std::cout << "\n========================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "========================================================================\n";
    std::cout << "FillUpOscillation ALWAYS opens first position immediately at first tick.\n";
    std::cout << "Starting at a local high (before crash) means first entry is at the peak.\n";
    std::cout << "If price then drops more than survive%, margin call occurs.\n\n";
    std::cout << "MITIGATION OPTIONS:\n";
    std::cout << "1. Use higher survive% (18-20%) for crash resistance\n";
    std::cout << "2. Wait for first entry signal (price must drop from startup)\n";
    std::cout << "3. Start with reduced capital until position establishes\n";
    std::cout << "4. Add 'no entry if starting at ATH' filter\n";

    return 0;
}
