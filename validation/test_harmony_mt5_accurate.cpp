/**
 * Harmony Strategy - MT5 Accurate Implementation
 *
 * Combines "up while up" + "up while down" using EXACT MT5 lot sizing formulas.
 *
 * Key differences from previous implementation:
 * 1. Uses margin_stop_out_level (20%) in lot sizing
 * 2. Up While Up: opens at every new high (no fixed spacing)
 * 3. Up While Down: calculates spacing to fit N trades in survive distance
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cfloat>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

using namespace backtest;

// Global shared tick data
std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data into memory..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    g_shared_ticks.reserve(52000000);

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

        g_shared_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Loaded " << g_shared_ticks.size() << " ticks in "
              << duration.count() << " seconds" << std::endl;
}

/**
 * UpWhileUpStrategy - MT5 Accurate
 * Opens BUY at every new price high, lot sizing uses margin stop-out level
 */
class UpWhileUpStrategy {
public:
    struct Config {
        double survive_pct = 12.0;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double margin_stop_out_level = 20.0;  // 20% stop-out level
        double initial_margin_rate = 1.0;
    };

    struct Stats {
        int entries = 0;
        double total_volume = 0.0;
        double max_volume = 0.0;
    };

private:
    Config config_;
    Stats stats_;
    double checked_last_open_price_ = DBL_MIN;
    double volume_of_open_trades_ = 0.0;

public:
    UpWhileUpStrategy(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double current_ask = tick.ask;
        double spread = tick.ask - tick.bid;

        // Open new if price is rising (MT5: opens at EVERY new high)
        if (volume_of_open_trades_ == 0.0 || checked_last_open_price_ < current_ask) {
            checked_last_open_price_ = current_ask;

            // Scan positions to get current volume and calculate used margin
            volume_of_open_trades_ = 0.0;
            double used_margin = 0.0;
            for (const Trade* trade : engine.GetOpenPositions()) {
                if (trade->direction == "BUY") {
                    volume_of_open_trades_ += trade->lot_size;
                    used_margin += trade->lot_size * config_.contract_size * current_ask / config_.leverage * config_.initial_margin_rate;
                }
            }

            double equity = engine.GetEquity();
            double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

            double equity_at_target = equity * config_.margin_stop_out_level / current_margin_level;
            double equity_difference = equity - equity_at_target;
            double price_difference = (volume_of_open_trades_ > 0)
                ? equity_difference / (volume_of_open_trades_ * config_.contract_size)
                : 0.0;

            double end_price = current_ask * ((100.0 - config_.survive_pct) / 100.0);
            double distance = current_ask - end_price;

            // Check if we can afford to open more
            if (volume_of_open_trades_ == 0.0 || (current_ask - price_difference) < end_price) {
                // MT5 CFDLEVERAGE formula
                double trade_size = CalculateLotSize(equity, used_margin, current_ask, distance, spread);

                if (trade_size >= config_.min_volume) {
                    Trade* trade = engine.OpenMarketOrder("BUY", trade_size, 0.0, 0.0);
                    if (trade) {
                        volume_of_open_trades_ += trade_size;
                        stats_.entries++;
                        stats_.total_volume += trade_size;
                        stats_.max_volume = std::max(stats_.max_volume, volume_of_open_trades_);
                    }
                }
            }
        }
    }

    double CalculateLotSize(double equity, double used_margin, double current_ask,
                            double distance, double spread) {
        // MT5 CFDLEVERAGE formula from open_upwards_while_going_upwards_optimized.mq5 line 144
        // trade_size = (100 * equity * leverage
        //             - 100 * contract_size * distance * volume * leverage
        //             - leverage * margin_stop_out * used_margin)
        //            / (contract_size * (100 * distance * leverage
        //             + 100 * spread * leverage
        //             + starting_price * margin_rate * margin_stop_out))

        double numerator = 100.0 * equity * config_.leverage
                         - 100.0 * config_.contract_size * distance * volume_of_open_trades_ * config_.leverage
                         - config_.leverage * config_.margin_stop_out_level * used_margin;

        double denominator = config_.contract_size * (
            100.0 * distance * config_.leverage
            + 100.0 * spread * config_.leverage
            + current_ask * config_.initial_margin_rate * config_.margin_stop_out_level
        );

        if (denominator <= 0) return 0.0;

        double lots = numerator / denominator;
        lots = std::max(0.0, lots);
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;  // Round to 2 decimals

        return lots;
    }

