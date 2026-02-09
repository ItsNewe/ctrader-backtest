#include "../include/tick_based_engine.h"
#include "../include/strategy_shannons_demon.h"
#include "../include/strategy_gamma_scalper.h"
#include "../include/strategy_stochastic_resonance.h"
#include "../include/strategy_entropy_harvester.h"
#include "../include/strategy_liquidity_premium.h"
#include "../include/strategy_damped_oscillator.h"
#include "../include/strategy_asymmetric_vol.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <functional>
#include <fstream>

using namespace backtest;

struct StrategyResult {
    std::string name;
    std::string instrument;
    double initial_balance;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    size_t total_trades;
    double win_rate;
    double total_swap;
    double sharpe_approx;
    std::string notes;
};

TickBacktestConfig CreateGoldConfig(const std::string& data_path) {
    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -73.69;
    config.swap_short = 45.0;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;
    config.verbose = false;

    return config;
}

TickBacktestConfig CreateSilverConfig(const std::string& data_path) {
    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = -25.44;
    config.swap_short = 10.0;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;
    config.verbose = false;

    return config;
}

TickBacktestConfig CreateUSDJPYConfig(const std::string& data_path) {
    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "USDJPY";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100000.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = 7.29;    // POSITIVE swap for longs!
    config.swap_short = -25.0;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.swap_divide_by_price = true;  // USDJPY: quote=JPY, account=USD, need /price conversion
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;
    config.verbose = false;

    return config;
}

