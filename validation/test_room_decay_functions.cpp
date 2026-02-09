/**
 * Test different room decay functions to find optimal shape
 *
 * Compares:
 * 1. Hyperbola (current): room = base × (gain)^power
 * 2. Linear decay: room = base - k × gain
 * 3. Exponential decay: room = base × e^(-k × gain)
 * 4. Constant percentage: room = price × constant_pct
 * 5. Step function: room = base / (1 + floor(gain/threshold))
 * 6. Sigmoid: room = base / (1 + e^(k × (gain - midpoint)))
 * 7. Square root: room = base × sqrt(1 / (1 + gain/scale))
 * 8. Multiple hyperbolas (reset every N% gain)
 */

#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

using namespace backtest;

// Room decay function types
enum class DecayType {
    HYPERBOLA,          // Original: base × (gain)^power
    LINEAR,             // base - k × gain (clamped to min)
    EXPONENTIAL,        // base × e^(-k × gain)
    CONSTANT_PCT,       // price × constant_pct
    STEP,               // base / (1 + floor(gain/threshold))
    SIGMOID,            // base / (1 + e^(k × (gain - midpoint)))
    SQRT,               // base × sqrt(1 / (1 + gain/scale))
    MULTI_HYPERBOLA,    // Reset hyperbola every N% gain
    LOGARITHMIC,        // base / (1 + k × ln(1 + gain))
    INVERSE_LINEAR      // base × scale / (scale + gain)
};

std::string DecayTypeName(DecayType type) {
    switch (type) {
        case DecayType::HYPERBOLA: return "Hyperbola";
        case DecayType::LINEAR: return "Linear";
        case DecayType::EXPONENTIAL: return "Exponential";
        case DecayType::CONSTANT_PCT: return "Constant%";
        case DecayType::STEP: return "Step";
        case DecayType::SIGMOID: return "Sigmoid";
        case DecayType::SQRT: return "Sqrt";
        case DecayType::MULTI_HYPERBOLA: return "MultiHyper";
        case DecayType::LOGARITHMIC: return "Logarithmic";
        case DecayType::INVERSE_LINEAR: return "InvLinear";
        default: return "Unknown";
    }
}

struct DecayConfig {
    DecayType type;
    double param1;  // Primary parameter (varies by type)
    double param2;  // Secondary parameter (varies by type)
    double multiplier;  // Initial room as % of price
    double stop_out_margin;

    std::string Describe() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3);
        ss << DecayTypeName(type) << "(";
        switch (type) {
            case DecayType::HYPERBOLA:
                ss << "power=" << param1;
                break;
            case DecayType::LINEAR:
                ss << "k=" << param1;
                break;
            case DecayType::EXPONENTIAL:
                ss << "k=" << param1;
                break;
            case DecayType::CONSTANT_PCT:
                ss << "pct=" << param1 << "%";
                break;
            case DecayType::STEP:
                ss << "thresh=" << param1;
                break;
            case DecayType::SIGMOID:
                ss << "k=" << param1 << ",mid=" << param2;
                break;
            case DecayType::SQRT:
                ss << "scale=" << param1;
                break;
            case DecayType::MULTI_HYPERBOLA:
                ss << "power=" << param1 << ",reset=" << param2 << "%";
                break;
            case DecayType::LOGARITHMIC:
                ss << "k=" << param1;
                break;
            case DecayType::INVERSE_LINEAR:
                ss << "scale=" << param1;
                break;
            default:
                ss << param1 << "," << param2;
        }
        ss << ")";
        return ss.str();
    }
};

// Strategy with configurable room decay
class FlexibleRoomStrategy {
public:
    DecayConfig config;
    double starting_price = 0.0;
    double starting_room = 0.0;
    double last_entry_price = 0.0;
    double volume_of_open_trades = 0.0;
    double last_reset_price = 0.0;  // For multi-hyperbola
    double last_reset_room = 0.0;
    int trade_count = 0;

    FlexibleRoomStrategy(const DecayConfig& cfg) : config(cfg) {}

