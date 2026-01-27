/**
 * Adaptive Control Strategy - Quick Focused Sweep
 *
 * Reduced sweep focused on key questions:
 * 1. Can self-tuning achieve target DD while maximizing returns?
 * 2. Does adaptive control improve regime independence?
 * 3. What adaptation speed works best?
 * 4. Does it converge to optimal parameters?
 */

#include "../include/strategy_adaptive_control.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>

using namespace backtest;

// Shared tick data
std::vector<Tick> g_shared_ticks_2025;
std::vector<Tick> g_shared_ticks_2024;

void LoadTickData(const std::string& path, std::vector<Tick>& dest, const std::string& label) {
    std::cout << "Loading " << label << " tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Cannot open tick file: " << path << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);

    dest.reserve(52000000);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;

        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        tick.timestamp = datetime_str;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;

        dest.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "  Loaded " << dest.size() << " ticks in " << duration.count() << "s" << std::endl;
}

struct AdaptiveTask {
    double target_max_dd;
    double target_min_sharpe;
    double adaptation_speed;
    int lookback_trades;
    double initial_survive;
    double initial_spacing;
    bool is_baseline;
    std::string year;
    std::string label;
};

struct AdaptiveResult {
    std::string label;
    std::string year;
    double target_max_dd;
    double target_min_sharpe;
    double adaptation_speed;
    int lookback_trades;
    double initial_survive;
    double initial_spacing;
    bool is_baseline;

    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;

    double final_survive;
    double final_spacing;
    int total_adaptations;
    int survive_adaptations;
    int spacing_adaptations;
    double final_rolling_dd;
    double final_rolling_sharpe;
    bool stopped_out;
};

class WorkQueue {
    std::queue<AdaptiveTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const AdaptiveTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(AdaptiveTask& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !tasks_.empty() || done_; });
        if (tasks_.empty()) return false;
        task = tasks_.front();
        tasks_.pop();
        return true;
    }

    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        cv_.notify_all();
    }
};

