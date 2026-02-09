/**
 * Scaled Extraction Model Test
 *
 * Key insight: Minimum capital scales with gold price!
 * At $2000 gold, 10% drop = $200. At $4000 gold, 10% drop = $400.
 *
 * Model:
 * - Reference price = $2,600 (Jan 2024 baseline)
 * - Reference capital = $10,000
 * - Min capital at price P = $10,000 × (P / $2,600)
 * - Extract when equity > min_capital
 * - Redeposit at restart = min_capital at that price
 *
 * Uses V4 Aggressive preset: pct_spacing=true, BaseSpacingPct=0.05
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <algorithm>

using namespace backtest;

std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;

const double REFERENCE_PRICE = 2600.0;   // Jan 2024 gold price baseline
const double REFERENCE_CAPITAL = 10000.0;

// Calculate minimum capital needed at a given price
double MinCapitalAtPrice(double price) {
    return REFERENCE_CAPITAL * (price / REFERENCE_PRICE);
}

struct ScaledResult {
    double survive_pct;
    double extracted_2024;
    double deposited_2024;
    int extractions_2024;
    int stop_outs_2024;
    double extracted_2025;
    double deposited_2025;
    int extractions_2025;
    int stop_outs_2025;
    double net_profit;  // extracted - deposited
};

std::mutex g_mutex;
std::vector<ScaledResult> g_results;
std::atomic<int> g_completed{0};

TickBacktestConfig GetConfig(double initial_balance) {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    return config;
}

struct YearResult {
    double total_extracted;
    double total_deposited;
    int extraction_count;
    int stop_out_count;
};

YearResult RunYearScaled(const std::vector<Tick>& ticks, double survive_pct) {
    YearResult yr = {0, 0, 0, 0};

    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.pct_spacing = true;
    adaptive_cfg.typical_vol_pct = 0.55;
    adaptive_cfg.min_spacing_mult = 0.5;
    adaptive_cfg.max_spacing_mult = 3.0;
    adaptive_cfg.min_spacing_abs = 0.005;
    adaptive_cfg.max_spacing_abs = 1.0;
    adaptive_cfg.spacing_change_threshold = 0.01;

    if (ticks.empty()) return yr;

    // Initial deposit based on first price
    double start_price = ticks[0].bid;
    double initial_capital = MinCapitalAtPrice(start_price);
    yr.total_deposited = initial_capital;

    size_t tick_idx = 0;
    const int MAX_RESTARTS = 200;

    while (tick_idx < ticks.size() && yr.stop_out_count < MAX_RESTARTS) {
        double current_price = ticks[tick_idx].bid;
        double min_capital = MinCapitalAtPrice(current_price);

        auto config = GetConfig(min_capital);
        TickBasedEngine engine(config);
        FillUpOscillation strategy(survive_pct, 0.05, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

        bool stopped = false;

        for (; tick_idx < ticks.size(); tick_idx++) {
            const Tick& tick = ticks[tick_idx];
            strategy.OnTick(tick, engine);

            double equity = engine.GetEquity();
            double current_min = MinCapitalAtPrice(tick.bid);
            auto positions = engine.GetOpenPositions();

            // Extract when equity > min_capital and no positions
            if (positions.empty() && equity > current_min + 10.0) {
                double to_extract = equity - current_min;
                yr.total_extracted += to_extract;
                yr.extraction_count++;
            }

            // Check stop-out
            if (engine.GetResults().stop_out_occurred) {
                stopped = true;
                yr.stop_out_count++;

                // Need to redeposit at current price level
                tick_idx++;
                if (tick_idx < ticks.size()) {
                    double restart_price = ticks[tick_idx].bid;
                    double restart_capital = MinCapitalAtPrice(restart_price);
                    yr.total_deposited += restart_capital;
                }
                break;
            }
        }

        if (!stopped) {
            // Year finished - extract remaining
            double final_equity = engine.GetBalance();
            double final_min = MinCapitalAtPrice(ticks.back().bid);
            if (final_equity > final_min) {
                yr.total_extracted += (final_equity - final_min);
                yr.extraction_count++;
            }
            break;
        }
    }

    return yr;
}

ScaledResult RunTest(double survive_pct) {
    ScaledResult r;
    r.survive_pct = survive_pct;

    YearResult yr2024 = RunYearScaled(g_ticks_2024, survive_pct);
    r.extracted_2024 = yr2024.total_extracted;
    r.deposited_2024 = yr2024.total_deposited;
    r.extractions_2024 = yr2024.extraction_count;
    r.stop_outs_2024 = yr2024.stop_out_count;

    YearResult yr2025 = RunYearScaled(g_ticks_2025, survive_pct);
    r.extracted_2025 = yr2025.total_extracted;
    r.deposited_2025 = yr2025.total_deposited;
    r.extractions_2025 = yr2025.extraction_count;
    r.stop_outs_2025 = yr2025.stop_out_count;

    r.net_profit = (r.extracted_2024 + r.extracted_2025) - (r.deposited_2024 + r.deposited_2025);

    return r;
}

void Worker(std::queue<double>& work_queue, int total) {
    while (true) {
        double survive_pct;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (work_queue.empty()) return;
            survive_pct = work_queue.front();
            work_queue.pop();
        }

        ScaledResult r = RunTest(survive_pct);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_results.push_back(r);
            g_completed++;

            std::cout << "[" << g_completed << "/" << total << "] "
                      << "survive=" << std::fixed << std::setprecision(0) << r.survive_pct << "%"
                      << " | net=$" << (int)r.net_profit
                      << " | SOs=" << (r.stop_outs_2024 + r.stop_outs_2025) << "\n";
        }
    }
}

void LoadTickData(const std::string& path, std::vector<Tick>& ticks) {
    std::cout << "Loading: " << path << "... " << std::flush;

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string line;
    std::getline(file, line);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string token;

        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, token, '\t'); tick.bid = std::stod(token);
        std::getline(ss, token, '\t'); tick.ask = std::stod(token);

        ticks.push_back(tick);
    }

    std::cout << ticks.size() << " ticks\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "SCALED EXTRACTION MODEL (V4 Aggressive: BaseSpacingPct=0.05)\n";
    std::cout << "Min capital scales with gold price: $10k at $2,600 reference\n";
    std::cout << "====================================================================\n\n";

    try {
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv", g_ticks_2024);
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", g_ticks_2025);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    // Show price scaling
    std::cout << "\nMin capital scaling:\n";
    std::cout << "  At $2,600: $" << (int)MinCapitalAtPrice(2600) << "\n";
    std::cout << "  At $3,000: $" << (int)MinCapitalAtPrice(3000) << "\n";
    std::cout << "  At $3,500: $" << (int)MinCapitalAtPrice(3500) << "\n";
    std::cout << "  At $4,000: $" << (int)MinCapitalAtPrice(4000) << "\n";
    std::cout << "  At $4,500: $" << (int)MinCapitalAtPrice(4500) << "\n";

    std::vector<double> survive_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

    std::queue<double> work_queue;
    for (double s : survive_values) {
        work_queue.push(s);
    }
    int total = (int)survive_values.size();

    std::cout << "\nRunning " << total << " configurations...\n\n";

    auto start = std::chrono::high_resolution_clock::now();

    unsigned int num_threads = std::min((unsigned int)total, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker, std::ref(work_queue), total);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    std::sort(g_results.begin(), g_results.end(), [](const ScaledResult& a, const ScaledResult& b) {
        return a.survive_pct < b.survive_pct;
    });

    // Results table
    std::cout << "\n====================================================================\n";
    std::cout << "RESULTS: SCALED EXTRACTION MODEL\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left
              << std::setw(10) << "Survive"
              << std::setw(14) << "Deposit 24"
              << std::setw(14) << "Extract 24"
              << std::setw(8) << "SO"
              << std::setw(14) << "Deposit 25"
              << std::setw(14) << "Extract 25"
              << std::setw(8) << "SO"
              << std::setw(16) << "NET PROFIT"
              << "\n";
    std::cout << std::string(98, '-') << "\n";

    double best_net = -999999;
    int best_survive = 0;

    for (const auto& r : g_results) {
        std::cout << std::setw(10) << (std::to_string((int)r.survive_pct) + "%")
                  << std::setw(14) << ("$" + std::to_string((int)r.deposited_2024))
                  << std::setw(14) << ("$" + std::to_string((int)r.extracted_2024))
                  << std::setw(8) << r.stop_outs_2024
                  << std::setw(14) << ("$" + std::to_string((int)r.deposited_2025))
                  << std::setw(14) << ("$" + std::to_string((int)r.extracted_2025))
                  << std::setw(8) << r.stop_outs_2025
                  << std::setw(16) << ("$" + std::to_string((int)r.net_profit))
                  << "\n";

        if (r.net_profit > best_net) {
            best_net = r.net_profit;
            best_survive = (int)r.survive_pct;
        }
    }

    std::cout << "\n====================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "====================================================================\n";

    std::cout << "\nBest net profit: survive=" << best_survive << "% -> $" << (int)best_net << "\n";

    // ROI calculation
    std::cout << "\nROI (net profit / total deposited):\n";
    for (const auto& r : g_results) {
        double total_dep = r.deposited_2024 + r.deposited_2025;
        double roi = (r.net_profit / total_dep) * 100;
        std::cout << "  survive=" << std::setw(2) << (int)r.survive_pct << "%: "
                  << "dep=$" << std::setw(6) << (int)total_dep
                  << " -> net=$" << std::setw(8) << (int)r.net_profit
                  << " (ROI=" << std::setw(5) << std::setprecision(0) << roi << "%)\n";
    }

    std::cout << "\nCompleted in " << std::setprecision(1) << elapsed << " seconds\n";

    return 0;
}