    double CalculateRoom(double current_price) {
        if (trade_count == 0) return starting_room;

        double gain = current_price - starting_price;
        if (gain < 1e-6) return starting_room;

        double room = starting_room;

        switch (config.type) {
            case DecayType::HYPERBOLA:
                // room = base × (gain)^power
                room = starting_room * std::pow(gain, config.param1);
                break;

            case DecayType::LINEAR:
                // room = base - k × gain (clamped)
                room = starting_room - config.param1 * gain;
                break;

            case DecayType::EXPONENTIAL:
                // room = base × e^(-k × gain)
                room = starting_room * std::exp(-config.param1 * gain);
                break;

            case DecayType::CONSTANT_PCT:
                // room = price × constant_pct
                room = current_price * config.param1 / 100.0;
                break;

            case DecayType::STEP:
                // room = base / (1 + floor(gain/threshold))
                room = starting_room / (1.0 + std::floor(gain / config.param1));
                break;

            case DecayType::SIGMOID:
                // room = base / (1 + e^(k × (gain - midpoint)))
                room = starting_room / (1.0 + std::exp(config.param1 * (gain - config.param2)));
                break;

            case DecayType::SQRT:
                // room = base × sqrt(1 / (1 + gain/scale))
                room = starting_room * std::sqrt(1.0 / (1.0 + gain / config.param1));
                break;

            case DecayType::MULTI_HYPERBOLA: {
                // Reset hyperbola every N% price gain
                double pct_from_reset = (current_price - last_reset_price) / last_reset_price * 100.0;
                if (pct_from_reset >= config.param2) {
                    // Would reset - use fresh room from current price
                    room = current_price * config.multiplier / 100.0;
                } else {
                    // Within cycle - use hyperbola from last reset
                    double cycle_gain = current_price - last_reset_price;
                    if (cycle_gain > 1e-6) {
                        room = last_reset_room * std::pow(cycle_gain, config.param1);
                    } else {
                        room = last_reset_room;
                    }
                }
                break;
            }

            case DecayType::LOGARITHMIC:
                // room = base / (1 + k × ln(1 + gain))
                room = starting_room / (1.0 + config.param1 * std::log(1.0 + gain));
                break;

            case DecayType::INVERSE_LINEAR:
                // room = base × scale / (scale + gain) - hyperbola-like but gentler
                room = starting_room * config.param1 / (config.param1 + gain);
                break;
        }

        // Clamp room to reasonable bounds
        room = std::max(0.01, std::min(1e9, room));
        return room;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double ask = tick.ask;
        double bid = tick.bid;
        double equity = engine.GetEquity();
        double balance = engine.GetBalance();
        const auto& positions = engine.GetOpenPositions();

        // Calculate margin
        double used_margin = 0.0;
        for (const Trade* t : positions) {
            used_margin += t->lot_size * 1.0 * t->entry_price / 100.0;  // contract=1, leverage=100
        }
        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        // Stop-out check
        if (margin_level < config.stop_out_margin && margin_level > 0 && !positions.empty()) {
            // Close all
            std::vector<Trade*> to_close(positions.begin(), positions.end());
            for (Trade* t : to_close) {
                engine.ClosePosition(t, "STOP_OUT");
            }
            // Reset
            volume_of_open_trades = 0.0;
            last_entry_price = 0.0;
            starting_price = 0.0;
            starting_room = 0.0;
            last_reset_price = 0.0;
            last_reset_room = 0.0;
            trade_count = 0;
            return;
        }

        // First trade
        if (volume_of_open_trades == 0.0) {
            starting_price = ask;
            starting_room = ask * config.multiplier / 100.0;
            last_reset_price = ask;
            last_reset_room = starting_room;

            double room = starting_room;
            double spread_cost = (ask - bid) * 1.0;

            double numerator = 100.0 * balance * 100.0;
            double denominator = 100.0 * room * 100.0 + 100.0 * spread_cost * 100.0 + config.stop_out_margin * ask;

            if (denominator <= 0) return;

            double lot_size = numerator / denominator / 1.0;
            lot_size = std::max(0.01, std::min(100.0, lot_size));
            lot_size = std::round(lot_size * 100.0) / 100.0;

            if (lot_size >= 0.01) {
                Trade* t = engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, 0.0);
                if (t) {
                    volume_of_open_trades += lot_size;
                    last_entry_price = ask;
                    trade_count++;
                }
            }
            return;
        }

        // Check for multi-hyperbola reset
        if (config.type == DecayType::MULTI_HYPERBOLA) {
            double pct_from_reset = (ask - last_reset_price) / last_reset_price * 100.0;
            if (pct_from_reset >= config.param2) {
                last_reset_price = ask;
                last_reset_room = ask * config.multiplier / 100.0;
            }
        }