    const Stats& GetStats() const { return stats_; }
};

/**
 * UpWhileDownStrategy - MT5 Accurate
 * Opens BUY as price drops, with calculated spacing to fit N trades in survive distance
 */
class UpWhileDownStrategy {
public:
    struct Config {
        double survive_pct = 12.0;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double margin_stop_out_level = 20.0;
        double initial_margin_rate = 1.0;
        int sizing_mode = 0;  // 0 = constant
    };

    struct Stats {
        int entries = 0;
        double total_volume = 0.0;
        double max_volume = 0.0;
        double spacing = 0.0;
        double trade_size = 0.0;
    };

private:
    Config config_;
    Stats stats_;
    double lowest_buy_ = DBL_MAX;
    double highest_buy_ = DBL_MIN;
    double volume_of_open_trades_ = 0.0;
    double trade_size_buy_ = 0.0;
    double spacing_buy_ = 0.0;
    int count_buy_ = 0;
    bool sizing_calculated_ = false;

public:
    UpWhileDownStrategy(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double spread = tick.ask - tick.bid;

        // Scan positions
        Iterate(engine);

        // Calculate sizing on first tick or when no positions
        if (count_buy_ == 0) {
            SizingBuy(tick, engine, spread);
            sizing_calculated_ = true;
        }

        // Open new positions
        OpenNew(tick, engine);
    }

