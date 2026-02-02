/**
 * FillUp/FillDown Hedge Strategy Test - Unified Margin Pool
 *
 * Tests Long Gold (FillUp) + Short Silver (FillDown) hedge with SHARED margin.
 * Both legs trade on the same account, so silver profits cover gold margin.
 */

#include "../include/multi_symbol_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <deque>
#include <cfloat>

using namespace backtest;

// Synchronized tick for both symbols
struct SyncedTick {
    std::string timestamp;
    Tick gold;
    Tick silver;
    bool has_gold = false;
    bool has_silver = false;
};

// Load ticks from file
std::vector<Tick> LoadTicks(const std::string& file_path) {
    std::vector<Tick> ticks;
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << file_path << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Tick tick;
        size_t pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);
        line = line.substr(pos + 1);

        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.bid = std::stod(line.substr(0, pos));
        line = line.substr(pos + 1);

        pos = line.find('\t');
        if (pos == std::string::npos) pos = line.length();
        tick.ask = std::stod(line.substr(0, pos));

        ticks.push_back(tick);
    }
    return ticks;
}

// Synchronize gold and silver ticks by timestamp (second-level precision)
std::vector<SyncedTick> SynchronizeTicks(
    const std::vector<Tick>& gold_ticks,
    const std::vector<Tick>& silver_ticks)
{
    std::vector<SyncedTick> synced;
    std::map<std::string, SyncedTick> tick_map;

    auto truncate_ts = [](const std::string& ts) -> std::string {
        if (ts.length() >= 19) return ts.substr(0, 19);
        return ts;
    };

    for (const auto& tick : gold_ticks) {
        std::string key = truncate_ts(tick.timestamp);
        auto& st = tick_map[key];
        st.timestamp = key;
        st.gold = tick;
        st.has_gold = true;
    }

    for (const auto& tick : silver_ticks) {
        std::string key = truncate_ts(tick.timestamp);
        auto& st = tick_map[key];
        st.timestamp = key;
        st.silver = tick;
        st.has_silver = true;
    }

    for (const auto& [key, st] : tick_map) {
        if (st.has_gold && st.has_silver) {
            synced.push_back(st);
        }
    }

    std::sort(synced.begin(), synced.end(),
        [](const SyncedTick& a, const SyncedTick& b) {
            return a.timestamp < b.timestamp;
        });

    return synced;
}

// Simple FillUp-style strategy for the multi-symbol engine
class UnifiedFillUp {
public:
    struct Config {
        double survive_pct = 15.0;
        double base_spacing = 1.5;
        double min_volume = 0.01;
        double max_volume = 5.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        bool adaptive_spacing = true;
        double volatility_lookback_hours = 4.0;
    };

    UnifiedFillUp(const Config& config, const std::string& symbol)
        : config_(config), symbol_(symbol), lowest_entry_(DBL_MAX), highest_entry_(DBL_MIN),
          current_spacing_(config.base_spacing), entries_(0) {}