        // Subsequent trades on new high
        if (ask > last_entry_price) {
            double room = CalculateRoom(ask);
            double spread_cost = (ask - bid) * 1.0;

            double numerator = 100.0 * equity * 100.0
                             - 100.0 * config.stop_out_margin * used_margin
                             - 100.0 * room * 100.0 * volume_of_open_trades;
            double denominator = 100.0 * room * 100.0 + 100.0 * spread_cost * 100.0 + config.stop_out_margin * ask;

            if (denominator <= 0 || numerator <= 0) return;

            double lot_size = numerator / denominator / 1.0;
            lot_size = std::max(0.01, std::min(100.0, lot_size));
            lot_size = std::round(lot_size * 100.0) / 100.0;

            // Check free margin
            double free_margin = equity - used_margin;
            double margin_per_lot = 0.01 * 1.0 * ask / 100.0;
            if (margin_per_lot > 0) {
                double lot_by_margin = (free_margin / margin_per_lot) * 0.01;
                lot_size = std::min(lot_size, lot_by_margin);
            }

            if (lot_size >= 0.01) {
                lot_size = std::round(lot_size * 100.0) / 100.0;
                Trade* t = engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, 0.0);
                if (t) {
                    volume_of_open_trades += lot_size;
                    last_entry_price = ask;
                    trade_count++;
                }
            }
        }
    }
};

struct TestResult {
    DecayConfig config;
    double final_balance;
    double max_drawdown_pct;
    double profit_factor;
    int total_trades;
    int cycles;
    double return_pct;
};

