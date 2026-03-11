// simd_ops-inl.h — Highway implementations (compiled per-target via foreach_target.h)
// NO include guards — this file is intentionally included multiple times.

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// ---------------------------------------------------------------------------
// Reductions
// ---------------------------------------------------------------------------

double SumImpl(const double* data, size_t n) {
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        acc = hn::Add(acc, hn::LoadU(d, data + i));
    }
    double result = hn::ReduceSum(d, acc);
    for (; i < n; ++i) result += data[i];
    return result;
}

double MaxValueImpl(const double* data, size_t n) {
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    if (n < N) {
        double m = data[0];
        for (size_t i = 1; i < n; ++i)
            if (data[i] > m) m = data[i];
        return m;
    }
    auto acc = hn::LoadU(d, data);
    size_t i = N;
    for (; i + N <= n; i += N) {
        acc = hn::Max(acc, hn::LoadU(d, data + i));
    }
    // Horizontal max: store to array and reduce
    HWY_ALIGN double buf[HWY_MAX_LANES_D(hn::ScalableTag<double>)];
    hn::Store(acc, d, buf);
    double m = buf[0];
    for (size_t j = 1; j < N; ++j)
        if (buf[j] > m) m = buf[j];
    // Scalar remainder
    for (; i < n; ++i)
        if (data[i] > m) m = data[i];
    return m;
}

double MinValueImpl(const double* data, size_t n) {
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    if (n < N) {
        double m = data[0];
        for (size_t i = 1; i < n; ++i)
            if (data[i] < m) m = data[i];
        return m;
    }
    auto acc = hn::LoadU(d, data);
    size_t i = N;
    for (; i + N <= n; i += N) {
        acc = hn::Min(acc, hn::LoadU(d, data + i));
    }
    HWY_ALIGN double buf[HWY_MAX_LANES_D(hn::ScalableTag<double>)];
    hn::Store(acc, d, buf);
    double m = buf[0];
    for (size_t j = 1; j < N; ++j)
        if (buf[j] < m) m = buf[j];
    for (; i < n; ++i)
        if (data[i] < m) m = data[i];
    return m;
}

// Max drawdown is inherently sequential (running peak), implemented as scalar
double MaxDrawdownImpl(const double* equity, size_t n) {
    if (n < 2) return 0.0;
    double peak = equity[0];
    double max_dd = 0.0;
    for (size_t i = 1; i < n; ++i) {
        if (equity[i] > peak) peak = equity[i];
        double dd = peak - equity[i];
        if (dd > max_dd) max_dd = dd;
    }
    return max_dd;
}

// ---------------------------------------------------------------------------
// P&L batch
// ---------------------------------------------------------------------------

void CalculatePnlBatchImpl(const double* entry_prices, const double* lot_sizes,
    double current_price, double contract_size,
    double* pnl_output, size_t n, bool is_buy)
{
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_price = hn::Set(d, current_price);
    const auto v_contract = hn::Set(d, contract_size);
    size_t i = 0;

    if (is_buy) {
        // PnL = (current - entry) * lot * contract
        for (; i + N <= n; i += N) {
            auto entry = hn::LoadU(d, entry_prices + i);
            auto lots = hn::LoadU(d, lot_sizes + i);
            auto diff = hn::Sub(v_price, entry);
            auto pnl = hn::Mul(hn::Mul(diff, lots), v_contract);
            hn::StoreU(pnl, d, pnl_output + i);
        }
        for (; i < n; ++i) {
            pnl_output[i] = (current_price - entry_prices[i]) * lot_sizes[i] * contract_size;
        }
    } else {
        // PnL = (entry - current) * lot * contract
        for (; i + N <= n; i += N) {
            auto entry = hn::LoadU(d, entry_prices + i);
            auto lots = hn::LoadU(d, lot_sizes + i);
            auto diff = hn::Sub(entry, v_price);
            auto pnl = hn::Mul(hn::Mul(diff, lots), v_contract);
            hn::StoreU(pnl, d, pnl_output + i);
        }
        for (; i < n; ++i) {
            pnl_output[i] = (entry_prices[i] - current_price) * lot_sizes[i] * contract_size;
        }
    }
}