    void OnTick(double bid, double ask, const std::string& timestamp, MultiSymbolEngine& engine) {
        // Update adaptive spacing
        price_history_.push_back(bid);
        size_t lookback = static_cast<size_t>(config_.volatility_lookback_hours * 3600 * 10);
        lookback = std::max(lookback, size_t(100));
        while (price_history_.size() > lookback) {
            price_history_.pop_front();
        }

        if (config_.adaptive_spacing && price_history_.size() >= 100) {
            double high = *std::max_element(price_history_.begin(), price_history_.end());
            double low = *std::min_element(price_history_.begin(), price_history_.end());
            double mid = (high + low) / 2.0;
            if (mid > 0) {
                double range_pct = (high - low) / mid * 100.0;
                double vol_ratio = range_pct / 0.55;  // typical vol
                current_spacing_ = config_.base_spacing * vol_ratio;
                current_spacing_ = std::max(current_spacing_, config_.base_spacing * 0.5);
                current_spacing_ = std::min(current_spacing_, config_.base_spacing * 3.0);
            }
        }

        // Track positions for this symbol
        auto positions = engine.GetPositionsForSymbol(symbol_);
        double volume = 0.0;
        lowest_entry_ = DBL_MAX;
        highest_entry_ = DBL_MIN;

        for (const auto* pos : positions) {
            if (pos->direction == TradeDirection::BUY) {
                volume += pos->lot_size;
                lowest_entry_ = std::min(lowest_entry_, pos->entry_price);
                highest_entry_ = std::max(highest_entry_, pos->entry_price);
            }
        }

        int pos_count = static_cast<int>(positions.size());

        // Calculate lot size based on survive percentage
        double lot = CalculateLotSize(ask, engine, volume, pos_count);

        // Entry logic
        if (pos_count == 0) {
            // First entry
            double tp = ask + current_spacing_;
            if (engine.OpenPosition(symbol_, TradeDirection::BUY, lot, 0.0, tp, timestamp)) {
                lowest_entry_ = ask;
                highest_entry_ = ask;
                entries_++;
            }
        } else {
            // Entry on dip below lowest
            if (ask <= lowest_entry_ - current_spacing_) {
                double tp = ask + current_spacing_;
                if (engine.OpenPosition(symbol_, TradeDirection::BUY, lot, 0.0, tp, timestamp)) {
                    lowest_entry_ = ask;
                    entries_++;
                }
            }
            // Entry on rally above highest
            else if (ask >= highest_entry_ + current_spacing_) {
                double tp = ask + current_spacing_;
                if (engine.OpenPosition(symbol_, TradeDirection::BUY, lot, 0.0, tp, timestamp)) {
                    highest_entry_ = ask;
                    entries_++;
                }
            }
        }
    }

    int GetEntries() const { return entries_; }
    double GetCurrentSpacing() const { return current_spacing_; }

private:
    Config config_;
    std::string symbol_;
    double lowest_entry_;
    double highest_entry_;
    double current_spacing_;
    int entries_;
    std::deque<double> price_history_;

    double CalculateLotSize(double price, MultiSymbolEngine& engine, double volume, int pos_count) {
        double equity = engine.GetEquity();
        double end_price = (pos_count == 0)
            ? price * ((100.0 - config_.survive_pct) / 100.0)
            : highest_entry_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = price - end_price;
        if (distance <= 0) distance = current_spacing_;

        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = equity - volume * distance * config_.contract_size;
        double used_margin = engine.GetUsedMargin();

        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < 20.0) {
            return config_.min_volume;  // Minimum lot
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

        double max_mult = config_.max_volume / config_.min_volume;
        double low = 1.0, high = max_mult;
        double best_mult = 1.0;

        while (high - low > 0.05) {
            double mid = (low + high) / 2.0;
            double test_equity = equity_at_target - mid * d_equity;
            double test_margin = used_margin + mid * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > 20.0) {
                best_mult = mid;
                low = mid;
            } else {
                high = mid;
            }
        }

        return std::min(best_mult * config_.min_volume, config_.max_volume);
    }
};

// Simple FillDown-style strategy for shorts
class UnifiedFillDown {
public:
    struct Config {
        double survive_pct = 20.0;
        double base_spacing_pct = 0.5;  // % of price
        double min_volume = 0.01;
        double max_volume = 5.0;
        double contract_size = 5000.0;
        double leverage = 500.0;
        bool adaptive_spacing = true;
        double volatility_lookback_hours = 4.0;
        double tp_multiplier = 1.0;     // TP = spacing * this multiplier (higher = wider TP)
    };

    UnifiedFillDown(const Config& config, const std::string& symbol)
        : config_(config), symbol_(symbol), lowest_entry_(DBL_MAX), highest_entry_(DBL_MIN),
          current_spacing_(0.0), entries_(0) {}

