/**
 * Test FillUpOscillation strategy on all  oil/gas commodity instruments.
 * Tests both grid trading and carry trade approaches.
 *
 * Instruments: CL-OIL, UKOUSD, UKOUSDft, USOUSD, NG-C, GASOIL-C, GAS-C
 */
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <filesystem>

using namespace backtest;

// ============================================================================
// Instrument configurations from  live scan
// ============================================================================
struct InstrumentConfig {
    std::string symbol;
    std::string description;
    double contract_size;
    double pip_size;
    int digits;
    double swap_long;
    double swap_short;
    int swap_mode;       // 0=DISABLED, 1=POINTS
    int swap_3days;
    double volume_min;
    double volume_step;
    double daily_range_pct;  // For strategy tuning
};

static std::vector<InstrumentConfig> GetOilGasInstruments() {
    return {
        {"CL-OIL",    "Crude Oil Future CFD",   1000.0, 0.001, 3,  0.0,     0.0,      0, 3, 0.01, 0.01, 3.36},
        {"UKOUSD",    "Brent Crude Oil Cash",    1000.0, 0.001, 3,  9.3785, -30.539,   1, 5, 0.01, 0.01, 3.05},
        {"UKOUSDft",  "Brent Crude Oil Future",  1000.0, 0.001, 3,  56.158, -102.463,  0, 5, 0.01, 0.01, 3.14},
        {"USOUSD",    "WTI Crude Oil Cash",      1000.0, 0.001, 3, -4.8265, -13.0265,  1, 5, 0.01, 0.01, 3.33},
        {"NG-C",      "Natural Gas",            10000.0, 0.001, 3,  32.86,  -49.52,    1, 5, 0.10, 0.10, 8.94},
        {"GASOIL-C",  "Low Sulphur Gasoil",       100.0, 0.01,  2,  14.51,  -28.61,    1, 5, 0.10, 0.10, 3.42},
        {"GAS-C",     "Gasoline",               42000.0, 0.0001,4, -6.65,    3.17,     1, 5, 0.10, 0.10, 2.94},
    };
}

// ============================================================================
// Configure engine for a given instrument
// ============================================================================
TickBacktestConfig MakeEngineConfig(const InstrumentConfig& inst, double initial_balance) {
    std::string data_dir = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\";

    TickDataConfig tick_config;
    tick_config.file_path = data_dir + inst.symbol + "_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig config;
    config.symbol = inst.symbol;
    config.initial_balance = initial_balance;
    config.contract_size = inst.contract_size;
    config.leverage = 500.0;
    config.pip_size = inst.pip_size;
    config.digits = inst.digits;
    config.swap_long = inst.swap_long;
    config.swap_short = inst.swap_short;
    config.swap_mode = inst.swap_mode;
    config.swap_3days = inst.swap_3days;
    config.volume_min = inst.volume_min;
    config.volume_step = inst.volume_step;
    config.volume_max = 20.0;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.verbose = false;
    config.tick_data_config = tick_config;
    return config;
}

