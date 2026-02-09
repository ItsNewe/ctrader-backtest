#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cfloat>
#include <vector>

using namespace backtest;

/**
 * HarmonyStrategy - Combines "up while up" + "up while down"
 *
 * Up while up: Opens BUY at new price highs (with spacing)
 * Up while down: Opens BUY as price drops (uniform 0.01 lots, spaced)
 * ALL positions held - no closing
 *
 * Key: Lot sizing and spacing MUST respect survive_pct
 * ACCURATE loss calculation: sum P/L for each position at worst-case price
 */
class HarmonyStrategy {
public:
    struct Config {
        double survive_pct = 13.0;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;

        // Up while down: uniform small lots
        double down_lot_size = 0.01;

        // Up spacing as percentage of price (e.g., 0.5 = 0.5% spacing)
        double up_spacing_pct = 0.5;
    };

    struct Stats {
        int up_entries = 0;
        int down_entries = 0;
        double up_volume = 0.0;
        double down_volume = 0.0;
        double start_price = 0.0;
        double highest_price = 0.0;
        double lowest_price = DBL_MAX;
        double max_total_volume = 0.0;
    };

private:
    Config config_;
    Stats stats_;

    // "Up while up" tracking - last entry price for spacing
    double last_up_entry_ = 0.0;

    // "Up while down" tracking - lowest entry and spacing
    double lowest_entry_ = DBL_MAX;
    double down_spacing_ = 0.0;

    // Total open volume for margin calc
    double total_volume_ = 0.0;

    // Track highest entry price (worst case reference)
    double highest_entry_price_ = 0.0;

    bool initialized_ = false;

public:
    HarmonyStrategy(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // Initialize on first tick
        if (!initialized_) {
            stats_.start_price = tick.ask;
            stats_.highest_price = tick.ask;
            stats_.lowest_price = tick.ask;
            highest_entry_price_ = tick.ask;
            CalculateDownSpacing(tick, engine);
            initialized_ = true;
        }

        // Track price extremes
        stats_.highest_price = std::max(stats_.highest_price, tick.ask);
        stats_.lowest_price = std::min(stats_.lowest_price, tick.ask);

        // Update total volume and highest entry
        UpdateVolumeAndHighestEntry(engine);
        stats_.max_total_volume = std::max(stats_.max_total_volume, total_volume_);

        // "Up while up": price above last entry + spacing
        double up_spacing = tick.ask * (config_.up_spacing_pct / 100.0);
        if (last_up_entry_ == 0.0 || tick.ask >= last_up_entry_ + up_spacing) {
            ExecuteUpWhileUp(tick, engine);
        }

        // "Up while down": price dropped below lowest entry - spacing
        if (lowest_entry_ >= DBL_MAX || tick.ask <= lowest_entry_ - down_spacing_) {
            ExecuteUpWhileDown(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }

private:
    void CalculateDownSpacing(const Tick& tick, TickBasedEngine& engine) {
        // Survive distance from current price
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);

        // Cost per 0.01 lot trade (margin + potential loss at survive distance)
        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double loss_per_lot = survive_dist * config_.contract_size;
        double cost_per_trade = (margin_per_lot + loss_per_lot) * config_.down_lot_size;

        // Budget 50% of equity for down trades
        double equity = engine.GetEquity();
        double budget = equity * 0.50;

        int num_trades = (int)(budget / cost_per_trade);
        num_trades = std::max(num_trades, 1);

        // Spacing to fit trades in survive distance
        down_spacing_ = survive_dist / num_trades;
        down_spacing_ = std::max(down_spacing_, 1.0);  // min $1.00 spacing
    }

    void UpdateVolumeAndHighestEntry(TickBasedEngine& engine) {
        total_volume_ = 0.0;
        highest_entry_price_ = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                total_volume_ += trade->lot_size;
                highest_entry_price_ = std::max(highest_entry_price_, trade->entry_price);
            }
        }
    }

    /**
     * ACCURATE loss calculation:
     * Calculate total P/L for all positions if price drops to worst-case level.
     * Worst-case = highest_entry_price * (1 - survive_pct)
     *
     * For each position:
     *   P/L = (worst_price - entry_price) * lot_size * contract_size
     *   Negative = loss, Positive = profit (if entry < worst_price)
     */
    double CalculateAccuratePotentialLoss(TickBasedEngine& engine, double new_entry_price = 0.0, double new_lot_size = 0.0) {
        // Determine highest entry including potential new position
        double max_entry = highest_entry_price_;
        if (new_entry_price > 0) {
            max_entry = std::max(max_entry, new_entry_price);
        }

        if (max_entry <= 0) return 0.0;

        // Worst-case price = survive_pct drop from highest entry
        double worst_price = max_entry * (1.0 - config_.survive_pct / 100.0);

        double total_pnl = 0.0;

        // Calculate P/L for each existing position at worst_price
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                double pnl = (worst_price - trade->entry_price) * trade->lot_size * config_.contract_size;
                total_pnl += pnl;
            }
        }

        // Add P/L from potential new position
        if (new_lot_size > 0 && new_entry_price > 0) {
            double new_pnl = (worst_price - new_entry_price) * new_lot_size * config_.contract_size;
            total_pnl += new_pnl;
        }

        // Return loss (negative of P/L if P/L is negative)
        return (total_pnl < 0) ? -total_pnl : 0.0;
    }

    double CalculateUpLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();

        // Current potential loss (accurate calculation)
        double current_loss = CalculateAccuratePotentialLoss(engine);

        // Available equity after accounting for potential loss
        double available = equity - current_loss;
        if (available <= 0) return 0.0;

        // Only use 30% of available for this trade (more conservative)
        available *= 0.30;

        // Survive distance from current price (for new position)
        double survive_dist = tick.ask * (config_.survive_pct / 100.0);
        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double loss_per_lot = survive_dist * config_.contract_size;

        // Max lots
        double lots = available / (margin_per_lot + loss_per_lot);
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;

        return std::max(lots, 0.0);
    }

    void ExecuteUpWhileUp(const Tick& tick, TickBasedEngine& engine) {
        double lots = CalculateUpLotSize(tick, engine);
        if (lots < config_.min_volume) return;

        // Verify with accurate loss calculation before executing
        double equity = engine.GetEquity();
        double potential_loss = CalculateAccuratePotentialLoss(engine, tick.ask, lots);
        double margin_needed = (tick.ask * config_.contract_size * lots) / config_.leverage;

        // Safety check: total potential loss + margin must not exceed 80% of equity
        if (potential_loss + margin_needed >= equity * 0.80) {
            // Reduce lot size
            lots = config_.min_volume;
            potential_loss = CalculateAccuratePotentialLoss(engine, tick.ask, lots);
            margin_needed = (tick.ask * config_.contract_size * lots) / config_.leverage;
            if (potential_loss + margin_needed >= equity * 0.80) {
                return;  // Can't afford even minimum
            }
        }

        Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
        if (trade) {
            last_up_entry_ = tick.ask;  // Update for spacing
            stats_.up_entries++;
            stats_.up_volume += lots;
            highest_entry_price_ = std::max(highest_entry_price_, tick.ask);
        }
    }

    void ExecuteUpWhileDown(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();

        // Calculate potential loss with new position (accurate)
        double potential_loss = CalculateAccuratePotentialLoss(engine, tick.ask, config_.down_lot_size);
        double margin_needed = (tick.ask * config_.contract_size * config_.down_lot_size) / config_.leverage;

        // Safety check: total required must not exceed 90% of equity
        if (potential_loss + margin_needed >= equity * 0.90) {
            return;
        }

        Trade* trade = engine.OpenMarketOrder("BUY", config_.down_lot_size, 0.0, 0.0);
        if (trade) {
            lowest_entry_ = tick.ask;
            stats_.down_entries++;
            stats_.down_volume += config_.down_lot_size;
        }
    }
};