    void OnTick(double bid, double ask, const std::string& timestamp, MultiSymbolEngine& engine) {
        // Initialize spacing from price
        if (current_spacing_ == 0.0) {
            current_spacing_ = bid * (config_.base_spacing_pct / 100.0);
        }

        // Update adaptive spacing
        price_history_.push_back(bid);
        size_t lookback = static_cast<size_t>(config_.volatility_lookback_hours * 3600 * 10);
        lookback = std::max(lookback, size_t(100));
        while (price_history_.size() > lookback) {
            price_history_.pop_front();
        }

        if (config_.adaptive_spacing && price_history_.size() >= 100) {
            double high = *std::max_element(price_history_.begin(), price_history_.end());
            double low = *std::min_element(price_history_.begin(), price_history_.end());
            double mid = (high + low) / 2.0;
            if (mid > 0) {
                double range_pct = (high - low) / mid * 100.0;
                double vol_ratio = range_pct / 0.45;  // typical vol for silver
                double base = bid * (config_.base_spacing_pct / 100.0);
                current_spacing_ = base * vol_ratio;
                current_spacing_ = std::max(current_spacing_, base * 0.5);
                current_spacing_ = std::min(current_spacing_, base * 3.0);
            }
        }

        // Track positions for this symbol
        auto positions = engine.GetPositionsForSymbol(symbol_);
        double volume = 0.0;
        lowest_entry_ = DBL_MAX;
        highest_entry_ = DBL_MIN;

        for (const auto* pos : positions) {
            if (pos->direction == TradeDirection::SELL) {
                volume += pos->lot_size;
                lowest_entry_ = std::min(lowest_entry_, pos->entry_price);
                highest_entry_ = std::max(highest_entry_, pos->entry_price);
            }
        }

        int pos_count = static_cast<int>(positions.size());

        // Calculate lot size
        double lot = CalculateLotSize(bid, engine, volume, pos_count);

        // Entry logic for shorts
        // TP distance = spacing * tp_multiplier (wider TP keeps positions open longer)
        double tp_distance = current_spacing_ * config_.tp_multiplier;

        if (pos_count == 0) {
            // First entry
            double tp = bid - tp_distance;
            if (tp > 0 && engine.OpenPosition(symbol_, TradeDirection::SELL, lot, 0.0, tp, timestamp)) {
                lowest_entry_ = bid;
                highest_entry_ = bid;
                entries_++;
            }
        } else {
            // Entry on rally above highest (sell higher)
            if (bid >= highest_entry_ + current_spacing_) {
                double tp = bid - tp_distance;
                if (tp > 0 && engine.OpenPosition(symbol_, TradeDirection::SELL, lot, 0.0, tp, timestamp)) {
                    highest_entry_ = bid;
                    entries_++;
                }
            }
            // Entry on dip below lowest (sell lower too, filling the grid)
            else if (bid <= lowest_entry_ - current_spacing_) {
                double tp = bid - tp_distance;
                if (tp > 0 && engine.OpenPosition(symbol_, TradeDirection::SELL, lot, 0.0, tp, timestamp)) {
                    lowest_entry_ = bid;
                    entries_++;
                }
            }
        }
    }

    int GetEntries() const { return entries_; }
    double GetCurrentSpacing() const { return current_spacing_; }

private:
    Config config_;
    std::string symbol_;
    double lowest_entry_;
    double highest_entry_;
    double current_spacing_;
    int entries_;
    std::deque<double> price_history_;

    double CalculateLotSize(double price, MultiSymbolEngine& engine, double volume, int pos_count) {
        double equity = engine.GetEquity();

        // For shorts: survive a price RISE
        double end_price = (pos_count == 0)
            ? price * ((100.0 + config_.survive_pct) / 100.0)
            : lowest_entry_ * ((100.0 + config_.survive_pct) / 100.0);

        double distance = end_price - price;
        if (distance <= 0) distance = current_spacing_;

        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = equity - volume * distance * config_.contract_size;
        double used_margin = engine.GetUsedMargin();

        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < 20.0) {
            return config_.min_volume;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

        double max_mult = config_.max_volume / config_.min_volume;
        double low = 1.0, high = max_mult;
        double best_mult = 1.0;

        while (high - low > 0.05) {
            double mid = (low + high) / 2.0;
            double test_equity = equity_at_target - mid * d_equity;
            double test_margin = used_margin + mid * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > 20.0) {
                best_mult = mid;
                low = mid;
            } else {
                high = mid;
            }
        }

        return std::min(best_mult * config_.min_volume, config_.max_volume);
    }
};

struct HedgeResult {
    std::string mode_name;
    double initial_balance;
    double final_equity;
    double max_dd_pct;
    double peak_equity;
    int gold_entries;
    int silver_entries;
    int total_trades;
    double return_multiple;
    bool survived;
};