void PrintResult(const StrategyResult& r) {
    std::cout << std::left << std::setw(30) << r.name
              << std::setw(10) << r.instrument
              << std::right << std::fixed << std::setprecision(2)
              << std::setw(12) << r.final_balance
              << std::setw(8) << r.return_multiple << "x"
              << std::setw(10) << r.max_dd_pct << "%"
              << std::setw(10) << r.total_trades
              << std::setw(10) << r.win_rate << "%"
              << std::setw(10) << r.total_swap
              << "  " << std::left << r.notes
              << std::endl;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  7 MARKET-PARASITIC STRATEGIES - COMPREHENSIVE INVESTIGATION" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Data paths
    #ifdef _WIN32
    std::string gold_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string silver_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    std::string usdjpy_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\USDJPY\\USDJPY_TICKS_2025.csv";
    #else
    std::string gold_path = "validation/Grid/XAUUSD_TICKS_2025.csv";
    std::string silver_path = "validation/XAGUSD/XAGUSD_TICKS_2025.csv";
    std::string usdjpy_path = "validation/USDJPY/USDJPY_TICKS_2025.csv";
    #endif

    std::vector<StrategyResult> results;

    // Check which data files exist
    bool has_gold = std::ifstream(gold_path).good();
    bool has_silver = std::ifstream(silver_path).good();
    bool has_usdjpy = std::ifstream(usdjpy_path).good();

    std::cout << "Data availability:" << std::endl;
    std::cout << "  XAUUSD: " << (has_gold ? "YES" : "NO") << std::endl;
    std::cout << "  XAGUSD: " << (has_silver ? "YES" : "NO") << std::endl;
    std::cout << "  USDJPY: " << (has_usdjpy ? "YES" : "NO") << std::endl;
    std::cout << std::endl;

    // ========================================================================
    // STRATEGY 1: Shannon's Demon
    // Recommended: XAGUSD | Also test on: XAUUSD
    // ========================================================================
    std::cout << "--- Strategy 1: Shannon's Demon (Volatility Pumping) ---" << std::endl;
    {
        // Test on Gold first (always available)
        if (has_gold) {
            auto config = CreateGoldConfig(gold_path);
            try {
                TickBasedEngine engine(config);
                ShannonsDemon::Config sc;
                sc.target_exposure_pct = 150.0;     // 1.5x notional/equity
                sc.rebalance_threshold_pct = 20.0;  // Rebalance when 20% drift
                sc.min_trade_size = 0.01;
                sc.max_position_lots = 0.10;        // Conservative for gold
                sc.contract_size = 100.0;
                sc.leverage = 500.0;
                sc.warmup_ticks = 1000;
                sc.rebalance_fraction = 0.5;
                ShannonsDemon strategy(sc);

                engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                    strategy.OnTick(tick, eng);
                });

                auto r = engine.GetResults();
                StrategyResult sr;
                sr.name = "1. Shannon's Demon";
                sr.instrument = "XAUUSD";
                sr.initial_balance = r.initial_balance;
                sr.final_balance = r.final_balance;
                sr.return_multiple = r.final_balance / r.initial_balance;
                sr.max_dd_pct = strategy.GetMaxDDPct();
                sr.total_trades = r.total_trades;
                sr.win_rate = r.win_rate;
                sr.total_swap = r.total_swap_charged;
                sr.notes = "Buys: " + std::to_string(strategy.GetBuys()) +
                           " Sells: " + std::to_string(strategy.GetSells());
                results.push_back(sr);
                std::cout << "  XAUUSD: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                          << " | DD: " << sr.max_dd_pct << "% | Buys: " << strategy.GetBuys()
                          << " Sells: " << strategy.GetSells()
                          << " | Swap: $" << r.total_swap_charged << std::endl;
            } catch (const std::exception& e) {
                std::cout << "  XAUUSD: ERROR - " << e.what() << std::endl;
            }
        }

        if (has_silver) {
            auto config = CreateSilverConfig(silver_path);
            try {
                TickBasedEngine engine(config);
                ShannonsDemon::Config sc;
                sc.target_exposure_pct = 100.0;     // 1x for silver (higher OMR)
                sc.rebalance_threshold_pct = 20.0;
                sc.min_trade_size = 0.01;
                sc.max_position_lots = 0.05;        // Very conservative for silver
                sc.contract_size = 5000.0;
                sc.leverage = 500.0;
                sc.warmup_ticks = 1000;
                sc.rebalance_fraction = 0.5;
                ShannonsDemon strategy(sc);

                engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                    strategy.OnTick(tick, eng);
                });

                auto r = engine.GetResults();
                StrategyResult sr;
                sr.name = "1. Shannon's Demon";
                sr.instrument = "XAGUSD*";
                sr.initial_balance = r.initial_balance;
                sr.final_balance = r.final_balance;
                sr.return_multiple = r.final_balance / r.initial_balance;
                sr.max_dd_pct = strategy.GetMaxDDPct();
                sr.total_trades = r.total_trades;
                sr.win_rate = r.win_rate;
                sr.total_swap = r.total_swap_charged;
                sr.notes = "RECOMMENDED | Buys: " + std::to_string(strategy.GetBuys()) +
                           " Sells: " + std::to_string(strategy.GetSells());
                results.push_back(sr);
                std::cout << "  XAGUSD*: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                          << " | DD: " << sr.max_dd_pct << "% | Buys: " << strategy.GetBuys()
                          << " Sells: " << strategy.GetSells()
                          << " | Swap: $" << r.total_swap_charged << std::endl;
            } catch (const std::exception& e) {
                std::cout << "  XAGUSD: ERROR - " << e.what() << std::endl;
            }
        }
    }
    std::cout << std::endl;

    // ========================================================================
    // STRATEGY 2: Gamma Scalping
    // Recommended: XAUUSD
    // ========================================================================
    std::cout << "--- Strategy 2: Gamma Scalping (Synthetic Long Gamma) ---" << std::endl;
    if (has_gold) {
        auto config = CreateGoldConfig(gold_path);
        try {
            TickBasedEngine engine(config);
            GammaScalper::Config gc;
            gc.base_lots = 0.03;
            gc.scalp_lots = 0.01;
            gc.delta_band = 0.15;        // Rehedge at 0.15% move
            gc.harvest_profit = 0.80;    // $0.80 per 0.01 lot to harvest
            gc.max_position_lots = 0.30;
            gc.min_position_lots = 0.01;
            gc.contract_size = 100.0;
            gc.leverage = 500.0;
            gc.lookback_ticks = 200;
            gc.warmup_ticks = 500;
            GammaScalper strategy(gc);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "2. Gamma Scalping";
            sr.instrument = "XAUUSD*";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "RECOMMENDED | Scalps: " + std::to_string(strategy.GetScalpCount()) +
                       " Harvests: " + std::to_string(strategy.GetHarvestCount());
            results.push_back(sr);
            std::cout << "  XAUUSD*: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "% | Scalps: " << strategy.GetScalpCount()
                      << " | Harvests: " << strategy.GetHarvestCount()
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAUUSD: ERROR - " << e.what() << std::endl;
        }
    }
    std::cout << std::endl;

    // ========================================================================
    // STRATEGY 3: Stochastic Resonance
    // Recommended: USDJPY | Also test on: XAUUSD
    // ========================================================================
    std::cout << "--- Strategy 3: Stochastic Resonance ---" << std::endl;
    if (has_gold) {
        auto config = CreateGoldConfig(gold_path);
        try {
            TickBasedEngine engine(config);
            StochasticResonance::Config src;
            src.barrier_lookback_hours = 12.0;
            src.noise_lookback_hours = 2.0;
            src.resonance_low = 0.5;
            src.resonance_high = 2.0;
            src.entry_distance_pct = 0.15;
            src.tp_distance_pct = 0.12;
            src.sl_distance_pct = 0.40;
            src.lot_size = 0.02;
            src.contract_size = 100.0;
            src.leverage = 500.0;
            src.max_positions = 5;
            src.warmup_ticks = 10000;
            src.ticks_per_hour = 3000;
            StochasticResonance strategy(src);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "3. Stochastic Resonance";
            sr.instrument = "XAUUSD";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "Entries: " + std::to_string(strategy.GetResonanceEntries()) +
                       " Rejected: " + std::to_string(strategy.GetNoiseRejections());
            results.push_back(sr);
            std::cout << "  XAUUSD: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "% | Entries: " << strategy.GetResonanceEntries()
                      << " | Rejected: " << strategy.GetNoiseRejections()
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAUUSD: ERROR - " << e.what() << std::endl;
        }
    }

    if (has_usdjpy) {
        auto config = CreateUSDJPYConfig(usdjpy_path);
        try {
            TickBasedEngine engine(config);
            StochasticResonance::Config src;
            src.barrier_lookback_hours = 12.0;
            src.noise_lookback_hours = 2.0;
            src.resonance_low = 0.5;
            src.resonance_high = 2.0;
            src.entry_distance_pct = 0.10;
            src.tp_distance_pct = 0.08;
            src.sl_distance_pct = 0.30;
            src.lot_size = 0.02;
            src.contract_size = 100000.0;
            src.leverage = 500.0;
            src.max_positions = 5;
            src.warmup_ticks = 10000;
            src.ticks_per_hour = 3000;
            StochasticResonance strategy(src);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "3. Stochastic Resonance";
            sr.instrument = "USDJPY*";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "RECOMMENDED | Entries: " + std::to_string(strategy.GetResonanceEntries());
            results.push_back(sr);
            std::cout << "  USDJPY*: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "% | Entries: " << strategy.GetResonanceEntries()
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  USDJPY: ERROR - " << e.what() << std::endl;
        }
    }
    std::cout << std::endl;

    // ========================================================================
    // STRATEGY 4: Entropy Harvesting (Maxwell's Demon)
    // Recommended: XAUUSD
    // ========================================================================
    std::cout << "--- Strategy 4: Entropy Harvesting (Maxwell's Demon) ---" << std::endl;
    if (has_gold) {
        auto config = CreateGoldConfig(gold_path);
        try {
            TickBasedEngine engine(config);
            EntropyHarvester::Config ec;
            ec.spacing = 1.50;
            ec.tp_distance = 1.00;
            ec.survive_pct = 13.0;
            ec.min_volume = 0.01;
            ec.max_volume = 10.0;
            ec.contract_size = 100.0;
            ec.leverage = 500.0;
            ec.reversal_window = 30;
            ec.direction_threshold = 6;
            ec.max_positions = 50;
            ec.require_reversal = true;
            ec.cooldown_ticks = 30;
            EntropyHarvester strategy(ec);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "4. Entropy Harvester";
            sr.instrument = "XAUUSD*";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "RECOMMENDED | Allowed: " + std::to_string(strategy.GetEntriesAllowed()) +
                       " Blocked: " + std::to_string(strategy.GetEntriesBlocked()) +
                       " Selectivity: " + std::to_string((int)strategy.GetSelectivityRatio()) + "%";
            results.push_back(sr);
            std::cout << "  XAUUSD*: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "%"
                      << " | Allowed/Blocked: " << strategy.GetEntriesAllowed() << "/" << strategy.GetEntriesBlocked()
                      << " | Selectivity: " << strategy.GetSelectivityRatio() << "%"
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAUUSD: ERROR - " << e.what() << std::endl;
        }

        // Also test without selectivity (control)
        {
            auto config2 = CreateGoldConfig(gold_path);
            try {
                TickBasedEngine engine(config2);
                EntropyHarvester::Config ec;
                ec.spacing = 1.50;
                ec.tp_distance = 1.00;
                ec.survive_pct = 13.0;
                ec.min_volume = 0.01;
                ec.max_volume = 10.0;
                ec.contract_size = 100.0;
                ec.leverage = 500.0;
                ec.reversal_window = 30;
                ec.direction_threshold = 6;
                ec.max_positions = 50;
                ec.require_reversal = false;  // NO selectivity (control)
                ec.cooldown_ticks = 30;
                EntropyHarvester strategy(ec);

                engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                    strategy.OnTick(tick, eng);
                });

                auto r = engine.GetResults();
                StrategyResult sr;
                sr.name = "4. Entropy (No Select)";
                sr.instrument = "XAUUSD";
                sr.initial_balance = r.initial_balance;
                sr.final_balance = r.final_balance;
                sr.return_multiple = r.final_balance / r.initial_balance;
                sr.max_dd_pct = strategy.GetMaxDDPct();
                sr.total_trades = r.total_trades;
                sr.win_rate = r.win_rate;
                sr.total_swap = r.total_swap_charged;
                sr.notes = "CONTROL (no demon) | Entries: " + std::to_string(strategy.GetEntriesAllowed());
                results.push_back(sr);
                std::cout << "  XAUUSD (no selectivity): $" << r.final_balance << " (" << sr.return_multiple << "x)"
                          << " | DD: " << sr.max_dd_pct << "%"
                          << " | Entries: " << strategy.GetEntriesAllowed()
                          << " | Swap: $" << r.total_swap_charged << std::endl;
            } catch (const std::exception& e) {
                std::cout << "  Control: ERROR - " << e.what() << std::endl;
            }
        }
    }
    std::cout << std::endl;

    // ========================================================================
    // STRATEGY 5: Liquidity Premium Capture
    // Recommended: XAGUSD | Also test on: XAUUSD
    // ========================================================================
    std::cout << "--- Strategy 5: Liquidity Premium Capture ---" << std::endl;
    if (has_gold) {
        auto config = CreateGoldConfig(gold_path);
        try {
            TickBasedEngine engine(config);
            LiquidityPremium::Config lc;
            lc.grid_levels = 15;
            lc.level_spacing = 1.00;
            lc.tp_distance = 0.80;
            lc.lot_size = 0.02;
            lc.max_total_lots = 0.40;
            lc.contract_size = 100.0;
            lc.leverage = 500.0;
            lc.refresh_interval = 10000;
            lc.grid_shift_threshold = 1.5;
            lc.warmup_ticks = 1000;
            LiquidityPremium strategy(lc);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "5. Liquidity Premium";
            sr.instrument = "XAUUSD";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "Fills: " + std::to_string(strategy.GetFills()) +
                       " Rebuilds: " + std::to_string(strategy.GetGridRebuilds());
            results.push_back(sr);
            std::cout << "  XAUUSD: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "% | Fills: " << strategy.GetFills()
                      << " | Rebuilds: " << strategy.GetGridRebuilds()
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAUUSD: ERROR - " << e.what() << std::endl;
        }
    }

    if (has_silver) {
        auto config = CreateSilverConfig(silver_path);
        try {
            TickBasedEngine engine(config);
            LiquidityPremium::Config lc;
            lc.grid_levels = 15;
            lc.level_spacing = 0.05;        // Silver: $0.05 spacing
            lc.tp_distance = 0.03;          // Silver: $0.03 TP
            lc.lot_size = 0.02;
            lc.max_total_lots = 0.40;
            lc.contract_size = 5000.0;
            lc.leverage = 500.0;
            lc.refresh_interval = 10000;
            lc.grid_shift_threshold = 1.5;
            lc.warmup_ticks = 1000;
            LiquidityPremium strategy(lc);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "5. Liquidity Premium";
            sr.instrument = "XAGUSD*";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "RECOMMENDED | Fills: " + std::to_string(strategy.GetFills());
            results.push_back(sr);
            std::cout << "  XAGUSD*: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "% | Fills: " << strategy.GetFills()
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAGUSD: ERROR - " << e.what() << std::endl;
        }
    }
    std::cout << std::endl;

    // ========================================================================
    // STRATEGY 6: Damped Oscillator Energy Extraction
    // Recommended: XAUUSD
    // ========================================================================
    std::cout << "--- Strategy 6: Damped Oscillator Energy Extraction ---" << std::endl;
    if (has_gold) {
        auto config = CreateGoldConfig(gold_path);
        try {
            TickBasedEngine engine(config);
            DampedOscillator::Config dc;
            dc.velocity_window = 30;
            dc.acceleration_window = 15;
            dc.min_velocity = 0.005;     // $0.005/tick minimum velocity
            dc.tp_amplitude = 1.00;      // $1.00 expected oscillation
            dc.lot_size = 0.02;
            dc.max_lots = 0.40;
            dc.contract_size = 100.0;
            dc.leverage = 500.0;
            dc.cooldown_ticks = 50;
            dc.max_positions = 20;
            dc.warmup_ticks = 500;
            dc.energy_harvest_ratio = 0.6;
            DampedOscillator strategy(dc);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "6. Damped Oscillator";
            sr.instrument = "XAUUSD*";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "RECOMMENDED | Crossings: " + std::to_string(strategy.GetZeroCrossings()) +
                       " Entries: " + std::to_string(strategy.GetEntries()) +
                       " Harvests: " + std::to_string(strategy.GetEnergyHarvests());
            results.push_back(sr);
            std::cout << "  XAUUSD*: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "%"
                      << " | Zero-crossings: " << strategy.GetZeroCrossings()
                      << " | Entries: " << strategy.GetEntries()
                      << " | Harvests: " << strategy.GetEnergyHarvests()
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAUUSD: ERROR - " << e.what() << std::endl;
        }
    }
    std::cout << std::endl;

    // ========================================================================
    // STRATEGY 7: Asymmetric Volatility Harvesting
    // Recommended: XAGUSD | Also test on: XAUUSD
    // ========================================================================
    std::cout << "--- Strategy 7: Asymmetric Volatility Harvesting ---" << std::endl;
    if (has_gold) {
        auto config = CreateGoldConfig(gold_path);
        try {
            TickBasedEngine engine(config);
            AsymmetricVol::Config ac;
            ac.velocity_window = 200;
            ac.fast_down_threshold = -0.08;   // 0.08% drop over 200 ticks
            ac.slow_up_threshold = 0.02;      // 0.02% rise over 200 ticks
            ac.base_lots = 0.01;
            ac.max_scale = 4.0;
            ac.max_lots = 0.40;
            ac.fast_tp_pct = 0.08;            // 0.08% bounce TP
            ac.slow_tp_pct = 0.15;            // 0.15% gradual TP
            ac.sl_pct = 0.40;                 // 0.4% SL
            ac.contract_size = 100.0;
            ac.leverage = 500.0;
            ac.cooldown_ticks = 100;
            ac.max_positions = 20;
            ac.warmup_ticks = 500;
            AsymmetricVol strategy(ac);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "7. Asymmetric Vol";
            sr.instrument = "XAUUSD";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "Fast: " + std::to_string(strategy.GetFastEntries()) +
                       " Slow: " + std::to_string(strategy.GetSlowEntries()) +
                       " MaxVelDn: " + std::to_string(strategy.GetMaxVelocityDown()).substr(0,6) + "%";
            results.push_back(sr);
            std::cout << "  XAUUSD: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "%"
                      << " | Fast entries: " << strategy.GetFastEntries()
                      << " | Slow entries: " << strategy.GetSlowEntries()
                      << " | Max vel down: " << strategy.GetMaxVelocityDown() << "%"
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAUUSD: ERROR - " << e.what() << std::endl;
        }
    }

    if (has_silver) {
        auto config = CreateSilverConfig(silver_path);
        try {
            TickBasedEngine engine(config);
            AsymmetricVol::Config ac;
            ac.velocity_window = 200;
            ac.fast_down_threshold = -0.12;   // Silver more volatile
            ac.slow_up_threshold = 0.03;
            ac.base_lots = 0.01;
            ac.max_scale = 4.0;
            ac.max_lots = 0.20;              // Lower for silver (higher OMR)
            ac.fast_tp_pct = 0.10;
            ac.slow_tp_pct = 0.20;
            ac.sl_pct = 0.50;
            ac.contract_size = 5000.0;
            ac.leverage = 500.0;
            ac.cooldown_ticks = 100;
            ac.max_positions = 15;
            ac.warmup_ticks = 500;
            AsymmetricVol strategy(ac);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto r = engine.GetResults();
            StrategyResult sr;
            sr.name = "7. Asymmetric Vol";
            sr.instrument = "XAGUSD*";
            sr.initial_balance = r.initial_balance;
            sr.final_balance = r.final_balance;
            sr.return_multiple = r.final_balance / r.initial_balance;
            sr.max_dd_pct = strategy.GetMaxDDPct();
            sr.total_trades = r.total_trades;
            sr.win_rate = r.win_rate;
            sr.total_swap = r.total_swap_charged;
            sr.notes = "RECOMMENDED | Fast: " + std::to_string(strategy.GetFastEntries()) +
                       " Slow: " + std::to_string(strategy.GetSlowEntries());
            results.push_back(sr);
            std::cout << "  XAGUSD*: $" << r.final_balance << " (" << sr.return_multiple << "x)"
                      << " | DD: " << sr.max_dd_pct << "%"
                      << " | Fast: " << strategy.GetFastEntries()
                      << " | Slow: " << strategy.GetSlowEntries()
                      << " | Swap: $" << r.total_swap_charged << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  XAGUSD: ERROR - " << e.what() << std::endl;
        }
    }
    std::cout << std::endl;

    // ========================================================================
    // SUMMARY TABLE
    // ========================================================================
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY TABLE (* = recommended instrument)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(30) << "Strategy"
              << std::setw(10) << "Instr"
              << std::right
              << std::setw(12) << "Final$"
              << std::setw(9) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(10) << "WinRate"
              << std::setw(10) << "Swap$"
              << "  " << std::left << "Notes"
              << std::endl;
    std::cout << std::string(130, '-') << std::endl;

    for (const auto& r : results) {
        PrintResult(r);
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BASELINE COMPARISON: FillUpOscillation ADAPTIVE_SPACING" << std::endl;
    std::cout << "  Expected: ~6.57x return, ~67% max DD, ~10,334 trades" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