void PrintResults(const std::string& name, const TickBasedEngine::BacktestResults& results,
                  double initial_balance) {
    double return_mult = results.final_balance / initial_balance;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(30) << name
              << " $" << std::setw(12) << results.final_balance
              << " (" << std::setw(6) << return_mult << "x)"
              << " DD: " << std::setw(6) << results.max_drawdown_pct << "%"
              << std::endl;
}

void RunTest(const std::string& name, TickBacktestConfig& config, double survive_pct, double up_spacing_pct, double initial_balance) {
    TickBasedEngine engine(config);

    HarmonyStrategy::Config cfg;
    cfg.survive_pct = survive_pct;
    cfg.up_spacing_pct = up_spacing_pct;

    HarmonyStrategy strategy(cfg);

    engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    std::cout << name << " (survive=" << survive_pct << "%, up_spacing=" << up_spacing_pct << "%):" << std::endl;
    PrintResults("  Result", results, initial_balance);
    std::cout << "    Start: $" << stats.start_price
              << " | High: $" << stats.highest_price
              << " | Low: $" << stats.lowest_price << std::endl;
    std::cout << "    Up entries: " << stats.up_entries
              << " (" << stats.up_volume << " lots)" << std::endl;
    std::cout << "    Down entries: " << stats.down_entries
              << " (" << stats.down_volume << " lots)" << std::endl;
    std::cout << "    Max total volume: " << stats.max_total_volume << " lots" << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Harmony Strategy Test ===" << std::endl;
    std::cout << "Up While Up + Up While Down (no closing)" << std::endl;
    std::cout << "ACCURATE loss calculation + spacing on UP entries" << std::endl;
    std::cout << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

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
    base_config.tick_data_config = tick_config;
    base_config.verbose = false;

    // Test 1: Crash period
    std::cout << "=== Test 1: Crash Period (Oct 17 - Dec 30) ===" << std::endl;
    {
        TickBacktestConfig config = base_config;
        config.start_date = "2025.10.17";
        config.end_date = "2025.12.30";

        RunTest("Crash", config, 13.0, 0.5, initial_balance);
        RunTest("Crash", config, 14.0, 0.5, initial_balance);
        RunTest("Crash", config, 15.0, 0.5, initial_balance);
    }

    // Test 2: Full year with different spacing
    std::cout << "=== Test 2: Full Year 2025 ===" << std::endl;
    {
        TickBacktestConfig config = base_config;
        config.start_date = "2025.01.01";
        config.end_date = "2025.12.30";

        // Test different survive% with 0.5% spacing
        RunTest("Full Year", config, 13.0, 0.5, initial_balance);
        RunTest("Full Year", config, 14.0, 0.5, initial_balance);
        RunTest("Full Year", config, 15.0, 0.5, initial_balance);

        std::cout << "--- With wider spacing (1.0%) ---" << std::endl;
        RunTest("Full Year", config, 13.0, 1.0, initial_balance);
        RunTest("Full Year", config, 14.0, 1.0, initial_balance);
        RunTest("Full Year", config, 15.0, 1.0, initial_balance);
    }

    return 0;
}