// Extended test with configurable spacing
HedgeResult RunSpacingTest(
    const std::vector<SyncedTick>& ticks,
    const std::string& mode_name,
    double gold_spacing,           // Absolute $ spacing for gold
    double silver_spacing_pct,     // % spacing for silver
    double gold_max_volume,
    double silver_max_volume,
    double gold_survive_pct,
    double silver_survive_pct,
    double initial_balance,
    double silver_tp_mult = 1.0,   // TP multiplier for silver (wider TP = longer hold)
    bool verbose = false)
{
    HedgeResult result;
    result.mode_name = mode_name;
    result.initial_balance = initial_balance;

    // Create unified engine
    MultiSymbolEngine::Config engine_config;
    engine_config.initial_balance = initial_balance;
    engine_config.leverage = 500.0;
    engine_config.margin_stop_out = 20.0;
    engine_config.verbose = false;

    MultiSymbolEngine engine(engine_config);

    // Add symbols
    SymbolConfig gold_sym;
    gold_sym.symbol = "XAUUSD";
    gold_sym.contract_size = 100.0;
    gold_sym.pip_size = 0.01;
    gold_sym.swap_long = -78.57;
    gold_sym.swap_short = 39.14;
    engine.AddSymbol(gold_sym);

    SymbolConfig silver_sym;
    silver_sym.symbol = "XAGUSD";
    silver_sym.contract_size = 5000.0;
    silver_sym.pip_size = 0.001;
    silver_sym.swap_long = -22.34;
    silver_sym.swap_short = 0.13;
    engine.AddSymbol(silver_sym);

    // Create strategies with configurable spacing
    UnifiedFillUp::Config gold_config;
    gold_config.survive_pct = gold_survive_pct;
    gold_config.base_spacing = gold_spacing;       // Use provided spacing
    gold_config.min_volume = 0.01;
    gold_config.max_volume = gold_max_volume;
    gold_config.contract_size = 100.0;
    gold_config.leverage = 500.0;
    gold_config.adaptive_spacing = false;          // Disable adaptive for controlled test

    UnifiedFillDown::Config silver_config;
    silver_config.survive_pct = silver_survive_pct;
    silver_config.base_spacing_pct = silver_spacing_pct;  // Use provided % spacing
    silver_config.min_volume = 0.01;
    silver_config.max_volume = silver_max_volume;
    silver_config.contract_size = 5000.0;
    silver_config.leverage = 500.0;
    silver_config.adaptive_spacing = false;        // Disable adaptive for controlled test
    silver_config.tp_multiplier = silver_tp_mult;  // Wider TP keeps shorts open longer

    UnifiedFillUp gold_strategy(gold_config, "XAUUSD");
    UnifiedFillDown silver_strategy(silver_config, "XAGUSD");

    // Process ticks
    int tick_count = 0;
    for (const auto& st : ticks) {
        // Update prices
        engine.UpdatePrice("XAUUSD", st.gold.bid, st.gold.ask);
        engine.UpdatePrice("XAGUSD", st.silver.bid, st.silver.ask);

        // Run strategies
        gold_strategy.OnTick(st.gold.bid, st.gold.ask, st.timestamp, engine);
        silver_strategy.OnTick(st.silver.bid, st.silver.ask, st.timestamp, engine);

        tick_count++;
        if (verbose && tick_count % 50000 == 0) {
            std::cout << "    " << tick_count << " ticks, Equity: $" << std::fixed << std::setprecision(2)
                      << engine.GetEquity() << ", DD: " << engine.GetMaxDrawdownPct() << "%"
                      << ", Gold pos: " << engine.GetPositionsForSymbol("XAUUSD").size()
                      << ", Silver pos: " << engine.GetPositionsForSymbol("XAGUSD").size() << std::endl;
        }
    }

    // Collect results
    auto engine_results = engine.GetResults();

    result.final_equity = engine_results.final_equity;
    result.max_dd_pct = engine_results.max_drawdown_pct;
    result.peak_equity = engine_results.peak_equity;
    result.gold_entries = gold_strategy.GetEntries();
    result.silver_entries = silver_strategy.GetEntries();
    result.total_trades = engine_results.total_trades;
    result.return_multiple = result.final_equity / initial_balance;
    result.survived = (result.final_equity > 0);

    return result;
}

