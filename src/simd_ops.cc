// simd_ops.cc — Highway multi-target compilation unit
// Triggers per-target compilation of simd_ops-inl.h, then exports dispatch tables.

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "simd_ops-inl.h"
#include "hwy/foreach_target.h"
#include "simd_ops-inl.h"

#include "hwy/base.h"
#include <cstdio>

namespace simd {

// Export dispatch tables for each vectorized function
HWY_EXPORT(SumImpl);
HWY_EXPORT(MaxValueImpl);
HWY_EXPORT(MinValueImpl);
HWY_EXPORT(MaxDrawdownImpl);
HWY_EXPORT(CalculatePnlBatchImpl);
HWY_EXPORT(CheckSlTpBuyImpl);
HWY_EXPORT(CheckSlTpSellImpl);
HWY_EXPORT(TotalMarginBatchImpl);
HWY_EXPORT(TotalMarginCfdLeverageImpl);
HWY_EXPORT(TotalMarginCfdImpl);
HWY_EXPORT(TotalMarginForexImpl);
HWY_EXPORT(TotalMarginFuturesImpl);
HWY_EXPORT(TotalMarginForexNolevImpl);

// ---------------------------------------------------------------------------
// Public API — delegates to HWY_DYNAMIC_DISPATCH
// ---------------------------------------------------------------------------

void init() {
    // No-op: Highway auto-detects CPU features on first dispatch
}

double sum(const double* data, size_t n) {
    return HWY_DYNAMIC_DISPATCH(SumImpl)(data, n);
}

double max_value(const double* data, size_t n) {
    return HWY_DYNAMIC_DISPATCH(MaxValueImpl)(data, n);
}

double min_value(const double* data, size_t n) {
    return HWY_DYNAMIC_DISPATCH(MinValueImpl)(data, n);
}

double max_drawdown(const double* equity, size_t n) {
    return HWY_DYNAMIC_DISPATCH(MaxDrawdownImpl)(equity, n);
}

void calculate_pnl_batch(const double* entry_prices, const double* lot_sizes,
    double current_price, double contract_size,
    double* pnl_output, size_t n, bool is_buy)
{
    HWY_DYNAMIC_DISPATCH(CalculatePnlBatchImpl)(
        entry_prices, lot_sizes, current_price, contract_size,
        pnl_output, n, is_buy);
}

int check_sl_tp_buy(const double* stop_losses, const double* take_profits,
    double bid_price, size_t n, int* hit_indices, int* hit_types)
{
    return HWY_DYNAMIC_DISPATCH(CheckSlTpBuyImpl)(
        stop_losses, take_profits, bid_price, n, hit_indices, hit_types);
}

int check_sl_tp_sell(const double* stop_losses, const double* take_profits,
    double ask_price, size_t n, int* hit_indices, int* hit_types)
{
    return HWY_DYNAMIC_DISPATCH(CheckSlTpSellImpl)(
        stop_losses, take_profits, ask_price, n, hit_indices, hit_types);
}

double total_margin_batch(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double leverage)
{
    return HWY_DYNAMIC_DISPATCH(TotalMarginBatchImpl)(
        lot_sizes, prices, n, contract_size, leverage);
}

double total_margin_cfd_leverage(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double leverage, double margin_rate)
{
    return HWY_DYNAMIC_DISPATCH(TotalMarginCfdLeverageImpl)(
        lot_sizes, prices, n, contract_size, leverage, margin_rate);
}

double total_margin_cfd(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double margin_rate)
{
    return HWY_DYNAMIC_DISPATCH(TotalMarginCfdImpl)(
        lot_sizes, prices, n, contract_size, margin_rate);
}

double total_margin_forex(const double* lot_sizes, size_t n,
    double contract_size, double leverage, double margin_rate)
{
    return HWY_DYNAMIC_DISPATCH(TotalMarginForexImpl)(
        lot_sizes, n, contract_size, leverage, margin_rate);
}

double total_margin_futures(const double* lot_sizes, size_t n,
    double margin_initial)
{
    return HWY_DYNAMIC_DISPATCH(TotalMarginFuturesImpl)(
        lot_sizes, n, margin_initial);
}

double total_margin_forex_nolev(const double* lot_sizes, size_t n,
    double contract_size, double margin_rate)
{
    return HWY_DYNAMIC_DISPATCH(TotalMarginForexNolevImpl)(
        lot_sizes, n, contract_size, margin_rate);
}

void print_cpu_features() {
    const int64_t targets = hwy::SupportedTargets();
    std::printf("Highway supported targets: 0x%llx\n",
        static_cast<unsigned long long>(targets));
    std::printf("  Best available: %s\n",
        hwy::TargetName(hwy::SupportedTargets() & ~(hwy::SupportedTargets() - 1)));
    // Print individual targets
    if (targets & HWY_AVX3_ZEN4)  std::printf("  - AVX-512 (Zen4)\n");
    if (targets & HWY_AVX3_DL)   std::printf("  - AVX-512 DL\n");
    if (targets & HWY_AVX3)      std::printf("  - AVX-512\n");
    if (targets & HWY_AVX2)      std::printf("  - AVX2\n");
    if (targets & HWY_SSE4)      std::printf("  - SSE4\n");
    if (targets & HWY_SSE2)      std::printf("  - SSE2\n");
    if (targets & HWY_NEON)      std::printf("  - NEON\n");
    if (targets & HWY_SVE)       std::printf("  - SVE\n");
    if (targets & HWY_SVE2)      std::printf("  - SVE2\n");
    if (targets & HWY_EMU128)    std::printf("  - EMU128 (scalar fallback)\n");
    if (targets & HWY_SCALAR)    std::printf("  - Scalar\n");
}

}  // namespace simd