int main() {
    std::cout << "=== Room Decay Function Comparison ===" << std::endl;
    std::cout << "Testing different mathematical shapes for room decay\n" << std::endl;

    // Load tick data once
    std::cout << "Loading NAS100 tick data..." << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\.claude-worktrees\\ctrader-backtest\\beautiful-margulis\\validation\\Grid\\NAS100_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    manager.LoadAllTicks();
    const std::vector<Tick>& ticks = manager.GetAllTicks();

    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;

    // Build test configurations - focused key variants
    std::vector<DecayConfig> configs;

    double base_multiplier = 10.0;  // 10% starting room
    double stop_out = 500.0;        // 500% margin level stop-out

    // 1. HYPERBOLA variants (original strategy) - best performer baseline
    for (double power : {-0.5, -0.7}) {
        configs.push_back({DecayType::HYPERBOLA, power, 0.0, base_multiplier, stop_out});
    }

    // 2. LINEAR decay - simplest model
    for (double k : {0.1, 0.2}) {
        configs.push_back({DecayType::LINEAR, k, 0.0, base_multiplier, stop_out});
    }

    // 3. EXPONENTIAL decay
    for (double k : {0.0005, 0.001}) {
        configs.push_back({DecayType::EXPONENTIAL, k, 0.0, base_multiplier, stop_out});
    }

    // 4. CONSTANT PERCENTAGE (no decay - room scales with price)
    for (double pct : {1.0, 2.0}) {
        configs.push_back({DecayType::CONSTANT_PCT, pct, 0.0, base_multiplier, stop_out});
    }

    // 5. STEP function
    for (double thresh : {1000.0, 2000.0}) {
        configs.push_back({DecayType::STEP, thresh, 0.0, base_multiplier, stop_out});
    }

    // 6. SQRT decay (gentler than hyperbola)
    for (double scale : {1000.0, 2000.0}) {
        configs.push_back({DecayType::SQRT, scale, 0.0, base_multiplier, stop_out});
    }

    // 7. MULTI-HYPERBOLA (reset cycles) - key innovation to test
    for (double power : {-0.5}) {
        for (double reset_pct : {5.0, 10.0, 20.0}) {
            configs.push_back({DecayType::MULTI_HYPERBOLA, power, reset_pct, base_multiplier, stop_out});
        }
    }

    // 8. LOGARITHMIC decay
    for (double k : {0.2, 0.5}) {
        configs.push_back({DecayType::LOGARITHMIC, k, 0.0, base_multiplier, stop_out});
    }

    // 9. INVERSE LINEAR (gentler hyperbola)
    for (double scale : {1000.0, 2000.0}) {
        configs.push_back({DecayType::INVERSE_LINEAR, scale, 0.0, base_multiplier, stop_out});
    }

    std::cout << "\nTesting " << configs.size() << " configurations...\n" << std::endl;

    // Run tests sequentially (safer for debugging)
    std::vector<TestResult> results;

    for (size_t idx = 0; idx < configs.size(); idx++) {
        const auto& cfg = configs[idx];

        TickBacktestConfig engine_config;
        engine_config.symbol = "NAS100";
        engine_config.initial_balance = 10000.0;
        engine_config.contract_size = 1.0;
        engine_config.leverage = 100.0;
        engine_config.pip_size = 0.01;
        engine_config.swap_long = -17.14;
        engine_config.swap_short = 5.76;
        engine_config.swap_mode = 5;
        engine_config.swap_3days = 5;
        engine_config.start_date = "2025.04.07";
        engine_config.end_date = "2025.10.30";
        engine_config.tick_data_config = tick_config;
        engine_config.verbose = false;

        TickBasedEngine engine(engine_config);
        FlexibleRoomStrategy strategy(cfg);

        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();

        TestResult tr;
        tr.config = cfg;
        tr.final_balance = res.final_balance;
        tr.max_drawdown_pct = res.max_drawdown_pct;
        tr.profit_factor = res.profit_factor;
        tr.total_trades = res.total_trades;
        tr.cycles = strategy.trade_count > 0 ? 1 : 0;
        tr.return_pct = (res.final_balance - 10000.0) / 10000.0 * 100.0;

        results.push_back(tr);

        std::cout << "  " << (idx + 1) << "/" << configs.size() << ": "
                  << cfg.Describe() << " -> $" << std::fixed << std::setprecision(0)
                  << tr.final_balance << std::endl;
    }

    // Sort by return
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        return a.final_balance > b.final_balance;
    });

    // Print results grouped by decay type
    std::cout << "\n\n====== RESULTS BY DECAY FUNCTION TYPE ======\n" << std::endl;

    std::map<DecayType, std::vector<TestResult>> by_type;
    for (const auto& r : results) {
        by_type[r.config.type].push_back(r);
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::setw(15) << "Type"
              << std::setw(12) << "Best$"
              << std::setw(12) << "BestRet%"
              << std::setw(10) << "AvgRet%"
              << std::setw(10) << "BestDD%"
              << std::setw(30) << "Best Config"
              << std::endl;
    std::cout << std::string(89, '-') << std::endl;

    for (auto& [type, type_results] : by_type) {
        // Sort this type's results
        std::sort(type_results.begin(), type_results.end(), [](const TestResult& a, const TestResult& b) {
            return a.final_balance > b.final_balance;
        });

        const auto& best = type_results.front();

        double avg_return = 0.0;
        for (const auto& r : type_results) {
            avg_return += r.return_pct;
        }
        avg_return /= type_results.size();

        std::cout << std::setw(15) << DecayTypeName(type)
                  << std::setw(12) << best.final_balance
                  << std::setw(12) << best.return_pct
                  << std::setw(10) << avg_return
                  << std::setw(10) << best.max_drawdown_pct
                  << "  " << best.config.Describe()
                  << std::endl;
    }

    // Top 20 overall
    std::cout << "\n\n====== TOP 20 CONFIGURATIONS OVERALL ======\n" << std::endl;
    std::cout << std::setw(5) << "Rank"
              << std::setw(15) << "Type"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return%"
              << std::setw(10) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(35) << "Configuration"
              << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)results.size()); i++) {
        const auto& r = results[i];
        std::cout << std::setw(5) << (i + 1)
                  << std::setw(15) << DecayTypeName(r.config.type)
                  << std::setw(12) << r.final_balance
                  << std::setw(10) << r.return_pct
                  << std::setw(10) << r.max_drawdown_pct
                  << std::setw(8) << r.total_trades
                  << "  " << r.config.Describe()
                  << std::endl;
    }

    // Bottom 10 (worst performers)
    std::cout << "\n\n====== BOTTOM 10 (WORST) ======\n" << std::endl;
    for (int i = std::max(0, (int)results.size() - 10); i < (int)results.size(); i++) {
        const auto& r = results[i];
        std::cout << std::setw(5) << (i + 1)
                  << std::setw(15) << DecayTypeName(r.config.type)
                  << std::setw(12) << r.final_balance
                  << std::setw(10) << r.return_pct
                  << std::setw(10) << r.max_drawdown_pct
                  << std::setw(8) << r.total_trades
                  << "  " << r.config.Describe()
                  << std::endl;
    }

    // Analysis summary
    std::cout << "\n\n====== ANALYSIS ======\n" << std::endl;

    // Find best of each type
    std::cout << "BEST DECAY SHAPE: " << DecayTypeName(results[0].config.type) << std::endl;
    std::cout << "  Config: " << results[0].config.Describe() << std::endl;
    std::cout << "  Return: " << results[0].return_pct << "%" << std::endl;
    std::cout << "  Max DD: " << results[0].max_drawdown_pct << "%" << std::endl;

    return 0;
}
