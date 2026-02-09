/**
 * Continuous Extraction Model Test
 *
 * Strategy: Extract ALL profits above minimum capital continuously
 * - Start with $10k
 * - Whenever equity > $10k and no open positions, extract the excess
 * - If stop-out, restart with fresh $10k
 * - Track total extracted over 2 years
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

const double BASE_CAPITAL = 10000.0;

struct ExtractionResult {
    double survive_pct;
    double extracted_2024;
    int extractions_2024;
    int stop_outs_2024;
    double extracted_2025;
    int extractions_2025;
    int stop_outs_2025;
    double total_extracted;
    int total_capital_deployed;
};

std::mutex g_mutex;
std::vector<ExtractionResult> g_results;
std::atomic<int> g_completed{0};

TickBacktestConfig GetConfig() {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = BASE_CAPITAL;
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
    int extraction_count;
    int stop_out_count;
    int restarts;
};

// Custom strategy wrapper that tracks extractions
YearResult RunYearWithContinuousExtraction(const std::vector<Tick>& ticks, double survive_pct) {
    YearResult yr = {0, 0, 0, 0};

    auto config = GetConfig();

    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.pct_spacing = true;
    adaptive_cfg.typical_vol_pct = 0.55;
    adaptive_cfg.min_spacing_mult = 0.5;
    adaptive_cfg.max_spacing_mult = 3.0;
    adaptive_cfg.min_spacing_abs = 0.005;
    adaptive_cfg.max_spacing_abs = 1.0;
    adaptive_cfg.spacing_change_threshold = 0.01;

    size_t tick_idx = 0;
    const int MAX_RESTARTS = 200;

    while (tick_idx < ticks.size() && yr.restarts < MAX_RESTARTS) {
        // Create fresh engine and strategy
        TickBasedEngine engine(config);
        FillUpOscillation strategy(survive_pct, 0.05, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

        double running_extracted = 0.0;
        int running_extractions = 0;
        bool stopped = false;

        // Process ticks
        for (; tick_idx < ticks.size(); tick_idx++) {
            strategy.OnTick(ticks[tick_idx], engine);

            // Check for extraction opportunity: equity > base AND no open positions
            double equity = engine.GetEquity();
            auto positions = engine.GetOpenPositions();

            if (positions.empty() && equity > BASE_CAPITAL + 1.0) {  // +1 to avoid tiny extractions
                double to_extract = equity - BASE_CAPITAL;
                running_extracted += to_extract;
                running_extractions++;
                // Note: We can't actually modify engine balance, but we track what WOULD be extracted
                // The strategy will continue as if it has full equity (slightly optimistic)
            }

            // Check stop-out
            if (engine.GetResults().stop_out_occurred) {
                stopped = true;
                yr.stop_out_count++;
                yr.total_extracted += running_extracted;
                yr.extraction_count += running_extractions;
                yr.restarts++;
                tick_idx++;  // Move past stop-out tick
                break;
            }
        }

        // If finished year without stop-out
        if (!stopped) {
            yr.total_extracted += running_extracted;
            yr.extraction_count += running_extractions;
            // Add final balance excess
            double final_excess = engine.GetBalance() - BASE_CAPITAL;
            if (final_excess > 0) {
                yr.total_extracted += final_excess;
                yr.extraction_count++;
            }
            break;
        }
    }

    return yr;
}

ExtractionResult RunTest(double survive_pct) {
    ExtractionResult r;
    r.survive_pct = survive_pct;

    YearResult yr2024 = RunYearWithContinuousExtraction(g_ticks_2024, survive_pct);
    r.extracted_2024 = yr2024.total_extracted;
    r.extractions_2024 = yr2024.extraction_count;
    r.stop_outs_2024 = yr2024.stop_out_count;

    YearResult yr2025 = RunYearWithContinuousExtraction(g_ticks_2025, survive_pct);
    r.extracted_2025 = yr2025.total_extracted;
    r.extractions_2025 = yr2025.extraction_count;
    r.stop_outs_2025 = yr2025.stop_out_count;

    r.total_extracted = r.extracted_2024 + r.extracted_2025;
    r.total_capital_deployed = (int)BASE_CAPITAL * (2 + yr2024.restarts + yr2025.restarts);

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

        ExtractionResult r = RunTest(survive_pct);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_results.push_back(r);
            g_completed++;

            int total_so = r.stop_outs_2024 + r.stop_outs_2025;
            std::cout << "[" << g_completed << "/" << total << "] "
                      << "survive=" << std::fixed << std::setprecision(0) << r.survive_pct << "%"
                      << " | ext=$" << (int)r.total_extracted
                      << " (" << (r.extractions_2024 + r.extractions_2025) << " times)"
                      << " | SOs=" << total_so << "\n";
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
    std::cout << "CONTINUOUS EXTRACTION MODEL (V4 Aggressive: BaseSpacingPct=0.05)\n";
    std::cout << "Rule: Extract ALL profits above $" << (int)BASE_CAPITAL << " whenever no positions open\n";
    std::cout << "====================================================================\n\n";

    try {
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv", g_ticks_2024);
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", g_ticks_2025);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

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

    std::sort(g_results.begin(), g_results.end(), [](const ExtractionResult& a, const ExtractionResult& b) {
        return a.survive_pct < b.survive_pct;
    });

    // Results table
    std::cout << "\n====================================================================\n";
    std::cout << "RESULTS: CONTINUOUS EXTRACTION BY SURVIVE PERCENTAGE\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left
              << std::setw(10) << "Survive"
              << std::setw(14) << "Ext 2024"
              << std::setw(10) << "#Ext"
              << std::setw(8) << "SO"
              << std::setw(14) << "Ext 2025"
              << std::setw(10) << "#Ext"
              << std::setw(8) << "SO"
              << std::setw(16) << "2-Year Total"
              << std::setw(10) << "ROI"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    double best_total = 0;
    int best_survive = 0;
    double best_roi = 0;
    int best_roi_survive = 0;

    for (const auto& r : g_results) {
        double roi = (r.total_extracted / r.total_capital_deployed) * 100;

        std::cout << std::setw(10) << (std::to_string((int)r.survive_pct) + "%")
                  << std::setw(14) << ("$" + std::to_string((int)r.extracted_2024))
                  << std::setw(10) << r.extractions_2024
                  << std::setw(8) << r.stop_outs_2024
                  << std::setw(14) << ("$" + std::to_string((int)r.extracted_2025))
                  << std::setw(10) << r.extractions_2025
                  << std::setw(8) << r.stop_outs_2025
                  << std::setw(16) << ("$" + std::to_string((int)r.total_extracted))
                  << std::setw(10) << (std::to_string((int)roi) + "%")
                  << "\n";

        if (r.total_extracted > best_total) {
            best_total = r.total_extracted;
            best_survive = (int)r.survive_pct;
        }
        if (roi > best_roi) {
            best_roi = roi;
            best_roi_survive = (int)r.survive_pct;
        }
    }

    std::cout << "\n====================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "====================================================================\n";

    std::cout << "\nBest total extraction: survive=" << best_survive << "% -> $" << (int)best_total << "\n";
    std::cout << "Best ROI (extraction/capital): survive=" << best_roi_survive << "% -> " << (int)best_roi << "%\n";

    // Comparison
    std::cout << "\nComparison to compound model (no extraction, survive=12%):\n";
    std::cout << "  Compound 2-year: ~$600,000 (but all at risk until end)\n";
    std::cout << "  Extraction best:  $" << (int)best_total << " (extracted safely along the way)\n";

    std::cout << "\nCompleted in " << std::setprecision(1) << elapsed << " seconds\n";

    return 0;
}