    const Stats& GetStats() const { return stats_; }

private:
    void Iterate(TickBasedEngine& engine) {
        volume_of_open_trades_ = 0.0;
        count_buy_ = 0;
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
                count_buy_++;
                volume_of_open_trades_ += trade->lot_size;
            }
        }
    }

    void SizingBuy(const Tick& tick, TickBasedEngine& engine, double spread) {
        trade_size_buy_ = 0.0;
        spacing_buy_ = 0.0;

        double equity = engine.GetEquity();
        // Calculate used margin from positions
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                used_margin += trade->lot_size * config_.contract_size * tick.ask / config_.leverage * config_.initial_margin_rate;
            }
        }
        double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        double equity_at_target = equity * config_.margin_stop_out_level / current_margin_level;

        double end_price = tick.ask * ((100.0 - config_.survive_pct) / 100.0);
        double starting_price = tick.ask;
        double len = starting_price - end_price;  // survive distance in $

        if (current_margin_level > config_.margin_stop_out_level) {
            double trade_size = config_.min_volume;

            // Calculate margin for one trade (average of start and end price)
            // MT5: local_used_margin = ((trade_size * contract_size * starting_price) / leverage * margin_rate +
            //                          (trade_size * contract_size * end_price) / leverage * margin_rate) / 2
            double local_used_margin = ((trade_size * config_.contract_size * starting_price) / config_.leverage * config_.initial_margin_rate +
                                        (trade_size * config_.contract_size * end_price) / config_.leverage * config_.initial_margin_rate) / 2.0;

            // d_equity = loss at end_price + spread cost
            double d_equity = config_.contract_size * trade_size * len + trade_size * spread * config_.contract_size;

            // Calculate number of trades that fit
            double number_of_trades;
            if (used_margin == 0) {
                number_of_trades = std::floor(equity_at_target / (config_.margin_stop_out_level / 100.0 * local_used_margin + d_equity));
            } else {
                number_of_trades = std::floor((equity_at_target - config_.margin_stop_out_level / 100.0 * used_margin)
                                              / (config_.margin_stop_out_level / 100.0 * local_used_margin + d_equity));
            }

            number_of_trades = std::max(1.0, number_of_trades);

            double proportion = number_of_trades / len;

            // Sizing mode 0: constant size
            if (proportion >= 1.0) {
                spacing_buy_ = 1.0;  // $1 spacing
                trade_size_buy_ = std::floor(proportion) * config_.min_volume;
            } else {
                trade_size_buy_ = config_.min_volume;
                spacing_buy_ = std::round((len / number_of_trades) * 100.0) / 100.0;
            }

            stats_.spacing = spacing_buy_;
            stats_.trade_size = trade_size_buy_;
        }
    }

    void OpenNew(const Tick& tick, TickBasedEngine& engine) {
        double current_ask = tick.ask;

        if (count_buy_ == 0) {
            // First entry
            if (trade_size_buy_ >= config_.min_volume) {
                Trade* trade = engine.OpenMarketOrder("BUY", trade_size_buy_, 0.0, 0.0);
                if (trade) {
                    highest_buy_ = current_ask;
                    lowest_buy_ = current_ask;
                    count_buy_++;
                    volume_of_open_trades_ += trade_size_buy_;
                    stats_.entries++;
                    stats_.total_volume += trade_size_buy_;
                    stats_.max_volume = std::max(stats_.max_volume, volume_of_open_trades_);
                }
            }
        } else {
            // Subsequent entries: only if price dropped below lowest buy - spacing
            if (spacing_buy_ > 0 && lowest_buy_ > current_ask + spacing_buy_) {
                // Calculate how many lots should be open at this price level
                double temp = (highest_buy_ - current_ask) / spacing_buy_;
                double temp1 = std::floor(temp);
                double temp4 = spacing_buy_ / (trade_size_buy_ / config_.min_volume);
                double temp2 = temp - temp1;
                double temp5 = std::floor(temp2 / temp4);
                double temp6 = temp1 * trade_size_buy_ + temp5 * config_.min_volume - (volume_of_open_trades_ - trade_size_buy_);

                if (temp6 > 0) {
                    double lots = std::min(temp6, config_.max_volume);
                    lots = std::floor(lots * 100) / 100;

                    if (lots >= config_.min_volume) {
                        Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
                        if (trade) {
                            lowest_buy_ = current_ask;
                            count_buy_++;
                            volume_of_open_trades_ += lots;
                            stats_.entries++;
                            stats_.total_volume += lots;
                            stats_.max_volume = std::max(stats_.max_volume, volume_of_open_trades_);
                        }
                    }
                }
            }
        }
    }
};

/**
 * HarmonyStrategy - Combines both strategies
 */
class HarmonyStrategy {
public:
    struct Config {
        double survive_pct = 12.0;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double margin_stop_out_level = 20.0;
        double initial_margin_rate = 1.0;
    };

    struct Stats {
        int up_entries = 0;
        int down_entries = 0;
        double up_volume = 0.0;
        double down_volume = 0.0;
        double max_volume = 0.0;
        double down_spacing = 0.0;
    };

private:
    Config config_;
    Stats stats_;

    // Up while up state
    double checked_last_open_price_ = DBL_MIN;

    // Up while down state
    double lowest_buy_ = DBL_MAX;
    double highest_buy_ = DBL_MIN;
    double trade_size_buy_ = 0.0;
    double spacing_buy_ = 0.0;
    int count_buy_ = 0;
    bool sizing_calculated_ = false;

    // Shared state
    double volume_of_open_trades_ = 0.0;

public:
    HarmonyStrategy(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double current_ask = tick.ask;
        double spread = tick.ask - tick.bid;

        // Scan positions
        ScanPositions(engine);

        // Calculate down sizing if needed
        if (count_buy_ == 0 && !sizing_calculated_) {
            CalculateDownSizing(tick, engine, spread);
            sizing_calculated_ = true;
        }

        // Up While Up logic
        if (volume_of_open_trades_ == 0.0 || checked_last_open_price_ < current_ask) {
            checked_last_open_price_ = current_ask;
            TryUpWhileUp(tick, engine, spread);
        }

        // Up While Down logic
        TryUpWhileDown(tick, engine);
    }

