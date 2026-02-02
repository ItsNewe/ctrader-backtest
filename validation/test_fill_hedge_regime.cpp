/**
 * FillUp/FillDown Hedge Strategy Test - REGIME AWARE
 *
 * Addresses the fundamental problem: shorting silver during a 152% rally is catastrophic.
 *
 * Solution 1: Regime Detection
 *   - Only run FillDown (shorts) when market is bearish
 *   - Detect regime using SMA crossover or price vs SMA
 *
 * Solution 2: Adaptive Direction
 *   - Bullish regime: Long gold + Long silver (both FillUp)
 *   - Bearish regime: Long gold + Short silver (FillUp + FillDown)
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
#include <numeric>

using namespace backtest;

// ============================================================
// REGIME DETECTOR
// ============================================================
enum class Regime { BULLISH, BEARISH, NEUTRAL };

struct RegimeConfig {
    int fast_period = 50;       // Fast SMA period (in ticks)
    int slow_period = 200;      // Slow SMA period
    double trend_threshold = 0.5;  // % above/below SMA to confirm trend
    int confirmation_ticks = 1000; // Ticks to confirm regime change
};

class RegimeDetector {
public:
    using Config = RegimeConfig;

    explicit RegimeDetector(const Config& config = Config())
        : config_(config), current_regime_(Regime::NEUTRAL),
          ticks_in_regime_(0), regime_changes_(0) {}

    void OnTick(double price) {
        prices_.push_back(price);

        // Keep only what we need
        size_t max_needed = static_cast<size_t>(config_.slow_period + config_.confirmation_ticks);
        while (prices_.size() > max_needed) {
            prices_.pop_front();
        }

        // Need enough data
        if (prices_.size() < static_cast<size_t>(config_.slow_period)) {
            return;
        }

        // Calculate SMAs
        double fast_sma = CalculateSMA(config_.fast_period);
        double slow_sma = CalculateSMA(config_.slow_period);

        fast_sma_ = fast_sma;
        slow_sma_ = slow_sma;

        // Determine regime
        Regime detected = Regime::NEUTRAL;

        // Fast above slow = bullish, fast below slow = bearish
        double sma_diff_pct = (fast_sma - slow_sma) / slow_sma * 100.0;

        if (sma_diff_pct > config_.trend_threshold) {
            detected = Regime::BULLISH;
        } else if (sma_diff_pct < -config_.trend_threshold) {
            detected = Regime::BEARISH;
        }

        // Confirmation logic
        if (detected == pending_regime_) {
            ticks_in_pending_++;
            if (ticks_in_pending_ >= config_.confirmation_ticks && detected != current_regime_) {
                current_regime_ = detected;
                regime_changes_++;
                ticks_in_regime_ = 0;
            }
        } else {
            pending_regime_ = detected;
            ticks_in_pending_ = 1;
        }

        ticks_in_regime_++;
    }

    Regime GetRegime() const { return current_regime_; }
    int GetRegimeChanges() const { return regime_changes_; }
    double GetFastSMA() const { return fast_sma_; }
    double GetSlowSMA() const { return slow_sma_; }

    std::string GetRegimeString() const {
        switch (current_regime_) {
            case Regime::BULLISH: return "BULLISH";
            case Regime::BEARISH: return "BEARISH";
            default: return "NEUTRAL";
        }
    }

private:
    Config config_;
    std::deque<double> prices_;
    Regime current_regime_;
    Regime pending_regime_ = Regime::NEUTRAL;
    int ticks_in_pending_ = 0;
    int ticks_in_regime_;
    int regime_changes_;
    double fast_sma_ = 0.0;
    double slow_sma_ = 0.0;

    double CalculateSMA(int period) const {
        if (prices_.size() < static_cast<size_t>(period)) return 0.0;
        double sum = 0.0;
        auto it = prices_.end() - period;
        for (int i = 0; i < period; ++i, ++it) {
            sum += *it;
        }
        return sum / period;
    }
};

// ============================================================
// SYNCHRONIZED TICK
// ============================================================
struct SyncedTick {
    std::string timestamp;
    Tick gold;
    Tick silver;
    bool has_gold = false;
    bool has_silver = false;
};

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

// ============================================================
// REGIME-AWARE FILLUP (can trade in either direction)
// ============================================================
class RegimeAwareFill {
public:
    struct Config {
        double survive_pct = 15.0;
        double base_spacing_pct = 0.3;  // % of price
        double min_volume = 0.01;
        double max_volume = 5.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double tp_multiplier = 5.0;     // TP = spacing * multiplier
    };

    RegimeAwareFill(const Config& config, const std::string& symbol)
        : config_(config), symbol_(symbol), lowest_entry_(DBL_MAX), highest_entry_(DBL_MIN),
          current_spacing_(0.0), entries_(0), current_direction_(TradeDirection::BUY) {}

    void SetDirection(TradeDirection dir) {
        // Only change direction if we have no positions
        // (in real implementation, would need to close existing positions first)
        current_direction_ = dir;
    }

    TradeDirection GetDirection() const { return current_direction_; }

    void OnTick(double bid, double ask, const std::string& timestamp,
                MultiSymbolEngine& engine, bool allow_new_entries = true) {

        // Initialize spacing from price
        if (current_spacing_ == 0.0) {
            current_spacing_ = bid * (config_.base_spacing_pct / 100.0);
        }

        // Track positions for this symbol
        auto positions = engine.GetPositionsForSymbol(symbol_);
        double volume = 0.0;
        lowest_entry_ = DBL_MAX;
        highest_entry_ = DBL_MIN;

        for (const auto* pos : positions) {
            volume += pos->lot_size;
            lowest_entry_ = std::min(lowest_entry_, pos->entry_price);
            highest_entry_ = std::max(highest_entry_, pos->entry_price);
        }

        int pos_count = static_cast<int>(positions.size());

        if (!allow_new_entries) return;

        // Calculate lot size
        double lot = CalculateLotSize(bid, ask, engine, volume, pos_count);

        // TP distance
        double tp_distance = current_spacing_ * config_.tp_multiplier;

        if (current_direction_ == TradeDirection::BUY) {
            // FillUp logic (longs)
            if (pos_count == 0) {
                double tp = ask + tp_distance;
                if (engine.OpenPosition(symbol_, TradeDirection::BUY, lot, 0.0, tp, timestamp)) {
                    lowest_entry_ = ask;
                    highest_entry_ = ask;
                    entries_++;
                }
            } else {
                // Entry on dip below lowest
                if (ask <= lowest_entry_ - current_spacing_) {
                    double tp = ask + tp_distance;
                    if (engine.OpenPosition(symbol_, TradeDirection::BUY, lot, 0.0, tp, timestamp)) {
                        lowest_entry_ = ask;
                        entries_++;
                    }
                }
                // Entry on rally above highest
                else if (ask >= highest_entry_ + current_spacing_) {
                    double tp = ask + tp_distance;
                    if (engine.OpenPosition(symbol_, TradeDirection::BUY, lot, 0.0, tp, timestamp)) {
                        highest_entry_ = ask;
                        entries_++;
                    }
                }
            }
        } else {
            // FillDown logic (shorts)
            if (pos_count == 0) {
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
                // Entry on dip below lowest
                else if (bid <= lowest_entry_ - current_spacing_) {
                    double tp = bid - tp_distance;
                    if (tp > 0 && engine.OpenPosition(symbol_, TradeDirection::SELL, lot, 0.0, tp, timestamp)) {
                        lowest_entry_ = bid;
                        entries_++;
                    }
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
    TradeDirection current_direction_;

    double CalculateLotSize(double bid, double ask, MultiSymbolEngine& engine,
                           double volume, int pos_count) {
        double equity = engine.GetEquity();
        double price = (current_direction_ == TradeDirection::BUY) ? ask : bid;

        double end_price;
        if (current_direction_ == TradeDirection::BUY) {
            // Survive a drop
            end_price = (pos_count == 0)
                ? price * ((100.0 - config_.survive_pct) / 100.0)
                : highest_entry_ * ((100.0 - config_.survive_pct) / 100.0);
        } else {
            // Survive a rise
            end_price = (pos_count == 0)
                ? price * ((100.0 + config_.survive_pct) / 100.0)
                : lowest_entry_ * ((100.0 + config_.survive_pct) / 100.0);
        }

        double distance = std::abs(price - end_price);
        if (distance <= 0) distance = current_spacing_;

        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = equity - volume * distance * config_.contract_size;
        double used_margin = engine.GetUsedMargin();

        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < 20.0) {
            return config_.min_volume;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ *
                         (number_of_trades * (number_of_trades + 1) / 2);
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

// ============================================================
// RESULTS
// ============================================================
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
    int regime_changes;
    int bullish_ticks;
    int bearish_ticks;
};

// ============================================================
// TEST FUNCTIONS
// ============================================================

// Mode 1: Regime-controlled shorts (only short silver in bearish regime)
HedgeResult RunRegimeControlledTest(
    const std::vector<SyncedTick>& ticks,
    const std::string& mode_name,
    double gold_spacing_pct,
    double silver_spacing_pct,
    double max_volume,
    double tp_multiplier,
    double initial_balance,
    const RegimeDetector::Config& regime_config,
    bool verbose = false)
{
    HedgeResult result;
    result.mode_name = mode_name;
    result.initial_balance = initial_balance;
    result.bullish_ticks = 0;
    result.bearish_ticks = 0;

    // Create engine
    MultiSymbolEngine::Config engine_config;
    engine_config.initial_balance = initial_balance;
    engine_config.leverage = 500.0;
    engine_config.margin_stop_out = 20.0;

    MultiSymbolEngine engine(engine_config);

    // Add symbols
    SymbolConfig gold_sym;
    gold_sym.symbol = "XAUUSD";
    gold_sym.contract_size = 100.0;
    gold_sym.pip_size = 0.01;
    engine.AddSymbol(gold_sym);

    SymbolConfig silver_sym;
    silver_sym.symbol = "XAGUSD";
    silver_sym.contract_size = 5000.0;
    silver_sym.pip_size = 0.001;
    engine.AddSymbol(silver_sym);

    // Create strategies
    RegimeAwareFill::Config gold_config;
    gold_config.survive_pct = 15.0;
    gold_config.base_spacing_pct = gold_spacing_pct;
    gold_config.max_volume = max_volume;
    gold_config.contract_size = 100.0;
    gold_config.tp_multiplier = tp_multiplier;

    RegimeAwareFill::Config silver_config;
    silver_config.survive_pct = 20.0;
    silver_config.base_spacing_pct = silver_spacing_pct;
    silver_config.max_volume = max_volume;
    silver_config.contract_size = 5000.0;
    silver_config.tp_multiplier = tp_multiplier;

    RegimeAwareFill gold_strategy(gold_config, "XAUUSD");
    RegimeAwareFill silver_strategy(silver_config, "XAGUSD");

    // Gold is always long
    gold_strategy.SetDirection(TradeDirection::BUY);
    // Silver starts neutral (no trades until regime is determined)
    silver_strategy.SetDirection(TradeDirection::SELL);

    // Regime detector on silver (detect silver's trend)
    RegimeDetector silver_regime(regime_config);

    // Process ticks
    int tick_count = 0;
    for (const auto& st : ticks) {
        // Update regime detector with silver price
        silver_regime.OnTick(st.silver.bid);
        Regime regime = silver_regime.GetRegime();

        if (regime == Regime::BULLISH) result.bullish_ticks++;
        else if (regime == Regime::BEARISH) result.bearish_ticks++;

        // Update prices
        engine.UpdatePrice("XAUUSD", st.gold.bid, st.gold.ask);
        engine.UpdatePrice("XAGUSD", st.silver.bid, st.silver.ask);

        // Gold always trades (long)
        gold_strategy.OnTick(st.gold.bid, st.gold.ask, st.timestamp, engine, true);

        // Silver only trades shorts in bearish regime
        bool allow_silver = (regime == Regime::BEARISH);
        silver_strategy.OnTick(st.silver.bid, st.silver.ask, st.timestamp, engine, allow_silver);

        tick_count++;
        if (verbose && tick_count % 500000 == 0) {
            std::cout << "    " << tick_count << " ticks, Equity: $" << std::fixed << std::setprecision(2)
                      << engine.GetEquity() << ", DD: " << engine.GetMaxDrawdownPct() << "%"
                      << ", Regime: " << silver_regime.GetRegimeString()
                      << ", Gold pos: " << engine.GetPositionsForSymbol("XAUUSD").size()
                      << ", Silver pos: " << engine.GetPositionsForSymbol("XAGUSD").size() << std::endl;
        }
    }

    // Results
    auto engine_results = engine.GetResults();
    result.final_equity = engine_results.final_equity;
    result.max_dd_pct = engine_results.max_drawdown_pct;
    result.peak_equity = engine_results.peak_equity;
    result.gold_entries = gold_strategy.GetEntries();
    result.silver_entries = silver_strategy.GetEntries();
    result.total_trades = engine_results.total_trades;
    result.return_multiple = result.final_equity / initial_balance;
    result.survived = (result.final_equity > 0);
    result.regime_changes = silver_regime.GetRegimeChanges();

    return result;
}

// Mode 2: Adaptive direction (long both in bullish, hedge in bearish)
HedgeResult RunAdaptiveDirectionTest(
    const std::vector<SyncedTick>& ticks,
    const std::string& mode_name,
    double gold_spacing_pct,
    double silver_spacing_pct,
    double max_volume,
    double tp_multiplier,
    double initial_balance,
    const RegimeDetector::Config& regime_config,
    bool verbose = false)
{
    HedgeResult result;
    result.mode_name = mode_name;
    result.initial_balance = initial_balance;
    result.bullish_ticks = 0;
    result.bearish_ticks = 0;

    // Create engine
    MultiSymbolEngine::Config engine_config;
    engine_config.initial_balance = initial_balance;
    engine_config.leverage = 500.0;
    engine_config.margin_stop_out = 20.0;

    MultiSymbolEngine engine(engine_config);

    // Add symbols
    SymbolConfig gold_sym;
    gold_sym.symbol = "XAUUSD";
    gold_sym.contract_size = 100.0;
    engine.AddSymbol(gold_sym);

    SymbolConfig silver_sym;
    silver_sym.symbol = "XAGUSD";
    silver_sym.contract_size = 5000.0;
    engine.AddSymbol(silver_sym);

    // Create strategies
    RegimeAwareFill::Config gold_config;
    gold_config.survive_pct = 15.0;
    gold_config.base_spacing_pct = gold_spacing_pct;
    gold_config.max_volume = max_volume;
    gold_config.contract_size = 100.0;
    gold_config.tp_multiplier = tp_multiplier;

    RegimeAwareFill::Config silver_config;
    silver_config.survive_pct = 20.0;
    silver_config.base_spacing_pct = silver_spacing_pct;
    silver_config.max_volume = max_volume;
    silver_config.contract_size = 5000.0;
    silver_config.tp_multiplier = tp_multiplier;

    RegimeAwareFill gold_strategy(gold_config, "XAUUSD");
    RegimeAwareFill silver_strategy(silver_config, "XAGUSD");

    // Both start as longs
    gold_strategy.SetDirection(TradeDirection::BUY);
    silver_strategy.SetDirection(TradeDirection::BUY);

    // Regime detector on silver
    RegimeDetector silver_regime(regime_config);

    Regime last_regime = Regime::NEUTRAL;

    // Process ticks
    int tick_count = 0;
    for (const auto& st : ticks) {
        // Update regime detector
        silver_regime.OnTick(st.silver.bid);
        Regime regime = silver_regime.GetRegime();

        if (regime == Regime::BULLISH) result.bullish_ticks++;
        else if (regime == Regime::BEARISH) result.bearish_ticks++;

        // Adaptive direction change (only when no positions)
        if (regime != last_regime) {
            auto silver_positions = engine.GetPositionsForSymbol("XAGUSD");
            if (silver_positions.empty()) {
                if (regime == Regime::BULLISH) {
                    silver_strategy.SetDirection(TradeDirection::BUY);  // Long silver in bull
                } else if (regime == Regime::BEARISH) {
                    silver_strategy.SetDirection(TradeDirection::SELL); // Short silver in bear
                }
                last_regime = regime;
            }
        }

        // Update prices
        engine.UpdatePrice("XAUUSD", st.gold.bid, st.gold.ask);
        engine.UpdatePrice("XAGUSD", st.silver.bid, st.silver.ask);

        // Both always trade
        gold_strategy.OnTick(st.gold.bid, st.gold.ask, st.timestamp, engine, true);
        silver_strategy.OnTick(st.silver.bid, st.silver.ask, st.timestamp, engine, true);

        tick_count++;
        if (verbose && tick_count % 500000 == 0) {
            std::string silver_dir = (silver_strategy.GetDirection() == TradeDirection::BUY) ? "LONG" : "SHORT";
            std::cout << "    " << tick_count << " ticks, Equity: $" << std::fixed << std::setprecision(2)
                      << engine.GetEquity() << ", DD: " << engine.GetMaxDrawdownPct() << "%"
                      << ", Regime: " << silver_regime.GetRegimeString()
                      << ", Silver: " << silver_dir
                      << ", Gold pos: " << engine.GetPositionsForSymbol("XAUUSD").size()
                      << ", Silver pos: " << engine.GetPositionsForSymbol("XAGUSD").size() << std::endl;
        }
    }

    // Results
    auto engine_results = engine.GetResults();
    result.final_equity = engine_results.final_equity;
    result.max_dd_pct = engine_results.max_drawdown_pct;
    result.peak_equity = engine_results.peak_equity;
    result.gold_entries = gold_strategy.GetEntries();
    result.silver_entries = silver_strategy.GetEntries();
    result.total_trades = engine_results.total_trades;
    result.return_multiple = result.final_equity / initial_balance;
    result.survived = (result.final_equity > 0);
    result.regime_changes = silver_regime.GetRegimeChanges();

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
    std::cout << "Regime changes: " << r.regime_changes << std::endl;
    std::cout << "Bullish ticks: " << r.bullish_ticks << " ("
              << (r.bullish_ticks * 100.0 / (r.bullish_ticks + r.bearish_ticks + 1)) << "%)" << std::endl;
    std::cout << "Survived: " << (r.survived ? "YES" : "NO") << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "==================================================" << std::endl;
    std::cout << "   FillUp/FillDown Hedge - REGIME AWARE" << std::endl;
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

    // Regime detector config
    RegimeDetector::Config regime_config;
    regime_config.fast_period = 10000;    // ~1000 seconds at 10 ticks/sec
    regime_config.slow_period = 50000;    // ~5000 seconds
    regime_config.trend_threshold = 0.3;  // 0.3% above/below SMA
    regime_config.confirmation_ticks = 5000;

    std::cout << "\n=== MODE 1: REGIME-CONTROLLED SHORTS ===" << std::endl;
    std::cout << "(Only short silver in bearish regime)" << std::endl;

    // Test 1a: Conservative regime detection
    std::cout << "\n--- Test 1a: Regime-controlled, TP=5x ---" << std::endl;
    results.push_back(RunRegimeControlledTest(synced, "Regime-Short (TP=5x)",
        0.3, 0.4,      // spacing %
        5.0,           // max volume
        5.0,           // TP multiplier
        initial_balance,
        regime_config,
        true));

    // Test 1b: Wider spacing
    std::cout << "\n--- Test 1b: Regime-controlled, wider spacing ---" << std::endl;
    results.push_back(RunRegimeControlledTest(synced, "Regime-Short (wide)",
        0.5, 0.6,      // wider spacing
        3.0,           // lower max volume
        10.0,          // wider TP
        initial_balance,
        regime_config,
        true));

    std::cout << "\n=== MODE 2: ADAPTIVE DIRECTION ===" << std::endl;
    std::cout << "(Long both in bull, hedge in bear)" << std::endl;

    // Test 2a: Adaptive direction
    std::cout << "\n--- Test 2a: Adaptive direction, TP=5x ---" << std::endl;
    results.push_back(RunAdaptiveDirectionTest(synced, "Adaptive (TP=5x)",
        0.3, 0.4,      // spacing %
        5.0,           // max volume
        5.0,           // TP multiplier
        initial_balance,
        regime_config,
        true));

    // Test 2b: Adaptive with faster regime detection
    RegimeDetector::Config fast_regime = regime_config;
    fast_regime.fast_period = 5000;
    fast_regime.slow_period = 20000;
    fast_regime.confirmation_ticks = 2000;

    std::cout << "\n--- Test 2b: Adaptive, faster regime detection ---" << std::endl;
    results.push_back(RunAdaptiveDirectionTest(synced, "Adaptive (fast)",
        0.3, 0.4,
        5.0,
        5.0,
        initial_balance,
        fast_regime,
        true));

    // Test 2c: Adaptive with wider spacing
    std::cout << "\n--- Test 2c: Adaptive, wide spacing ---" << std::endl;
    results.push_back(RunAdaptiveDirectionTest(synced, "Adaptive (wide)",
        0.5, 0.6,
        3.0,
        10.0,
        initial_balance,
        regime_config,
        true));

    // Summary
    std::cout << "\n\n========================================" << std::endl;
    std::cout << "           RESULTS SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;

    for (const auto& r : results) {
        PrintResult(r);
    }

    // Ranking
    std::cout << "\n\n=== RANKED RESULTS ===" << std::endl;
    std::vector<const HedgeResult*> sorted;
    for (const auto& r : results) sorted.push_back(&r);

    std::sort(sorted.begin(), sorted.end(),
        [](const HedgeResult* a, const HedgeResult* b) {
            if (a->survived != b->survived) return a->survived > b->survived;
            return a->return_multiple > b->return_multiple;
        });

    int rank = 1;
    for (const auto* r : sorted) {
        std::cout << rank++ << ". " << r->mode_name << ": "
                  << (r->survived ? "SURVIVED" : "STOPPED OUT")
                  << " | " << std::fixed << std::setprecision(2) << r->return_multiple << "x"
                  << " | DD: " << r->max_dd_pct << "%"
                  << std::endl;
    }

    return 0;
}
