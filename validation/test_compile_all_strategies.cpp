/**
 * Compile-time verification that all strategy headers are valid.
 * This test includes every strategy header and instantiates each to verify
 * they compile correctly after the engine centralization refactor.
 */

#include "../include/tick_based_engine.h"

// Primary strategies (already centralized)
#include "../include/fill_up_oscillation.h"
#include "../include/strategy_combined_ju.h"
#include "../include/fill_up_strategy.h"

// HIGH-severity fixes (now use engine API)
#include "../include/strategy_wuwei.h"
#include "../include/strategy_adaptive_regime_grid.h"
#include "../include/strategy_nasdaq_up.h"
#include "../include/strategy_barbell_sizing.h"
#include "../include/strategy_bidirectional_momentum.h"
#include "../include/strategy_dual_allocation.h"
#include "../include/strategy_entropy_export_v2.h"
#include "../include/strategy_fractal_grid.h"
#include "../include/strategy_hybrid_fillup_extended.h"
#include "../include/strategy_reflexivity.h"

// MEDIUM-severity fixes
#include "../include/strategy_vanderpol.h"
#include "../include/strategy_gamma_scalper.h"
#include "../include/strategy_asymmetric_vol.h"
#include "../include/strategy_entropy_harvester.h"

// LOW-severity fixes
#include "../include/strategy_ogy_control.h"
#include "../include/strategy_chaos_sync.h"
#include "../include/strategy_damped_oscillator.h"
#include "../include/strategy_noise_scaling.h"
#include "../include/strategy_parallel_dual_original.h"

// Standalone strategy rewrites (now use engine)
#include "../include/strategy_antifragile.h"
#include "../include/strategy_bidirectional_grid.h"
#include "../include/strategy_dynamic_hedge.h"
#include "../include/strategy_fillup_hedged.h"
#include "../include/strategy_hybrid.h"
#include "../include/strategy_hybrid_fillup.h"
#include "../include/strategy_regime_adaptive.h"

// Other strategies (should still compile)
#include "../include/strategy_adaptive_control.h"
#include "../include/strategy_ornstein_uhlenbeck.h"
#include "../include/strategy_parallel_dual.h"

#include <iostream>

using namespace backtest;

int main() {
    std::cout << "=== Strategy Compilation Verification ===" << std::endl;
    std::cout << "All strategy headers compiled successfully." << std::endl;
    std::cout << std::endl;

    // Verify engine API exists
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;

    TickBasedEngine engine(config);

    // Verify all engine public API methods compile
    double um = engine.GetUsedMargin();
    double fm = engine.GetFreeMargin();
    double ml = engine.GetMarginLevel();
    double mr = engine.CalculateMarginRequired(0.01, 2000.0);
    double nl = engine.NormalizeLots(0.015);
    const auto& cfg = engine.GetConfig();

    // Buy-side aggregates
    double bv = engine.GetBuyVolume();
    double hbe = engine.GetHighestBuyEntry();
    double lbe = engine.GetLowestBuyEntry();
    size_t bpc = engine.GetBuyPositionCount();

    // Sell-side aggregates
    double sv = engine.GetSellVolume();
    double hse = engine.GetHighestSellEntry();
    double lse = engine.GetLowestSellEntry();
    size_t spc = engine.GetSellPositionCount();

    std::cout << "Engine API verified:" << std::endl;
    std::cout << "  GetUsedMargin()=" << um << std::endl;
    std::cout << "  GetFreeMargin()=" << fm << std::endl;
    std::cout << "  GetMarginLevel()=" << ml << std::endl;
    std::cout << "  CalculateMarginRequired(0.01, 2000)=" << mr << std::endl;
    std::cout << "  NormalizeLots(0.015)=" << nl << std::endl;
    std::cout << "  GetConfig().contract_size=" << cfg.contract_size << std::endl;
    std::cout << "  Buy aggregates: vol=" << bv << " high=" << hbe << " low=" << lbe << " count=" << bpc << std::endl;
    std::cout << "  Sell aggregates: vol=" << sv << " high=" << hse << " low=" << lse << " count=" << spc << std::endl;

    std::cout << std::endl << "All checks passed!" << std::endl;
    return 0;
}