    const Stats& GetStats() const { return stats_; }

private:
    void ScanPositions(TickBasedEngine& engine) {
        volume_of_open_trades_ = 0.0;
        count_buy_ = 0;
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
                checked_last_open_price_ = std::max(checked_last_open_price_, trade->entry_price);
                count_buy_++;
                volume_of_open_trades_ += trade->lot_size;
            }
        }
        stats_.max_volume = std::max(stats_.max_volume, volume_of_open_trades_);
    }

    void TryUpWhileUp(const Tick& tick, TickBasedEngine& engine, double spread) {
        double equity = engine.GetEquity();
        // Calculate used margin from positions
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                used_margin += trade->lot_size * config_.contract_size * tick.ask / config_.leverage * config_.initial_margin_rate;
            }
        }
        double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        double equity_at_target = equity * config_.margin_stop_out_level / current_margin_level;
        double equity_difference = equity - equity_at_target;
        double price_difference = (volume_of_open_trades_ > 0)
            ? equity_difference / (volume_of_open_trades_ * config_.contract_size)
            : 0.0;

        double end_price = tick.ask * ((100.0 - config_.survive_pct) / 100.0);
        double distance = tick.ask - end_price;

        if (volume_of_open_trades_ == 0.0 || (tick.ask - price_difference) < end_price) {
            double lots = CalculateUpLotSize(equity, used_margin, tick.ask, distance, spread);

            if (lots >= config_.min_volume) {
                Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
                if (trade) {
                    volume_of_open_trades_ += lots;
                    stats_.up_entries++;
                    stats_.up_volume += lots;
                }
            }
        }
    }

    double CalculateUpLotSize(double equity, double used_margin, double current_ask,
                              double distance, double spread) {
        // MT5 CFDLEVERAGE formula
        double numerator = 100.0 * equity * config_.leverage
                         - 100.0 * config_.contract_size * distance * volume_of_open_trades_ * config_.leverage
                         - config_.leverage * config_.margin_stop_out_level * used_margin;

        double denominator = config_.contract_size * (
            100.0 * distance * config_.leverage
            + 100.0 * spread * config_.leverage
            + current_ask * config_.initial_margin_rate * config_.margin_stop_out_level
        );

        if (denominator <= 0) return 0.0;

        double lots = numerator / denominator;
        lots = std::max(0.0, lots);
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;

        return lots;
    }

    void CalculateDownSizing(const Tick& tick, TickBasedEngine& engine, double spread) {
        double equity = engine.GetEquity();
        // Calculate used margin from positions
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                used_margin += trade->lot_size * config_.contract_size * tick.ask / config_.leverage * config_.initial_margin_rate;
            }
        }
        double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        double equity_at_target = equity * config_.margin_stop_out_level / current_margin_level;

        double end_price = tick.ask * ((100.0 - config_.survive_pct) / 100.0);
        double starting_price = tick.ask;
        double len = starting_price - end_price;

        if (current_margin_level > config_.margin_stop_out_level) {
            double trade_size = config_.min_volume;

            double local_used_margin = ((trade_size * config_.contract_size * starting_price) / config_.leverage * config_.initial_margin_rate +
                                        (trade_size * config_.contract_size * end_price) / config_.leverage * config_.initial_margin_rate) / 2.0;

            double d_equity = config_.contract_size * trade_size * len + trade_size * spread * config_.contract_size;

            double number_of_trades;
            if (used_margin == 0) {
                number_of_trades = std::floor(equity_at_target / (config_.margin_stop_out_level / 100.0 * local_used_margin + d_equity));
            } else {
                number_of_trades = std::floor((equity_at_target - config_.margin_stop_out_level / 100.0 * used_margin)
                                              / (config_.margin_stop_out_level / 100.0 * local_used_margin + d_equity));
            }

            number_of_trades = std::max(1.0, number_of_trades);

            double proportion = number_of_trades / len;

            if (proportion >= 1.0) {
                spacing_buy_ = 1.0;
                trade_size_buy_ = std::floor(proportion) * config_.min_volume;
            } else {
                trade_size_buy_ = config_.min_volume;
                spacing_buy_ = std::round((len / number_of_trades) * 100.0) / 100.0;
            }

            stats_.down_spacing = spacing_buy_;
        }
    }

    void TryUpWhileDown(const Tick& tick, TickBasedEngine& engine) {
        double current_ask = tick.ask;

        if (count_buy_ == 0) {
            // First entry handled by UpWhileUp
            return;
        }

        // Only open if price dropped below lowest - spacing
        if (spacing_buy_ > 0 && lowest_buy_ != DBL_MAX && lowest_buy_ > current_ask + spacing_buy_) {
            double temp = (highest_buy_ - current_ask) / spacing_buy_;
            double temp1 = std::floor(temp);
            double temp4 = spacing_buy_ / (trade_size_buy_ / config_.min_volume);
            double temp2 = temp - temp1;
            double temp5 = std::floor(temp2 / temp4);
            double temp6 = temp1 * trade_size_buy_ + temp5 * config_.min_volume - (volume_of_open_trades_ - trade_size_buy_);

            if (temp6 > 0) {
                double lots = std::min(temp6, config_.max_volume);
                lots = std::floor(lots * 100) / 100;

                if (lots >= config_.min_volume) {
                    Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
                    if (trade) {
                        lowest_buy_ = current_ask;
                        count_buy_++;
                        volume_of_open_trades_ += lots;
                        stats_.down_entries++;
                        stats_.down_volume += lots;
                    }
                }
            }
        }
    }
};