HedgeResult RunUnifiedHedgeTest(
    const std::vector<SyncedTick>& ticks,
    const std::string& mode_name,
    double gold_max_volume,
    double silver_max_volume,
    double gold_survive_pct,
    double silver_survive_pct,
    double initial_balance)
{
    HedgeResult result;
    result.mode_name = mode_name;
    result.initial_balance = initial_balance;

    // Create unified engine
    MultiSymbolEngine::Config engine_config;
    engine_config.initial_balance = initial_balance;
    engine_config.leverage = 500.0;
    engine_config.margin_stop_out = 20.0;
    engine_config.verbose = false;

    MultiSymbolEngine engine(engine_config);

    // Add symbols
    SymbolConfig gold_sym;
    gold_sym.symbol = "XAUUSD";
    gold_sym.contract_size = 100.0;
    gold_sym.pip_size = 0.01;
    gold_sym.swap_long = -78.57;
    gold_sym.swap_short = 39.14;
    engine.AddSymbol(gold_sym);

    SymbolConfig silver_sym;
    silver_sym.symbol = "XAGUSD";
    silver_sym.contract_size = 5000.0;
    silver_sym.pip_size = 0.001;
    silver_sym.swap_long = -22.34;
    silver_sym.swap_short = 0.13;
    engine.AddSymbol(silver_sym);

    // Create strategies
    UnifiedFillUp::Config gold_config;
    gold_config.survive_pct = gold_survive_pct;
    gold_config.base_spacing = 1.5;
    gold_config.min_volume = 0.01;
    gold_config.max_volume = gold_max_volume;
    gold_config.contract_size = 100.0;
    gold_config.leverage = 500.0;

    UnifiedFillDown::Config silver_config;
    silver_config.survive_pct = silver_survive_pct;
    silver_config.base_spacing_pct = 0.5;
    silver_config.min_volume = 0.01;
    silver_config.max_volume = silver_max_volume;
    silver_config.contract_size = 5000.0;
    silver_config.leverage = 500.0;

    UnifiedFillUp gold_strategy(gold_config, "XAUUSD");
    UnifiedFillDown silver_strategy(silver_config, "XAGUSD");

    // Process ticks
    int tick_count = 0;
    for (const auto& st : ticks) {
        // Update prices
        engine.UpdatePrice("XAUUSD", st.gold.bid, st.gold.ask);
        engine.UpdatePrice("XAGUSD", st.silver.bid, st.silver.ask);

        // Run strategies
        gold_strategy.OnTick(st.gold.bid, st.gold.ask, st.timestamp, engine);
        silver_strategy.OnTick(st.silver.bid, st.silver.ask, st.timestamp, engine);

        tick_count++;
        if (tick_count % 100000 == 0) {
            std::cout << "  " << tick_count << " ticks, Equity: $" << std::fixed << std::setprecision(2)
                      << engine.GetEquity() << ", DD: " << engine.GetMaxDrawdownPct() << "%" << std::endl;
        }
    }

    // Collect results
    auto engine_results = engine.GetResults();

    result.final_equity = engine_results.final_equity;
    result.max_dd_pct = engine_results.max_drawdown_pct;
    result.peak_equity = engine_results.peak_equity;
    result.gold_entries = gold_strategy.GetEntries();
    result.silver_entries = silver_strategy.GetEntries();
    result.total_trades = engine_results.total_trades;
    result.return_multiple = result.final_equity / initial_balance;
    result.survived = (result.final_equity > 0);

    return result;
}