// ---------------------------------------------------------------------------
// SL/TP batch checking
// ---------------------------------------------------------------------------

// Uses SIMD for fast-skip: vectorized check per chunk, scalar extraction on hits
int CheckSlTpBuyImpl(const double* stop_losses, const double* take_profits,
    double bid_price, size_t n, int* hit_indices, int* hit_types)
{
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_price = hn::Set(d, bid_price);
    const auto v_zero = hn::Zero(d);
    int hit_count = 0;
    size_t i = 0;

    for (; i + N <= n; i += N) {
        auto sl = hn::LoadU(d, stop_losses + i);
        auto tp = hn::LoadU(d, take_profits + i);

        // SL hit: sl > 0 && price <= sl
        auto sl_active = hn::Gt(sl, v_zero);
        auto sl_hit = hn::And(sl_active, hn::Le(v_price, sl));
        // TP hit: tp > 0 && price >= tp
        auto tp_active = hn::Gt(tp, v_zero);
        auto tp_hit = hn::And(tp_active, hn::Ge(v_price, tp));

        auto any_hit = hn::Or(sl_hit, tp_hit);
        if (!hn::AllFalse(d, any_hit)) {
            // Scalar extraction for this chunk
            for (size_t j = 0; j < N && (i + j) < n; ++j) {
                size_t idx = i + j;
                if (stop_losses[idx] > 0 && bid_price <= stop_losses[idx]) {
                    hit_indices[hit_count] = static_cast<int>(idx);
                    hit_types[hit_count] = 1;  // SL
                    hit_count++;
                } else if (take_profits[idx] > 0 && bid_price >= take_profits[idx]) {
                    hit_indices[hit_count] = static_cast<int>(idx);
                    hit_types[hit_count] = 2;  // TP
                    hit_count++;
                }
            }
        }
    }
    // Scalar remainder
    for (; i < n; ++i) {
        if (stop_losses[i] > 0 && bid_price <= stop_losses[i]) {
            hit_indices[hit_count] = static_cast<int>(i);
            hit_types[hit_count] = 1;
            hit_count++;
        } else if (take_profits[i] > 0 && bid_price >= take_profits[i]) {
            hit_indices[hit_count] = static_cast<int>(i);
            hit_types[hit_count] = 2;
            hit_count++;
        }
    }
    return hit_count;
}

int CheckSlTpSellImpl(const double* stop_losses, const double* take_profits,
    double ask_price, size_t n, int* hit_indices, int* hit_types)
{
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_price = hn::Set(d, ask_price);
    const auto v_zero = hn::Zero(d);
    int hit_count = 0;
    size_t i = 0;

    for (; i + N <= n; i += N) {
        auto sl = hn::LoadU(d, stop_losses + i);
        auto tp = hn::LoadU(d, take_profits + i);

        // SL hit: sl > 0 && price >= sl
        auto sl_active = hn::Gt(sl, v_zero);
        auto sl_hit = hn::And(sl_active, hn::Ge(v_price, sl));
        // TP hit: tp > 0 && price <= tp
        auto tp_active = hn::Gt(tp, v_zero);
        auto tp_hit = hn::And(tp_active, hn::Le(v_price, tp));

        auto any_hit = hn::Or(sl_hit, tp_hit);
        if (!hn::AllFalse(d, any_hit)) {
            for (size_t j = 0; j < N && (i + j) < n; ++j) {
                size_t idx = i + j;
                if (stop_losses[idx] > 0 && ask_price >= stop_losses[idx]) {
                    hit_indices[hit_count] = static_cast<int>(idx);
                    hit_types[hit_count] = 1;
                    hit_count++;
                } else if (take_profits[idx] > 0 && ask_price <= take_profits[idx]) {
                    hit_indices[hit_count] = static_cast<int>(idx);
                    hit_types[hit_count] = 2;
                    hit_count++;
                }
            }
        }
    }
    for (; i < n; ++i) {
        if (stop_losses[i] > 0 && ask_price >= stop_losses[i]) {
            hit_indices[hit_count] = static_cast<int>(i);
            hit_types[hit_count] = 1;
            hit_count++;
        } else if (take_profits[i] > 0 && ask_price <= take_profits[i]) {
            hit_indices[hit_count] = static_cast<int>(i);
            hit_types[hit_count] = 2;
            hit_count++;
        }
    }
    return hit_count;
}