// ============================================================================
// Create FillUpOscillation config tuned for oil/gas
// All use pct_spacing since these are different price scales
// Key insight: contract_size affects P/L exposure, not just margin.
// Oil (cs=1000) has 10x P/L per pip vs gold (cs=100).
// We must scale survive_pct and spacing accordingly.
// ============================================================================
FillUpOscillation::Config MakeStrategyConfig(const InstrumentConfig& inst) {
    FillUpOscillation::Config cfg;
    cfg.contract_size = inst.contract_size;
    cfg.leverage = 500.0;
    cfg.min_volume = inst.volume_min;
    cfg.max_volume = 1.0;  // Cap max volume - oil has large contract sizes
    cfg.mode = FillUpOscillation::ADAPTIVE_SPACING;

    // Use percentage spacing for all oil instruments
    cfg.adaptive.pct_spacing = true;

    // Scale survive_pct based on contract_size relative to gold (100)
    // Higher contract_size = more P/L per pip = need higher survive_pct
    double cs_factor = inst.contract_size / 100.0;  // 1.0 for gold, 10.0 for oil

    if (inst.symbol == "NG-C") {
        // Natural Gas: extreme volatility (8.94%) + huge contract (10000)
        // P/L per 0.1 lot per $1 move: 0.1 * 10000 * 1 = $1000
        // NG can swing 50%+ in a year, need very high survive
        cfg.survive_pct = 50.0;
        cfg.base_spacing = 5.0;  // 5% of price
        cfg.volatility_lookback_hours = 2.0;
        cfg.adaptive.typical_vol_pct = 2.0;
        cfg.velocity_threshold = 50.0;
        cfg.max_volume = 0.1;
    } else if (inst.symbol == "GAS-C") {
        // Gasoline: enormous contract size (42000)
        // P/L per 0.1 lot per $0.01 move: 0.1 * 42000 * 0.01 = $42
        cfg.survive_pct = 40.0;
        cfg.base_spacing = 3.0;  // 3% of price
        cfg.volatility_lookback_hours = 4.0;
        cfg.adaptive.typical_vol_pct = 0.5;
        cfg.velocity_threshold = 30.0;
        cfg.max_volume = 0.1;
    } else if (inst.symbol == "GASOIL-C") {
        // Gasoil: contract size 100 (same as gold!)
        // P/L per 0.1 lot per $1 move: 0.1 * 100 * 1 = $10
        cfg.survive_pct = 30.0;
        cfg.base_spacing = 2.0;  // 2% of price
        cfg.volatility_lookback_hours = 4.0;
        cfg.adaptive.typical_vol_pct = 0.6;
        cfg.velocity_threshold = 30.0;
        cfg.max_volume = 1.0;
    } else {
        // Crude oils (CL-OIL, UKOUSD, UKOUSDft, USOUSD): contract 1000
        // P/L per 0.01 lot per $1 move: 0.01 * 1000 * 1 = $10
        // Oil crashed 25%+ in 2025 (from $75 to $56), need high survive_pct
        cfg.survive_pct = 40.0;
        cfg.base_spacing = 3.0;  // 3% of price (~$2)
        cfg.volatility_lookback_hours = 4.0;
        cfg.adaptive.typical_vol_pct = 0.6;
        cfg.velocity_threshold = 30.0;
        cfg.max_volume = 0.5;
    }

    // Safety: equity stop at 60% to prevent stop-out
    cfg.safety.equity_stop_pct = 60.0;

    // Safety: don't force min volume entries (crash protection)
    cfg.safety.force_min_volume_entry = false;

    return cfg;
}

// ============================================================================
// Simple Carry Trade Strategy
// Opens and holds long positions when swap is positive (earns daily interest)
// Uses grid-like entries but with wider spacing and bias toward accumulation
// ============================================================================
class CarryTradeStrategy {
public:
    struct Config {
        double contract_size;
        double leverage;
        double volume_per_entry;    // Fixed lot size per entry
        double entry_spacing_pct;   // % between entries
        double max_drawdown_pct;    // Stop adding if DD exceeds this
        double take_profit_pct;     // TP as % of entry price
        int max_positions;          // Maximum concurrent positions
        bool long_bias;             // true = buy (positive swap long), false = sell
    };

    struct Stats {
        int entries = 0;
        int tp_hits = 0;
        int dd_blocks = 0;
        double total_swap_earned = 0.0;
        double peak_positions = 0;
    };

    CarryTradeStrategy(const Config& cfg) : cfg_(cfg) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double price = tick.ask;
        double equity = engine.GetEquity();

        // Initialize
        if (peak_equity_ == 0.0) {
            peak_equity_ = equity;
            last_entry_price_ = 0.0;
        }
        if (equity > peak_equity_) peak_equity_ = equity;

        // Check drawdown
        double dd_pct = (peak_equity_ > 0) ? (1.0 - equity / peak_equity_) * 100.0 : 0.0;

        // Close positions at TP
        auto& positions = engine.GetOpenPositions();
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            Trade* t = positions[i];
            double entry = t->entry_price;
            double tp_dist = entry * cfg_.take_profit_pct / 100.0;

            if (cfg_.long_bias && tick.bid >= entry + tp_dist) {
                engine.ClosePosition(t, "CarryTP");
                stats_.tp_hits++;
            } else if (!cfg_.long_bias && tick.ask <= entry - tp_dist) {
                engine.ClosePosition(t, "CarryTP");
                stats_.tp_hits++;
            }
        }

        // Entry logic
        int open_count = (int)engine.GetOpenPositions().size();
        if (open_count >= cfg_.max_positions) return;

        if (dd_pct > cfg_.max_drawdown_pct) {
            stats_.dd_blocks++;
            return;
        }

        // Check spacing from last entry
        bool should_enter = false;
        if (last_entry_price_ == 0.0) {
            should_enter = true;  // First entry
        } else {
            double spacing = std::abs(price - last_entry_price_) / last_entry_price_ * 100.0;
            if (spacing >= cfg_.entry_spacing_pct) {
                should_enter = true;
            }
        }

        if (should_enter) {
            TradeDirection dir = cfg_.long_bias ? TradeDirection::BUY : TradeDirection::SELL;
            double lots = cfg_.volume_per_entry;

            Trade* trade = engine.OpenMarketOrder(dir, lots);
            if (trade != nullptr) {
                last_entry_price_ = price;
                stats_.entries++;
                if (open_count + 1 > stats_.peak_positions)
                    stats_.peak_positions = open_count + 1;
            }
        }
    }

    const Stats& GetStats() const { return stats_; }