struct TestResult {
    std::string name;
    double survive_pct;
    double final_balance;
    double return_mult;
    double max_dd;
    int entries;
    double total_volume;
    double max_volume;
    double swap_cost;
};

std::mutex g_results_mutex;
std::atomic<int> g_completed(0);

void RunUpWhileUp(double survive_pct, const TickBacktestConfig& base_config,
                  std::vector<TestResult>& results, int total) {
    TickBacktestConfig config = base_config;
    TickBasedEngine engine(config);

    UpWhileUpStrategy::Config cfg;
    cfg.survive_pct = survive_pct;
    UpWhileUpStrategy strategy(cfg);

    engine.RunWithTicks(g_shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto eng_results = engine.GetResults();
    auto stats = strategy.GetStats();

    TestResult result;
    result.name = "UpWhileUp";
    result.survive_pct = survive_pct;
    result.final_balance = eng_results.final_balance;
    result.return_mult = eng_results.final_balance / base_config.initial_balance;
    result.max_dd = eng_results.max_drawdown_pct;
    result.entries = stats.entries;
    result.total_volume = stats.total_volume;
    result.max_volume = stats.max_volume;
    result.swap_cost = eng_results.total_swap_charged;

    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        results.push_back(result);
        int done = ++g_completed;
        std::cout << "\r  Progress: " << done << "/" << total << std::flush;
    }
}

void RunUpWhileDown(double survive_pct, const TickBacktestConfig& base_config,
                    std::vector<TestResult>& results, int total) {
    TickBacktestConfig config = base_config;
    TickBasedEngine engine(config);

    UpWhileDownStrategy::Config cfg;
    cfg.survive_pct = survive_pct;
    UpWhileDownStrategy strategy(cfg);

    engine.RunWithTicks(g_shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto eng_results = engine.GetResults();
    auto stats = strategy.GetStats();

    TestResult result;
    result.name = "UpWhileDown";
    result.survive_pct = survive_pct;
    result.final_balance = eng_results.final_balance;
    result.return_mult = eng_results.final_balance / base_config.initial_balance;
    result.max_dd = eng_results.max_drawdown_pct;
    result.entries = stats.entries;
    result.total_volume = stats.total_volume;
    result.max_volume = stats.max_volume;
    result.swap_cost = eng_results.total_swap_charged;

    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        results.push_back(result);
        int done = ++g_completed;
        std::cout << "\r  Progress: " << done << "/" << total << std::flush;
    }
}