void PrintResult(const HedgeResult& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== " << r.mode_name << " ===" << std::endl;
    std::cout << "Initial:  $" << r.initial_balance << std::endl;
    std::cout << "Final:    $" << r.final_equity << " (" << r.return_multiple << "x)" << std::endl;
    std::cout << "Peak:     $" << r.peak_equity << std::endl;
    std::cout << "Max DD:   " << r.max_dd_pct << "%" << std::endl;
    std::cout << "Entries:  Gold=" << r.gold_entries << " Silver=" << r.silver_entries
              << " Total=" << r.total_trades << std::endl;
    std::cout << "Survived: " << (r.survived ? "YES" : "NO") << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "==================================================" << std::endl;
    std::cout << "   FillUp/FillDown Unified Hedge Test" << std::endl;
    std::cout << "   Long Gold + Short Silver (Shared Margin Pool)" << std::endl;
    std::cout << "==================================================" << std::endl;

    bool use_full_2025 = false;
    if (argc > 1 && std::string(argv[1]) == "full") {
        use_full_2025 = true;
    }

    // Load tick data
    std::cout << "\nLoading gold ticks..." << std::endl;
    std::string gold_path = use_full_2025
        ? "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv"
        : "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_RECENT.csv";
    auto gold_ticks = LoadTicks(gold_path);
    std::cout << "Loaded " << gold_ticks.size() << " gold ticks" << std::endl;

    std::cout << "Loading silver ticks..." << std::endl;
    std::string silver_path = use_full_2025
        ? "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_MT5_EXPORT.csv"
        : "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_RECENT.csv";
    auto silver_ticks = LoadTicks(silver_path);
    std::cout << "Loaded " << silver_ticks.size() << " silver ticks" << std::endl;

    std::cout << "Synchronizing ticks..." << std::endl;
    auto synced = SynchronizeTicks(gold_ticks, silver_ticks);
    std::cout << "Synchronized " << synced.size() << " tick pairs" << std::endl;

    if (synced.empty()) {
        std::cerr << "No synchronized ticks!" << std::endl;
        return 1;
    }

    // Show price ranges
    double gold_start = synced.front().gold.bid;
    double gold_end = synced.back().gold.bid;
    double silver_start = synced.front().silver.bid;
    double silver_end = synced.back().silver.bid;

    std::cout << "\nPrice range:" << std::endl;
    std::cout << "Gold:   $" << gold_start << " -> $" << gold_end
              << " (" << ((gold_end - gold_start) / gold_start * 100) << "%)" << std::endl;
    std::cout << "Silver: $" << silver_start << " -> $" << silver_end
              << " (" << ((silver_end - silver_start) / silver_start * 100) << "%)" << std::endl;
    std::cout << "Period: " << synced.front().timestamp << " to " << synced.back().timestamp << std::endl;

    double initial_balance = 10000.0;
    std::vector<HedgeResult> results;

    // ============================================================
    // NEW SPACING TESTS: Adjust spacing to balance entry counts
    // ============================================================
    // Problem: Gold had 1124 entries, Silver only 17
    // Solution: Wider gold spacing, tighter silver spacing
    //
    // Current gold spacing ~$1.50 at $5000 = 0.03%
    // Current silver spacing ~0.5% at $100 = $0.50
    //
    // If gold drops 13% ($704) with $1.50 spacing = ~469 entries
    // If silver drops 36% ($42) with $0.50 spacing = ~84 entries
    //   But silver TPs close shorts, so only 17 remain
    //
    // To balance:
    // - Gold: Use much wider spacing (e.g., $10-50) = fewer entries
    // - Silver: Use tighter spacing (e.g., 0.2-0.3%) = more entries
    // ============================================================

    std::cout << "\n=== SPACING ADJUSTMENT TESTS ===" << std::endl;
    std::cout << "Testing different spacing configurations to balance entries\n" << std::endl;

    // ============================================================
    // WIDER TP TESTS
    // The problem: Silver shorts close via TP before crash completes
    // Solution: Use wider TP (tp_multiplier > 1) to keep shorts open
    // ============================================================

    // Test 1: Balanced spacing, TP=2x (shorts need 2x spacing to close)
    std::cout << "--- Test 1: Gold=$15, Silver=0.3%, TP=2x ---" << std::endl;
    results.push_back(RunSpacingTest(synced, "Gold=$15, Ag=0.3%, TP=2x",
        15.0, 0.3,     // gold $15, silver 0.3%
        5.0, 5.0,      // max volumes
        15.0, 20.0,    // survive percentages
        initial_balance,
        2.0,           // TP multiplier = 2x (wider TP)
        true));

    // Test 2: Wider silver spacing, TP=3x
    std::cout << "\n--- Test 2: Gold=$20, Silver=0.5%, TP=3x ---" << std::endl;
    results.push_back(RunSpacingTest(synced, "Gold=$20, Ag=0.5%, TP=3x",
        20.0, 0.5,     // gold $20, silver 0.5%
        5.0, 5.0,      // max volumes
        15.0, 20.0,    // survive percentages
        initial_balance,
        3.0,           // TP multiplier = 3x
        true));

    // Test 3: Very wide gold, moderate silver, TP=5x
    std::cout << "\n--- Test 3: Gold=$30, Silver=0.4%, TP=5x ---" << std::endl;
    results.push_back(RunSpacingTest(synced, "Gold=$30, Ag=0.4%, TP=5x",
        30.0, 0.4,     // gold $30, silver 0.4%
        5.0, 5.0,      // max volumes
        18.0, 25.0,    // survive percentages (higher to reduce size)
        initial_balance,
        5.0,           // TP multiplier = 5x
        true));

    // Test 4: Conservative - few entries both sides, TP=10x (hold almost forever)
    std::cout << "\n--- Test 4: Gold=$50, Silver=1.0%, TP=10x ---" << std::endl;
    results.push_back(RunSpacingTest(synced, "Gold=$50, Ag=1.0%, TP=10x",
        50.0, 1.0,     // gold $50, silver 1.0%
        3.0, 3.0,      // lower max volumes
        20.0, 25.0,    // higher survive pct
        initial_balance,
        10.0,          // TP multiplier = 10x (very wide - hold during crash)
        true));

    // Test 5: Original spacing with TP=5x only
    std::cout << "\n--- Test 5: Gold=$10, Silver=0.3%, TP=5x ---" << std::endl;
    results.push_back(RunSpacingTest(synced, "Gold=$10, Ag=0.3%, TP=5x",
        10.0, 0.3,     // original moderate spacing
        5.0, 5.0,      // max volumes
        15.0, 20.0,    // survive percentages
        initial_balance,
        5.0,           // TP multiplier = 5x
        true));

    // Test 6: No TP at all (TP=0 means no TP)
    // Note: we can't use 0, but we can use a very large multiplier
    std::cout << "\n--- Test 6: Gold=$20, Silver=0.4%, TP=100x (effectively no TP) ---" << std::endl;
    results.push_back(RunSpacingTest(synced, "Gold=$20, Ag=0.4%, NO_TP",
        20.0, 0.4,     // moderate spacing
        5.0, 5.0,      // max volumes
        18.0, 22.0,    // survive percentages
        initial_balance,
        100.0,         // TP multiplier = 100x (effectively no TP)
        true));

    // Print summary
    std::cout << "\n\n========================================" << std::endl;
    std::cout << "           RESULTS SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;

    for (const auto& r : results) {
        PrintResult(r);
    }

    // Find best
    std::cout << "\n\n=== BEST RESULTS ===" << std::endl;

    // Sort by survival first, then by return
    std::vector<const HedgeResult*> sorted_results;
    for (const auto& r : results) sorted_results.push_back(&r);

    std::sort(sorted_results.begin(), sorted_results.end(),
        [](const HedgeResult* a, const HedgeResult* b) {
            if (a->survived != b->survived) return a->survived > b->survived;
            return a->return_multiple > b->return_multiple;
        });

    std::cout << "\nRanked by survival and return:" << std::endl;
    int rank = 1;
    for (const auto* r : sorted_results) {
        std::cout << rank++ << ". " << r->mode_name << ": "
                  << (r->survived ? "SURVIVED" : "STOPPED OUT")
                  << " | " << std::fixed << std::setprecision(2) << r->return_multiple << "x"
                  << " | DD: " << r->max_dd_pct << "%"
                  << " | Entries: " << r->gold_entries << "/" << r->silver_entries
                  << std::endl;
    }

    // Analysis
    std::cout << "\n=== ENTRY RATIO ANALYSIS ===" << std::endl;
    for (const auto& r : results) {
        double ratio = (r.silver_entries > 0) ? (double)r.gold_entries / r.silver_entries : 9999;
        std::cout << r.mode_name << ": Gold/Silver = " << std::fixed << std::setprecision(1)
                  << ratio << ":1" << std::endl;
    }

    return 0;
}