// ---------------------------------------------------------------------------
// Margin calculations
// ---------------------------------------------------------------------------

// margin = sum(lot * price * contract / leverage)
double TotalMarginBatchImpl(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double leverage)
{
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_factor = hn::Set(d, contract_size / leverage);
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        auto lots = hn::LoadU(d, lot_sizes + i);
        auto prc = hn::LoadU(d, prices + i);
        acc = hn::Add(acc, hn::Mul(hn::Mul(lots, prc), v_factor));
    }
    double result = hn::ReduceSum(d, acc);
    for (; i < n; ++i)
        result += lot_sizes[i] * prices[i] * contract_size / leverage;
    return result;
}

// margin = sum(lot * price * contract / leverage * margin_rate)
double TotalMarginCfdLeverageImpl(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double leverage, double margin_rate)
{
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_factor = hn::Set(d, contract_size / leverage * margin_rate);
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        auto lots = hn::LoadU(d, lot_sizes + i);
        auto prc = hn::LoadU(d, prices + i);
        acc = hn::Add(acc, hn::Mul(hn::Mul(lots, prc), v_factor));
    }
    double result = hn::ReduceSum(d, acc);
    for (; i < n; ++i)
        result += lot_sizes[i] * prices[i] * contract_size / leverage * margin_rate;
    return result;
}

// margin = sum(lot * price * contract * margin_rate)
double TotalMarginCfdImpl(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double margin_rate)
{
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_factor = hn::Set(d, contract_size * margin_rate);
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        auto lots = hn::LoadU(d, lot_sizes + i);
        auto prc = hn::LoadU(d, prices + i);
        acc = hn::Add(acc, hn::Mul(hn::Mul(lots, prc), v_factor));
    }
    double result = hn::ReduceSum(d, acc);
    for (; i < n; ++i)
        result += lot_sizes[i] * prices[i] * contract_size * margin_rate;
    return result;
}

// margin = sum(lot * contract / leverage * margin_rate)
double TotalMarginForexImpl(const double* lot_sizes, size_t n,
    double contract_size, double leverage, double margin_rate)
{
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_factor = hn::Set(d, contract_size / leverage * margin_rate);
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        auto lots = hn::LoadU(d, lot_sizes + i);
        acc = hn::Add(acc, hn::Mul(lots, v_factor));
    }
    double result = hn::ReduceSum(d, acc);
    for (; i < n; ++i)
        result += lot_sizes[i] * contract_size / leverage * margin_rate;
    return result;
}

// margin = sum(lot * margin_initial)
double TotalMarginFuturesImpl(const double* lot_sizes, size_t n,
    double margin_initial)
{
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_factor = hn::Set(d, margin_initial);
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        auto lots = hn::LoadU(d, lot_sizes + i);
        acc = hn::Add(acc, hn::Mul(lots, v_factor));
    }
    double result = hn::ReduceSum(d, acc);
    for (; i < n; ++i)
        result += lot_sizes[i] * margin_initial;
    return result;
}

// margin = sum(lot * contract * margin_rate)
double TotalMarginForexNolevImpl(const double* lot_sizes, size_t n,
    double contract_size, double margin_rate)
{
    if (n == 0) return 0.0;
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto v_factor = hn::Set(d, contract_size * margin_rate);
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + N <= n; i += N) {
        auto lots = hn::LoadU(d, lot_sizes + i);
        acc = hn::Add(acc, hn::Mul(lots, v_factor));
    }
    double result = hn::ReduceSum(d, acc);
    for (; i < n; ++i)
        result += lot_sizes[i] * contract_size * margin_rate;
    return result;
}

}  // namespace HWY_NAMESPACE
}  // namespace simd
HWY_AFTER_NAMESPACE();