std::atomic<int> g_completed{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<AdaptiveResult> g_results;

AdaptiveResult run_adaptive_test(const AdaptiveTask& task, const std::vector<Tick>& ticks) {
    AdaptiveResult r;
    r.label = task.label;
    r.year = task.year;
    r.target_max_dd = task.target_max_dd;
    r.target_min_sharpe = task.target_min_sharpe;
    r.adaptation_speed = task.adaptation_speed;
    r.lookback_trades = task.lookback_trades;
    r.initial_survive = task.initial_survive;
    r.initial_spacing = task.initial_spacing;
    r.is_baseline = task.is_baseline;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.final_survive = task.initial_survive;
    r.final_spacing = task.initial_spacing;
    r.total_adaptations = 0;
    r.survive_adaptations = 0;
    r.spacing_adaptations = 0;
    r.final_rolling_dd = 0;
    r.final_rolling_sharpe = 0;
    r.stopped_out = false;

    if (ticks.empty()) {
        r.stopped_out = true;
        return r;
    }

    TickBacktestConfig cfg;
    cfg.symbol = "XAUUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 100.0;
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.01;
    cfg.swap_long = -66.99;
    cfg.swap_short = 41.2;
    cfg.swap_mode = 1;
    cfg.swap_3days = 3;
    cfg.start_date = (task.year == "2024") ? "2024.01.01" : "2025.01.01";
    cfg.end_date = (task.year == "2024") ? "2024.12.30" : "2025.12.30";
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        if (task.is_baseline) {
            FillUpOscillation strategy(task.initial_survive, task.initial_spacing,
                                       0.01, 10.0, 100.0, 500.0,
                                       FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = res.max_drawdown_pct;
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.final_survive = task.initial_survive;
            r.final_spacing = strategy.GetCurrentSpacing();
        } else {
            StrategyAdaptiveControl::Config config;
            config.target_max_dd = task.target_max_dd;
            config.target_min_sharpe = task.target_min_sharpe;
            config.adaptation_speed = task.adaptation_speed;
            config.lookback_trades = task.lookback_trades;
            config.initial_survive = task.initial_survive;
            config.initial_spacing = task.initial_spacing;
            config.min_survive = 10.0;
            config.max_survive = 25.0;
            config.min_spacing = 0.5;
            config.max_spacing = 5.0;
            config.min_volume = 0.01;
            config.max_volume = 10.0;
            config.contract_size = 100.0;
            config.leverage = 500.0;

            StrategyAdaptiveControl strategy(config);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = strategy.GetMaxDD();
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.final_survive = strategy.GetCurrentSurvive();
            r.final_spacing = strategy.GetCurrentSpacing();
            r.total_adaptations = strategy.GetTotalAdaptations();
            r.survive_adaptations = strategy.GetSurviveAdaptations();
            r.spacing_adaptations = strategy.GetSpacingAdaptations();

            auto metrics = strategy.GetCurrentMetrics();
            r.final_rolling_dd = metrics.rolling_dd;
            r.final_rolling_sharpe = metrics.rolling_sharpe;
        }

        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.stopped_out = (r.return_mult < 0.1);

    } catch (const std::exception& e) {
        r.stopped_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total) {
    AdaptiveTask task;
    while (queue.pop(task)) {
        const std::vector<Tick>& ticks = (task.year == "2024") ? g_shared_ticks_2024 : g_shared_ticks_2025;
        AdaptiveResult r = run_adaptive_test(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        int done = ++g_completed;
        if (done % 5 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "\rProgress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * done / total) << "%)" << std::flush;
        }
    }
}

std::vector<AdaptiveTask> GenerateTasks() {
    std::vector<AdaptiveTask> tasks;

    std::vector<std::string> years = {"2024", "2025"};

    // Baseline configs (fixed parameters, no adaptation) - critical reference points
    std::vector<std::pair<double, double>> baseline_params = {
        {13.0, 1.5},   // Optimal from manual sweep
        {12.0, 1.5},
        {15.0, 1.5},
    };

    for (const auto& year : years) {
        for (const auto& [surv, spac] : baseline_params) {
            AdaptiveTask t;
            t.target_max_dd = 0;
            t.target_min_sharpe = 0;
            t.adaptation_speed = 0;
            t.lookback_trades = 0;
            t.initial_survive = surv;
            t.initial_spacing = spac;
            t.is_baseline = true;
            t.year = year;
            t.label = "BASE_s" + std::to_string((int)surv) + "_sp1.5";
            tasks.push_back(t);
        }
    }

    // Focused adaptive control sweep - reduced but covering key questions
    // Key parameters:
    // - target_max_dd: [40, 50, 60] - can we hit DD targets?
    // - target_min_sharpe: [5, 8] - reasonable Sharpe goals
    // - adaptation_speed: [0.02, 0.05, 0.1] - slow/medium/fast learning
    // - lookback_trades: [50, 100] - short vs medium window
    // - initial_survive: [12, 13] - start near optimal
    // - initial_spacing: [1.5] - start at optimal

    std::vector<double> target_dds = {40, 50, 60};
    std::vector<double> target_sharpes = {5, 8};
    std::vector<double> adapt_speeds = {0.02, 0.05, 0.1};
    std::vector<int> lookbacks = {50, 100};
    std::vector<double> init_survives = {12, 13};
    std::vector<double> init_spacings = {1.5};

    for (const auto& year : years) {
        for (double tdd : target_dds) {
            for (double tsh : target_sharpes) {
                for (double aspd : adapt_speeds) {
                    for (int lb : lookbacks) {
                        for (double isurv : init_survives) {
                            for (double ispac : init_spacings) {
                                AdaptiveTask t;
                                t.target_max_dd = tdd;
                                t.target_min_sharpe = tsh;
                                t.adaptation_speed = aspd;
                                t.lookback_trades = lb;
                                t.initial_survive = isurv;
                                t.initial_spacing = ispac;
                                t.is_baseline = false;
                                t.year = year;

                                std::ostringstream oss;
                                oss << "AD_dd" << (int)tdd << "_sh" << (int)tsh
                                    << "_a" << std::fixed << std::setprecision(2) << aspd
                                    << "_lb" << lb
                                    << "_s" << (int)isurv;
                                t.label = oss.str();

                                tasks.push_back(t);
                            }
                        }
                    }
                }
            }
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  ADAPTIVE CONTROL STRATEGY - QUICK SWEEP" << std::endl;
    std::cout << "  Self-tuning survive_pct and spacing based on performance" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";

    LoadTickData(path_2025, g_shared_ticks_2025, "2025");
    LoadTickData(path_2024, g_shared_ticks_2024, "2024");

    std::cout << std::endl;

    auto tasks = GenerateTasks();
    int total = (int)tasks.size();

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " worker threads" << std::endl;
    std::cout << "Testing " << total << " configurations..." << std::endl;
    std::cout << std::endl;

    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total);
    }

    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds ("
              << std::fixed << std::setprecision(2) << (double)duration.count() / total
              << "s per config)" << std::endl;
    std::cout << std::endl;

    // Separate results by year
    std::vector<AdaptiveResult> results_2025, results_2024;
    for (const auto& r : g_results) {
        if (r.year == "2025") results_2025.push_back(r);
        else results_2024.push_back(r);
    }

    auto sorter = [](const AdaptiveResult& a, const AdaptiveResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    };
    std::sort(results_2025.begin(), results_2025.end(), sorter);
    std::sort(results_2024.begin(), results_2024.end(), sorter);

    // ========================================================================
    // Print 2025 results
    // ========================================================================
    std::cout << "================================================================" << std::endl;
    std::cout << "  2025 RESULTS - TOP 20 (sorted by Sharpe proxy)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(35) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Sharpe"
              << std::setw(6) << "Adapt"
              << std::setw(8) << "FinalS"
              << std::setw(8) << "FinalSp"
              << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)20, results_2025.size()); i++) {
        const auto& r = results_2025[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(35) << r.label.substr(0, 34)
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(7) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(6) << r.total_adaptations
                  << std::setw(7) << std::setprecision(1) << r.final_survive << "%"
                  << std::setw(7) << std::setprecision(2) << r.final_spacing << "$"
                  << std::endl;
    }

    // ========================================================================
    // Print 2024 results
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  2024 RESULTS - TOP 20 (sorted by Sharpe proxy)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(35) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Sharpe"
              << std::setw(6) << "Adapt"
              << std::setw(8) << "FinalS"
              << std::setw(8) << "FinalSp"
              << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)20, results_2024.size()); i++) {
        const auto& r = results_2024[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(35) << r.label.substr(0, 34)
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(7) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(6) << r.total_adaptations
                  << std::setw(7) << std::setprecision(1) << r.final_survive << "%"
                  << std::setw(7) << std::setprecision(2) << r.final_spacing << "$"
                  << std::endl;
    }

    // ========================================================================
    // KEY QUESTION 1: Can self-tuning achieve target DD?
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  Q1: DD TARGET ACHIEVEMENT" << std::endl;
    std::cout << "  Did adaptive configs achieve their target DD?" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<double> dd_targets = {40, 50, 60};
    for (double tgt : dd_targets) {
        int achieved_25 = 0, total_25 = 0, achieved_24 = 0, total_24 = 0;
        double sum_dd_25 = 0, sum_dd_24 = 0;
        double sum_ret_25 = 0, sum_ret_24 = 0;

        for (const auto& r : g_results) {
            if (!r.is_baseline && r.target_max_dd == tgt) {
                if (r.year == "2025") {
                    total_25++;
                    if (r.max_dd_pct <= tgt) achieved_25++;
                    sum_dd_25 += r.max_dd_pct;
                    sum_ret_25 += r.return_mult;
                } else {
                    total_24++;
                    if (r.max_dd_pct <= tgt) achieved_24++;
                    sum_dd_24 += r.max_dd_pct;
                    sum_ret_24 += r.return_mult;
                }
            }
        }

        std::cout << "Target DD: " << std::fixed << std::setprecision(0) << tgt << "%" << std::endl;
        if (total_25 > 0) {
            std::cout << "  2025: " << achieved_25 << "/" << total_25 << " achieved ("
                      << std::setprecision(0) << (100.0 * achieved_25 / total_25) << "%)"
                      << "  Avg DD: " << std::setprecision(1) << (sum_dd_25 / total_25) << "%"
                      << "  Avg Return: " << std::setprecision(2) << (sum_ret_25 / total_25) << "x" << std::endl;
        }
        if (total_24 > 0) {
            std::cout << "  2024: " << achieved_24 << "/" << total_24 << " achieved ("
                      << std::setprecision(0) << (100.0 * achieved_24 / total_24) << "%)"
                      << "  Avg DD: " << std::setprecision(1) << (sum_dd_24 / total_24) << "%"
                      << "  Avg Return: " << std::setprecision(2) << (sum_ret_24 / total_24) << "x" << std::endl;
        }
    }

    // ========================================================================
    // KEY QUESTION 2: Regime Independence
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  Q2: REGIME INDEPENDENCE (2025/2024 ratio)" << std::endl;
    std::cout << "  Lower ratio = more stable across market regimes" << std::endl;
    std::cout << "================================================================" << std::endl;

    struct RegimeAnalysis {
        std::string label;
        double ret_2025, ret_2024, ratio;
        double dd_2025, dd_2024;
        bool is_baseline;
    };
    std::vector<RegimeAnalysis> regime_results;

    for (const auto& r25 : results_2025) {
        for (const auto& r24 : results_2024) {
            if (r25.label == r24.label && r24.return_mult > 0.5 && r25.return_mult > 0.5) {
                RegimeAnalysis ra;
                ra.label = r25.label;
                ra.ret_2025 = r25.return_mult;
                ra.ret_2024 = r24.return_mult;
                ra.ratio = r25.return_mult / r24.return_mult;
                ra.dd_2025 = r25.max_dd_pct;
                ra.dd_2024 = r24.max_dd_pct;
                ra.is_baseline = r25.is_baseline;
                regime_results.push_back(ra);
                break;
            }
        }
    }

    std::sort(regime_results.begin(), regime_results.end(), [](const RegimeAnalysis& a, const RegimeAnalysis& b) {
        return std::abs(a.ratio - 1.0) < std::abs(b.ratio - 1.0);
    });

    std::cout << std::left << std::setw(35) << "Label"
              << std::right << std::setw(8) << "2025"
              << std::setw(8) << "2024"
              << std::setw(8) << "Ratio"
              << std::setw(6) << "Type"
              << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)15, regime_results.size()); i++) {
        const auto& ra = regime_results[i];
        std::cout << std::left << std::setw(35) << ra.label.substr(0, 34)
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << ra.ret_2025 << "x"
                  << std::setw(6) << std::setprecision(2) << ra.ret_2024 << "x"
                  << std::setw(8) << std::setprecision(2) << ra.ratio
                  << std::setw(6) << (ra.is_baseline ? "BASE" : "ADPT")
                  << std::endl;
    }

    // Calculate average regime ratios
    double base_ratio_sum = 0, adapt_ratio_sum = 0;
    int base_count = 0, adapt_count = 0;
    for (const auto& ra : regime_results) {
        if (ra.is_baseline) { base_ratio_sum += ra.ratio; base_count++; }
        else { adapt_ratio_sum += ra.ratio; adapt_count++; }
    }
    std::cout << std::endl;
    if (base_count > 0) std::cout << "Baseline avg ratio: " << std::fixed << std::setprecision(2) << (base_ratio_sum / base_count) << std::endl;
    if (adapt_count > 0) std::cout << "Adaptive avg ratio: " << std::fixed << std::setprecision(2) << (adapt_ratio_sum / adapt_count) << std::endl;

    // ========================================================================
    // KEY QUESTION 3: Adaptation Speed
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  Q3: ADAPTATION SPEED COMPARISON" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<double> adapt_speeds = {0.02, 0.05, 0.1};
    for (double spd : adapt_speeds) {
        double sum_ret_25 = 0, sum_dd_25 = 0, sum_adapt_25 = 0;
        double sum_ret_24 = 0, sum_dd_24 = 0, sum_adapt_24 = 0;
        int cnt_25 = 0, cnt_24 = 0;

        for (const auto& r : g_results) {
            if (!r.is_baseline && std::abs(r.adaptation_speed - spd) < 0.001) {
                if (r.year == "2025") {
                    sum_ret_25 += r.return_mult;
                    sum_dd_25 += r.max_dd_pct;
                    sum_adapt_25 += r.total_adaptations;
                    cnt_25++;
                } else {
                    sum_ret_24 += r.return_mult;
                    sum_dd_24 += r.max_dd_pct;
                    sum_adapt_24 += r.total_adaptations;
                    cnt_24++;
                }
            }
        }

        std::cout << "Speed: " << std::fixed << std::setprecision(2) << spd << std::endl;
        if (cnt_25 > 0) {
            std::cout << "  2025: Return " << std::setprecision(2) << (sum_ret_25 / cnt_25) << "x"
                      << "  DD " << std::setprecision(1) << (sum_dd_25 / cnt_25) << "%"
                      << "  Adaptations " << std::setprecision(0) << (sum_adapt_25 / cnt_25) << std::endl;
        }
        if (cnt_24 > 0) {
            std::cout << "  2024: Return " << std::setprecision(2) << (sum_ret_24 / cnt_24) << "x"
                      << "  DD " << std::setprecision(1) << (sum_dd_24 / cnt_24) << "%"
                      << "  Adaptations " << std::setprecision(0) << (sum_adapt_24 / cnt_24) << std::endl;
        }
    }

    // ========================================================================
    // KEY QUESTION 4: Convergence to Optimal
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  Q4: CONVERGENCE TO OPTIMAL (s=13, sp=1.5)" << std::endl;
    std::cout << "  Do final parameters approach known optimal?" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find configs with most adaptations
    std::vector<AdaptiveResult> high_adapt;
    for (const auto& r : g_results) {
        if (!r.is_baseline && r.total_adaptations > 5) {
            high_adapt.push_back(r);
        }
    }

    std::sort(high_adapt.begin(), high_adapt.end(), [](const AdaptiveResult& a, const AdaptiveResult& b) {
        return a.total_adaptations > b.total_adaptations;
    });

    std::cout << std::left << std::setw(35) << "Label"
              << std::right << std::setw(6) << "Year"
              << std::setw(6) << "#Adp"
              << std::setw(8) << "InitS"
              << std::setw(8) << "FinalS"
              << std::setw(8) << "FinalSp"
              << std::setw(8) << "Return"
              << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)12, high_adapt.size()); i++) {
        const auto& r = high_adapt[i];
        std::cout << std::left << std::setw(35) << r.label.substr(0, 34)
                  << std::right << std::setw(6) << r.year
                  << std::setw(6) << r.total_adaptations
                  << std::setw(7) << std::fixed << std::setprecision(1) << r.initial_survive << "%"
                  << std::setw(7) << std::setprecision(1) << r.final_survive << "%"
                  << std::setw(7) << std::setprecision(2) << r.final_spacing << "$"
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::endl;
    }

    // ========================================================================
    // BASELINE vs ADAPTIVE SUMMARY
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BASELINE VS ADAPTIVE SUMMARY" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find best of each type for each year
    AdaptiveResult best_base_25{}, best_base_24{}, best_adapt_25{}, best_adapt_24{};
    bool found_b25 = false, found_b24 = false, found_a25 = false, found_a24 = false;

    for (const auto& r : results_2025) {
        if (r.is_baseline && (!found_b25 || r.sharpe_proxy > best_base_25.sharpe_proxy)) {
            best_base_25 = r; found_b25 = true;
        }
        if (!r.is_baseline && (!found_a25 || r.sharpe_proxy > best_adapt_25.sharpe_proxy)) {
            best_adapt_25 = r; found_a25 = true;
        }
    }
    for (const auto& r : results_2024) {
        if (r.is_baseline && (!found_b24 || r.sharpe_proxy > best_base_24.sharpe_proxy)) {
            best_base_24 = r; found_b24 = true;
        }
        if (!r.is_baseline && (!found_a24 || r.sharpe_proxy > best_adapt_24.sharpe_proxy)) {
            best_adapt_24 = r; found_a24 = true;
        }
    }

    std::cout << "2025:" << std::endl;
    std::cout << "  Best BASELINE: " << best_base_25.label
              << " | Return: " << std::fixed << std::setprecision(2) << best_base_25.return_mult << "x"
              << " | DD: " << std::setprecision(1) << best_base_25.max_dd_pct << "%"
              << " | Sharpe: " << std::setprecision(2) << best_base_25.sharpe_proxy << std::endl;
    std::cout << "  Best ADAPTIVE: " << best_adapt_25.label
              << " | Return: " << std::fixed << std::setprecision(2) << best_adapt_25.return_mult << "x"
              << " | DD: " << std::setprecision(1) << best_adapt_25.max_dd_pct << "%"
              << " | Sharpe: " << std::setprecision(2) << best_adapt_25.sharpe_proxy << std::endl;
    std::cout << "  Adapt -> Final: survive " << std::setprecision(1) << best_adapt_25.initial_survive << "% -> "
              << best_adapt_25.final_survive << "%  spacing $" << std::setprecision(2) << best_adapt_25.initial_spacing << " -> $"
              << best_adapt_25.final_spacing << std::endl;

    std::cout << std::endl << "2024:" << std::endl;
    std::cout << "  Best BASELINE: " << best_base_24.label
              << " | Return: " << std::fixed << std::setprecision(2) << best_base_24.return_mult << "x"
              << " | DD: " << std::setprecision(1) << best_base_24.max_dd_pct << "%"
              << " | Sharpe: " << std::setprecision(2) << best_base_24.sharpe_proxy << std::endl;
    std::cout << "  Best ADAPTIVE: " << best_adapt_24.label
              << " | Return: " << std::fixed << std::setprecision(2) << best_adapt_24.return_mult << "x"
              << " | DD: " << std::setprecision(1) << best_adapt_24.max_dd_pct << "%"
              << " | Sharpe: " << std::setprecision(2) << best_adapt_24.sharpe_proxy << std::endl;
    std::cout << "  Adapt -> Final: survive " << std::setprecision(1) << best_adapt_24.initial_survive << "% -> "
              << best_adapt_24.final_survive << "%  spacing $" << std::setprecision(2) << best_adapt_24.initial_spacing << " -> $"
              << best_adapt_24.final_spacing << std::endl;

    std::cout << std::endl;
    std::cout << "Total configs: " << g_results.size() << " | Time: " << duration.count() << "s" << std::endl;

    return 0;
}