private:
    Config cfg_;
    Stats stats_;
    double peak_equity_ = 0.0;
    double last_entry_price_ = 0.0;
};

// ============================================================================
// Print results for one instrument
// ============================================================================
void PrintResult(const std::string& /*label*/, const InstrumentConfig& inst,
                 const TickBasedEngine::BacktestResults& results, double initial_balance) {
    double return_mult = results.final_balance / initial_balance;
    double risk_adj = return_mult / (results.max_drawdown_pct / 100.0 + 0.01);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << std::left << std::setw(12) << inst.symbol
              << " " << std::setw(25) << inst.description
              << " | Final: $" << std::setw(12) << results.final_balance
              << " | " << std::setw(6) << return_mult << "x"
              << " | DD: " << std::setw(6) << results.max_drawdown_pct << "%"
              << " | Trades: " << std::setw(8) << results.total_trades
              << " | WR: " << std::setw(5) << results.win_rate << "%"
              << " | RiskAdj: " << std::setw(6) << risk_adj
              << " | Swap: $" << results.total_swap_charged
              << std::endl;
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    double initial_balance = 10000.0;
    auto instruments = GetOilGasInstruments();

    std::cout << "================================================================" << std::endl;
    std::cout << "  OIL/GAS INSTRUMENT TESTING - FillUpOscillation + Carry Trade" << std::endl;
    std::cout << "  Initial Balance: $" << initial_balance << " | Period: 2025" << std::endl;
    std::cout << "  Instruments: " << instruments.size() << std::endl;
    std::cout << "================================================================" << std::endl;

    // ========================================================================
    // PART 1: FillUpOscillation Grid Trading
    // ========================================================================
    std::cout << "\n=== PART 1: FillUpOscillation Grid Trading ===" << std::endl;
    std::cout << "  (ADAPTIVE_SPACING mode, pct_spacing=true for all)" << std::endl;
    std::cout << std::string(130, '-') << std::endl;

    for (const auto& inst : instruments) {
        // Check if data file exists
        std::string path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\"
                          + inst.symbol + "_TICKS_2025.csv";
        if (!std::filesystem::exists(path)) {
            std::cout << "  " << std::left << std::setw(12) << inst.symbol
                      << " SKIPPED - no tick data (" << path << ")" << std::endl;
            continue;
        }

        try {
            auto engine_cfg = MakeEngineConfig(inst, initial_balance);
            auto strat_cfg = MakeStrategyConfig(inst);
            FillUpOscillation strategy(strat_cfg);
            TickBasedEngine engine(engine_cfg);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto results = engine.GetResults();
            PrintResult("FillUp", inst, results, initial_balance);

        } catch (const std::exception& e) {
            std::cout << "  " << std::left << std::setw(12) << inst.symbol
                      << " ERROR: " << e.what() << std::endl;
        }
    }

    // ========================================================================
    // PART 2: Carry Trade (positive swap instruments only)
    // ========================================================================
    std::cout << "\n=== PART 2: Carry Trade (Positive Swap Long) ===" << std::endl;
    std::cout << "  Strategy: Buy and hold with grid entries, earn daily swap" << std::endl;
    std::cout << "  Eligible: UKOUSD (+$9.38/day), UKOUSDft (+$56.16/day)," << std::endl;
    std::cout << "            NG-C (+$328.60/day), GASOIL-C (+$14.51/day)" << std::endl;
    std::cout << std::string(130, '-') << std::endl;

    for (const auto& inst : instruments) {
        // Only test carry on positive-swap-long instruments
        if (inst.swap_long <= 0) continue;

        std::string path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\"
                          + inst.symbol + "_TICKS_2025.csv";
        if (!std::filesystem::exists(path)) {
            std::cout << "  " << std::left << std::setw(12) << inst.symbol
                      << " SKIPPED - no tick data" << std::endl;
            continue;
        }

        try {
            auto engine_cfg = MakeEngineConfig(inst, initial_balance);
            TickBasedEngine engine(engine_cfg);

            CarryTradeStrategy::Config carry_cfg;
            carry_cfg.contract_size = inst.contract_size;
            carry_cfg.leverage = 500.0;
            carry_cfg.long_bias = true;  // Buy to earn positive swap
            carry_cfg.max_drawdown_pct = 40.0;

            if (inst.symbol == "NG-C") {
                // NG: extreme vol, huge contract (10000), min lot 0.1
                // 0.1 lot margin = $4,295. Max 2 positions on $10K
                carry_cfg.volume_per_entry = 0.1;
                carry_cfg.entry_spacing_pct = 5.0;
                carry_cfg.take_profit_pct = 6.0;
                carry_cfg.max_positions = 2;
            } else if (inst.symbol == "GASOIL-C") {
                // Gasoil: contract=100 (like gold), min lot 0.1
                // 0.1 lot margin = $6,917. Max 1 position on $10K
                carry_cfg.volume_per_entry = 0.1;
                carry_cfg.entry_spacing_pct = 3.0;
                carry_cfg.take_profit_pct = 4.0;
                carry_cfg.max_positions = 1;
            } else {
                // Crude oils: contract=1000, min lot 0.01
                // 0.01 lot margin = ~$634. Reasonable for $10K
                carry_cfg.volume_per_entry = 0.01;
                carry_cfg.entry_spacing_pct = 2.5;
                carry_cfg.take_profit_pct = 3.0;
                carry_cfg.max_positions = 10;
            }

            CarryTradeStrategy strategy(carry_cfg);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto results = engine.GetResults();
            const auto& stats = strategy.GetStats();

            PrintResult("Carry", inst, results, initial_balance);
            std::cout << "    >> Carry Stats: entries=" << stats.entries
                      << " tp_hits=" << stats.tp_hits
                      << " dd_blocks=" << stats.dd_blocks
                      << " peak_pos=" << stats.peak_positions
                      << " swap_total=$" << results.total_swap_charged
                      << std::endl;

        } catch (const std::exception& e) {
            std::cout << "  " << std::left << std::setw(12) << inst.symbol
                      << " ERROR: " << e.what() << std::endl;
        }
    }

    // ========================================================================
    // PART 3: Carry Trade SHORT (positive swap short instruments)
    // ========================================================================
    std::cout << "\n=== PART 3: Carry Trade (Positive Swap Short) ===" << std::endl;
    std::cout << "  Strategy: Sell and hold, earn daily swap on shorts" << std::endl;
    std::cout << "  Eligible: GAS-C (swap_short=+3.17)" << std::endl;
    std::cout << std::string(130, '-') << std::endl;

    for (const auto& inst : instruments) {
        // Only test short carry on positive-swap-short instruments
        if (inst.swap_short <= 0) continue;

        std::string path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\"
                          + inst.symbol + "_TICKS_2025.csv";
        if (!std::filesystem::exists(path)) {
            std::cout << "  " << std::left << std::setw(12) << inst.symbol
                      << " SKIPPED - no tick data" << std::endl;
            continue;
        }

        try {
            auto engine_cfg = MakeEngineConfig(inst, initial_balance);
            TickBasedEngine engine(engine_cfg);

            CarryTradeStrategy::Config carry_cfg;
            carry_cfg.contract_size = inst.contract_size;
            carry_cfg.leverage = 500.0;
            carry_cfg.long_bias = false;  // Sell to earn positive swap on shorts
            carry_cfg.max_drawdown_pct = 40.0;
            if (inst.contract_size >= 10000) {
                carry_cfg.volume_per_entry = 0.1;
                carry_cfg.entry_spacing_pct = 4.0;
                carry_cfg.take_profit_pct = 5.0;
                carry_cfg.max_positions = 2;
            } else if (inst.contract_size >= 1000) {
                carry_cfg.volume_per_entry = 0.01;
                carry_cfg.entry_spacing_pct = 2.5;
                carry_cfg.take_profit_pct = 3.0;
                carry_cfg.max_positions = 10;
            } else {
                carry_cfg.volume_per_entry = 0.1;
                carry_cfg.entry_spacing_pct = 2.0;
                carry_cfg.take_profit_pct = 3.0;
                carry_cfg.max_positions = 10;
            }

            CarryTradeStrategy strategy(carry_cfg);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto results = engine.GetResults();
            const auto& stats = strategy.GetStats();

            PrintResult("Carry-S", inst, results, initial_balance);
            std::cout << "    >> Carry Stats: entries=" << stats.entries
                      << " tp_hits=" << stats.tp_hits
                      << " dd_blocks=" << stats.dd_blocks
                      << " peak_pos=" << stats.peak_positions
                      << " swap_total=$" << results.total_swap_charged
                      << std::endl;

        } catch (const std::exception& e) {
            std::cout << "  " << std::left << std::setw(12) << inst.symbol
                      << " ERROR: " << e.what() << std::endl;
        }
    }

    std::cout << "\n================================================================" << std::endl;
    std::cout << "  Testing complete." << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