void RunHarmony(double survive_pct, const TickBacktestConfig& base_config,
                std::vector<TestResult>& results, int total) {
    TickBacktestConfig config = base_config;
    TickBasedEngine engine(config);

    HarmonyStrategy::Config cfg;
    cfg.survive_pct = survive_pct;
    HarmonyStrategy strategy(cfg);

    engine.RunWithTicks(g_shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto eng_results = engine.GetResults();
    auto stats = strategy.GetStats();

    TestResult result;
    result.name = "Combined";
    result.survive_pct = survive_pct;
    result.final_balance = eng_results.final_balance;
    result.return_mult = eng_results.final_balance / base_config.initial_balance;
    result.max_dd = eng_results.max_drawdown_pct;
    result.entries = stats.up_entries + stats.down_entries;
    result.total_volume = stats.up_volume + stats.down_volume;
    result.max_volume = stats.max_volume;
    result.swap_cost = eng_results.total_swap_charged;

    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        results.push_back(result);
        int done = ++g_completed;
        std::cout << "\r  Progress: " << done << "/" << total << std::flush;
    }
}

int main() {
    std::cout << "=== Harmony Strategy - MT5 Accurate Implementation ===" << std::endl;
    std::cout << "Using EXACT MT5 lot sizing formulas with margin stop-out level" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Base config
    double initial_balance = 10000.0;
    TickBacktestConfig base_config;
    base_config.symbol = "XAUUSD";
    base_config.initial_balance = initial_balance;
    base_config.account_currency = "USD";
    base_config.contract_size = 100.0;
    base_config.leverage = 500.0;
    base_config.margin_rate = 1.0;
    base_config.pip_size = 0.01;
    base_config.swap_long = -66.99;
    base_config.swap_short = 41.2;
    base_config.swap_mode = 1;
    base_config.swap_3days = 3;
    base_config.verbose = false;

    // Test survive values
    std::vector<double> survive_values = {12.0, 13.0, 14.0, 15.0};
    int total_tests = survive_values.size() * 3;  // 3 strategies per survive%

    std::cout << "Running " << total_tests << " tests..." << std::endl;

    std::vector<TestResult> results;
    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();

    // Launch all tests
    for (double surv : survive_values) {
        threads.emplace_back(RunUpWhileUp, surv, std::cref(base_config), std::ref(results), total_tests);
        threads.emplace_back(RunUpWhileDown, surv, std::cref(base_config), std::ref(results), total_tests);
        threads.emplace_back(RunHarmony, surv, std::cref(base_config), std::ref(results), total_tests);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds" << std::endl;
    std::cout << std::endl;

    // Sort by survive_pct then by name
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.survive_pct != b.survive_pct) return a.survive_pct < b.survive_pct;
        return a.name < b.name;
    });

    // Print results
    std::cout << "=== Results ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(14) << "Strategy"
              << std::setw(8) << "Surv%"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Entries"
              << std::setw(12) << "TotalVol"
              << std::setw(10) << "MaxVol"
              << std::setw(12) << "SwapCost"
              << std::endl;
    std::cout << std::string(86, '-') << std::endl;

    double current_survive = -1;
    for (const auto& r : results) {
        if (r.survive_pct != current_survive) {
            if (current_survive > 0) std::cout << std::endl;
            current_survive = r.survive_pct;
        }

        std::cout << std::left << std::setw(14) << r.name
                  << std::setw(8) << r.survive_pct
                  << std::setw(10) << (std::to_string(r.return_mult).substr(0, 6) + "x")
                  << std::setw(10) << (std::to_string(r.max_dd).substr(0, 5) + "%")
                  << std::setw(10) << r.entries
                  << std::setw(12) << r.total_volume
                  << std::setw(10) << r.max_volume
                  << std::setw(12) << ("$" + std::to_string((int)r.swap_cost))
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== MT5 Reference (from your test) ===" << std::endl;
    std::cout << "UpWhileUp MT5 (12%): 208x return" << std::endl;
    std::cout << "FillUp MT5 (12%): 9.66x return" << std::endl;

    return 0;
}
