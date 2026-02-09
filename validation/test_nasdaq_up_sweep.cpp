/**
 * NasdaqUp Strategy Parameter Sweep
 *
 * Explores the parameter space to understand:
 * 1. How multiplier affects sizing and survival
 * 2. How power sign (+/-) affects room dynamics
 * 3. How stop_out_margin affects risk/reward tradeoff
 *
 * Uses RunWithTicks() pattern for efficient parallel testing.
 */
#include "../include/strategy_nasdaq_up.h"
#include "../include/tick_based_engine.h"
#include "../include/fast_tick_parser.h"
#include "../include/sweep_results_writer.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <chrono>

using namespace backtest;

struct SweepResult {
    double multiplier;
    double power;
    double stop_out_margin;
    double final_balance;
    double return_multiple;
    double max_drawdown_pct;
    int total_entries;
    int stop_outs;
    int cycles;
    double peak_volume;
    double max_room;
    double min_room;
    bool valid;  // Did it survive without blowing up?
};

int main() {
    std::cout << "=== NasdaqUp Parameter Sweep ===" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    auto start_time = std::chrono::steady_clock::now();

    // === Step 1: Load ticks ONCE into memory ===
    std::cout << "Step 1: Loading tick data..." << std::flush;
    std::string tick_file = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";

    std::vector<Tick> all_ticks;
    size_t loaded = FastTickParser::LoadAllTicks(tick_file, all_ticks);
    std::cout << " Loaded " << loaded << " ticks" << std::endl;

    if (loaded == 0) {
        std::cerr << "ERROR: No ticks loaded!" << std::endl;
        return 1;
    }

    // === Step 2: Define parameter ranges ===
    // Multiplier: controls initial "room" as % of price
    std::vector<double> multipliers = {0.01, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0};

    // Power: controls how room changes with price gain
    // Negative = room shrinks (aggressive), Positive = room grows (conservative)
    std::vector<double> powers = {-2.0, -1.5, -1.0, -0.5, -0.2, -0.1, 0.0, 0.1, 0.2, 0.5, 1.0, 1.5, 2.0};

    // Stop-out margin level
    std::vector<double> stop_out_margins = {50.0, 74.0, 100.0, 150.0};

    size_t total_combos = multipliers.size() * powers.size() * stop_out_margins.size();
    std::cout << "Step 2: Sweep parameters defined" << std::endl;
    std::cout << "  Multipliers: " << multipliers.size() << " values" << std::endl;
    std::cout << "  Powers: " << powers.size() << " values" << std::endl;
    std::cout << "  Stop-out margins: " << stop_out_margins.size() << " values" << std::endl;
    std::cout << "  Total combinations: " << total_combos << std::endl;

    // === Step 3: Configure base engine settings ===
    TickBacktestConfig base_config;
    base_config.symbol = "XAUUSD";
    base_config.initial_balance = 10000.0;
    base_config.contract_size = 100.0;
    base_config.leverage = 500.0;
    base_config.pip_size = 0.01;
    base_config.swap_long = -66.99;
    base_config.swap_short = 41.2;
    base_config.swap_mode = 1;
    base_config.swap_3days = 3;
    base_config.start_date = "2025.01.01";
    base_config.end_date = "2025.03.31";  // 3 months
    base_config.verbose = false;

    // === Step 4: Run sweep ===
    std::cout << "Step 3: Running sweep..." << std::endl;
    std::vector<SweepResult> results;
    results.reserve(total_combos);

    size_t completed = 0;

    for (double mult : multipliers) {
        for (double pow : powers) {
            for (double margin : stop_out_margins) {
                // Create fresh engine
                TickBasedEngine engine(base_config);

                // Create strategy with these params
                NasdaqUp::Config strat_config;
                strat_config.multiplier = mult;
                strat_config.power = pow;
                strat_config.stop_out_margin = margin;
                strat_config.contract_size = 100.0;
                strat_config.leverage = 500.0;
                strat_config.verbose = false;

                NasdaqUp strategy(strat_config);

                // Run with pre-loaded ticks
                engine.RunWithTicks(all_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                    strategy.OnTick(tick, eng);
                });

                // Collect results
                auto eng_results = engine.GetResults();
                auto stats = strategy.GetStats();

                SweepResult r;
                r.multiplier = mult;
                r.power = pow;
                r.stop_out_margin = margin;
                r.final_balance = eng_results.final_balance;
                r.return_multiple = eng_results.final_balance / eng_results.initial_balance;
                r.max_drawdown_pct = eng_results.max_drawdown_pct;
                r.total_entries = stats.total_entries;
                r.stop_outs = stats.stop_outs;
                r.cycles = stats.cycles;
                r.peak_volume = stats.peak_volume;
                r.max_room = stats.max_room_seen;
                r.min_room = (stats.min_room_seen == DBL_MAX) ? 0 : stats.min_room_seen;
                r.valid = (eng_results.final_balance > 0 && !eng_results.stop_out_occurred);

                results.push_back(r);

                completed++;
                if (completed % 50 == 0 || completed == total_combos) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    double secs = std::chrono::duration<double>(elapsed).count();
                    double rate = completed / secs;
                    std::cout << "\r  " << completed << "/" << total_combos
                              << " (" << (100.0 * completed / total_combos) << "%, "
                              << rate << " combos/s)   " << std::flush;
                }
            }
        }
    }
    std::cout << "\n  Sweep complete!" << std::endl;

    // === Step 5: Save results to CSV ===
    std::cout << "Step 4: Saving results..." << std::flush;
    std::string csv_file = "nasdaq_up_sweep_results.csv";
    std::ofstream csv(csv_file);
    csv << "multiplier,power,stop_out_margin,final_balance,return_multiple,max_drawdown_pct,"
        << "total_entries,stop_outs,cycles,peak_volume,max_room,min_room,valid\n";

    for (const auto& r : results) {
        csv << r.multiplier << ","
            << r.power << ","
            << r.stop_out_margin << ","
            << r.final_balance << ","
            << r.return_multiple << ","
            << r.max_drawdown_pct << ","
            << r.total_entries << ","
            << r.stop_outs << ","
            << r.cycles << ","
            << r.peak_volume << ","
            << r.max_room << ","
            << r.min_room << ","
            << (r.valid ? 1 : 0) << "\n";
    }
    csv.close();
    std::cout << " Saved to " << csv_file << std::endl;

    // === Step 6: Analyze results ===
    std::cout << "\n=== Analysis ===" << std::endl;

    // Find best by return
    auto best_return = std::max_element(results.begin(), results.end(),
        [](const SweepResult& a, const SweepResult& b) {
            return a.return_multiple < b.return_multiple;
        });

    // Find best by Calmar-like ratio (return / drawdown)
    auto best_calmar = std::max_element(results.begin(), results.end(),
        [](const SweepResult& a, const SweepResult& b) {
            double calmar_a = (a.max_drawdown_pct > 0) ? a.return_multiple / a.max_drawdown_pct : 0;
            double calmar_b = (b.max_drawdown_pct > 0) ? b.return_multiple / b.max_drawdown_pct : 0;
            return calmar_a < calmar_b;
        });

    // Find most stable (fewest stop-outs with positive return)
    auto most_stable = std::min_element(results.begin(), results.end(),
        [](const SweepResult& a, const SweepResult& b) {
            if (a.return_multiple < 1.0 && b.return_multiple >= 1.0) return true;
            if (a.return_multiple >= 1.0 && b.return_multiple < 1.0) return false;
            return a.stop_outs < b.stop_outs;
        });

    std::cout << "\nBest by Return:" << std::endl;
    std::cout << "  multiplier=" << best_return->multiplier
              << ", power=" << best_return->power
              << ", margin=" << best_return->stop_out_margin << "%" << std::endl;
    std::cout << "  Return: " << best_return->return_multiple << "x"
              << ", DD: " << best_return->max_drawdown_pct << "%"
              << ", StopOuts: " << best_return->stop_outs << std::endl;

    std::cout << "\nBest by Risk-Adjusted (Return/DD):" << std::endl;
    std::cout << "  multiplier=" << best_calmar->multiplier
              << ", power=" << best_calmar->power
              << ", margin=" << best_calmar->stop_out_margin << "%" << std::endl;
    std::cout << "  Return: " << best_calmar->return_multiple << "x"
              << ", DD: " << best_calmar->max_drawdown_pct << "%"
              << ", StopOuts: " << best_calmar->stop_outs << std::endl;

    std::cout << "\nMost Stable (fewest stop-outs with profit):" << std::endl;
    std::cout << "  multiplier=" << most_stable->multiplier
              << ", power=" << most_stable->power
              << ", margin=" << most_stable->stop_out_margin << "%" << std::endl;
    std::cout << "  Return: " << most_stable->return_multiple << "x"
              << ", DD: " << most_stable->max_drawdown_pct << "%"
              << ", StopOuts: " << most_stable->stop_outs << std::endl;

    // === Step 7: Power sign analysis ===
    std::cout << "\n=== Power Sign Analysis ===" << std::endl;

    double avg_return_pos = 0, avg_return_neg = 0, avg_return_zero = 0;
    int count_pos = 0, count_neg = 0, count_zero = 0;
    int stopouts_pos = 0, stopouts_neg = 0, stopouts_zero = 0;

    for (const auto& r : results) {
        if (r.power > 0.01) {
            avg_return_pos += r.return_multiple;
            stopouts_pos += r.stop_outs;
            count_pos++;
        } else if (r.power < -0.01) {
            avg_return_neg += r.return_multiple;
            stopouts_neg += r.stop_outs;
            count_neg++;
        } else {
            avg_return_zero += r.return_multiple;
            stopouts_zero += r.stop_outs;
            count_zero++;
        }
    }

    if (count_pos > 0) avg_return_pos /= count_pos;
    if (count_neg > 0) avg_return_neg /= count_neg;
    if (count_zero > 0) avg_return_zero /= count_zero;

    std::cout << "Positive power (room grows): avg return = " << avg_return_pos
              << "x, total stop-outs = " << stopouts_pos << std::endl;
    std::cout << "Negative power (room shrinks): avg return = " << avg_return_neg
              << "x, total stop-outs = " << stopouts_neg << std::endl;
    std::cout << "Zero power (room fixed): avg return = " << avg_return_zero
              << "x, total stop-outs = " << stopouts_zero << std::endl;

    // === Step 8: Multiplier analysis ===
    std::cout << "\n=== Multiplier Effect ===" << std::endl;
    for (double mult : multipliers) {
        double avg_ret = 0;
        int total_stops = 0;
        int count = 0;
        for (const auto& r : results) {
            if (std::abs(r.multiplier - mult) < 0.001) {
                avg_ret += r.return_multiple;
                total_stops += r.stop_outs;
                count++;
            }
        }
        if (count > 0) avg_ret /= count;
        std::cout << "  multiplier=" << std::setw(6) << mult
                  << " -> avg return = " << std::setw(8) << avg_ret << "x"
                  << ", total stop-outs = " << total_stops << std::endl;
    }

    auto total_elapsed = std::chrono::steady_clock::now() - start_time;
    double total_secs = std::chrono::duration<double>(total_elapsed).count();
    std::cout << "\nTotal time: " << total_secs << " seconds" << std::endl;

    std::cout << "\n*** SWEEP COMPLETED ***" << std::endl;
    return 0;
}
