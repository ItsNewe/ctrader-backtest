/**
 * SIMD Intrinsics for Vectorized Operations
 *
 * Supports: SSE2, SSE4.1, AVX2, AVX-512
 * Auto-detects best available instruction set at runtime
 *
 * Usage:
 *   #include "simd_intrinsics.h"
 *   simd::init();  // Detect CPU features
 *
 *   std::vector<double> prices(1000);
 *   std::vector<double> sma(1000);
 *   simd::sma_vectorized(prices.data(), sma.data(), 1000, 14);
 */

#ifndef SIMD_INTRINSICS_H
#define SIMD_INTRINSICS_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// Platform detection
#if defined(_MSC_VER)
    #include <intrin.h>
    #define SIMD_MSVC 1
#elif defined(__GNUC__) || defined(__clang__)
    #include <cpuid.h>
    #include <x86intrin.h>
    #define SIMD_GCC 1
#endif

// Always available on x86-64
#include <immintrin.h>

namespace simd {

//=============================================================================
// CPU Feature Detection
//=============================================================================

struct CPUFeatures {
    bool sse2 = false;
    bool sse41 = false;
    bool avx = false;
    bool avx2 = false;
    bool avx512f = false;
    bool avx512dq = false;
    bool fma = false;
};

inline CPUFeatures g_cpu_features;

inline void detect_cpu_features() {
    int cpuinfo[4] = {0};

#if SIMD_MSVC
    __cpuid(cpuinfo, 1);
#elif SIMD_GCC
    __cpuid(1, cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);
#endif

    g_cpu_features.sse2  = (cpuinfo[3] & (1 << 26)) != 0;
    g_cpu_features.sse41 = (cpuinfo[2] & (1 << 19)) != 0;
    g_cpu_features.avx   = (cpuinfo[2] & (1 << 28)) != 0;
    g_cpu_features.fma   = (cpuinfo[2] & (1 << 12)) != 0;

    // Check for AVX2 and AVX-512
    if (g_cpu_features.avx) {
#if SIMD_MSVC
        __cpuidex(cpuinfo, 7, 0);
#elif SIMD_GCC
        __cpuid_count(7, 0, cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);
#endif
        g_cpu_features.avx2     = (cpuinfo[1] & (1 << 5)) != 0;
        g_cpu_features.avx512f  = (cpuinfo[1] & (1 << 16)) != 0;
        g_cpu_features.avx512dq = (cpuinfo[1] & (1 << 17)) != 0;
    }
}

inline void init() {
    detect_cpu_features();
}

inline bool has_avx512() { return g_cpu_features.avx512f; }
inline bool has_avx2() { return g_cpu_features.avx2; }
inline bool has_avx() { return g_cpu_features.avx; }
inline bool has_sse41() { return g_cpu_features.sse41; }

//=============================================================================
// Vector Sum (for SMA calculations)
//=============================================================================

// SSE2 version - sum 2 doubles
inline double sum_sse2(const double* data, size_t n) {
    __m128d sum = _mm_setzero_pd();
    size_t i = 0;

    for (; i + 2 <= n; i += 2) {
        __m128d v = _mm_loadu_pd(data + i);
        sum = _mm_add_pd(sum, v);
    }

    // Horizontal sum
    __m128d shuf = _mm_shuffle_pd(sum, sum, 1);
    sum = _mm_add_pd(sum, shuf);

    double result;
    _mm_store_sd(&result, sum);

    // Handle remainder
    for (; i < n; ++i) {
        result += data[i];
    }

    return result;
}

// AVX2 version - sum 4 doubles at once (2x unrolled with prefetch)
inline double sum_avx2(const double* data, size_t n) {
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();
    size_t i = 0;

    // Prefetch first cache line
    _mm_prefetch(reinterpret_cast<const char*>(data), _MM_HINT_T0);

    // 2x unrolled loop - process 8 doubles per iteration
    for (; i + 8 <= n; i += 8) {
        // Prefetch ahead (64 bytes = 8 doubles ahead)
        _mm_prefetch(reinterpret_cast<const char*>(data + i + 16), _MM_HINT_T0);

        __m256d v0 = _mm256_loadu_pd(data + i);
        __m256d v1 = _mm256_loadu_pd(data + i + 4);
        sum0 = _mm256_add_pd(sum0, v0);
        sum1 = _mm256_add_pd(sum1, v1);
    }

    // Handle 4-element chunk if remaining
    if (i + 4 <= n) {
        __m256d v = _mm256_loadu_pd(data + i);
        sum0 = _mm256_add_pd(sum0, v);
        i += 4;
    }

    // Combine accumulators
    sum0 = _mm256_add_pd(sum0, sum1);

    // Horizontal sum (256-bit to 128-bit)
    __m128d lo = _mm256_castpd256_pd128(sum0);
    __m128d hi = _mm256_extractf128_pd(sum0, 1);
    lo = _mm_add_pd(lo, hi);

    __m128d shuf = _mm_shuffle_pd(lo, lo, 1);
    lo = _mm_add_pd(lo, shuf);

    double result;
    _mm_store_sd(&result, lo);

    // Handle remainder
    for (; i < n; ++i) {
        result += data[i];
    }

    return result;
}

#ifdef __AVX512F__
// AVX-512 version - sum 8 doubles at once (2x unrolled with prefetch)
inline double sum_avx512(const double* data, size_t n) {
    __m512d sum0 = _mm512_setzero_pd();
    __m512d sum1 = _mm512_setzero_pd();
    size_t i = 0;

    _mm_prefetch(reinterpret_cast<const char*>(data), _MM_HINT_T0);

    // 2x unrolled - process 16 doubles per iteration
    for (; i + 16 <= n; i += 16) {
        _mm_prefetch(reinterpret_cast<const char*>(data + i + 32), _MM_HINT_T0);

        __m512d v0 = _mm512_loadu_pd(data + i);
        __m512d v1 = _mm512_loadu_pd(data + i + 8);
        sum0 = _mm512_add_pd(sum0, v0);
        sum1 = _mm512_add_pd(sum1, v1);
    }

    // Handle 8-element chunk
    if (i + 8 <= n) {
        __m512d v = _mm512_loadu_pd(data + i);
        sum0 = _mm512_add_pd(sum0, v);
        i += 8;
    }

    // Combine and reduce
    sum0 = _mm512_add_pd(sum0, sum1);
    double result = _mm512_reduce_add_pd(sum0);

    // Handle remainder
    for (; i < n; ++i) {
        result += data[i];
    }

    return result;
}

// AVX-512 max
inline double max_avx512(const double* data, size_t n) {
    if (n == 0) return 0.0;

    __m512d max_vec = _mm512_set1_pd(-1e308);
    size_t i = 0;

    // 2x unrolled
    for (; i + 16 <= n; i += 16) {
        __m512d v0 = _mm512_loadu_pd(data + i);
        __m512d v1 = _mm512_loadu_pd(data + i + 8);
        max_vec = _mm512_max_pd(max_vec, v0);
        max_vec = _mm512_max_pd(max_vec, v1);
    }

    for (; i + 8 <= n; i += 8) {
        __m512d v = _mm512_loadu_pd(data + i);
        max_vec = _mm512_max_pd(max_vec, v);
    }

    double result = _mm512_reduce_max_pd(max_vec);

    for (; i < n; ++i) {
        if (data[i] > result) result = data[i];
    }

    return result;
}

// AVX-512 min
inline double min_avx512(const double* data, size_t n) {
    if (n == 0) return 0.0;

    __m512d min_vec = _mm512_set1_pd(1e308);
    size_t i = 0;

    for (; i + 16 <= n; i += 16) {
        __m512d v0 = _mm512_loadu_pd(data + i);
        __m512d v1 = _mm512_loadu_pd(data + i + 8);
        min_vec = _mm512_min_pd(min_vec, v0);
        min_vec = _mm512_min_pd(min_vec, v1);
    }

    for (; i + 8 <= n; i += 8) {
        __m512d v = _mm512_loadu_pd(data + i);
        min_vec = _mm512_min_pd(min_vec, v);
    }

    double result = _mm512_reduce_min_pd(min_vec);

    for (; i < n; ++i) {
        if (data[i] < result) result = data[i];
    }

    return result;
}

// AVX-512 P/L batch calculation
inline void calculate_pnl_batch_avx512(
    const double* entry_prices, const double* lot_sizes,
    double current_price, double contract_size,
    double* pnl_output, size_t n, bool is_buy) {

    __m512d price_vec = _mm512_set1_pd(current_price);
    __m512d contract_vec = _mm512_set1_pd(contract_size);

    _mm_prefetch(reinterpret_cast<const char*>(entry_prices), _MM_HINT_T0);
    _mm_prefetch(reinterpret_cast<const char*>(lot_sizes), _MM_HINT_T0);

    size_t i = 0;

    // Process 16 positions per iteration
    for (; i + 16 <= n; i += 16) {
        _mm_prefetch(reinterpret_cast<const char*>(entry_prices + i + 32), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(lot_sizes + i + 32), _MM_HINT_T0);

        __m512d entry0 = _mm512_loadu_pd(entry_prices + i);
        __m512d entry1 = _mm512_loadu_pd(entry_prices + i + 8);
        __m512d lots0 = _mm512_loadu_pd(lot_sizes + i);
        __m512d lots1 = _mm512_loadu_pd(lot_sizes + i + 8);

        __m512d diff0, diff1;
        if (is_buy) {
            diff0 = _mm512_sub_pd(price_vec, entry0);
            diff1 = _mm512_sub_pd(price_vec, entry1);
        } else {
            diff0 = _mm512_sub_pd(entry0, price_vec);
            diff1 = _mm512_sub_pd(entry1, price_vec);
        }

        __m512d pnl0 = _mm512_mul_pd(_mm512_mul_pd(diff0, lots0), contract_vec);
        __m512d pnl1 = _mm512_mul_pd(_mm512_mul_pd(diff1, lots1), contract_vec);

        _mm512_storeu_pd(pnl_output + i, pnl0);
        _mm512_storeu_pd(pnl_output + i + 8, pnl1);
    }

    // Handle remaining with 8-element chunks
    for (; i + 8 <= n; i += 8) {
        __m512d entry = _mm512_loadu_pd(entry_prices + i);
        __m512d lots = _mm512_loadu_pd(lot_sizes + i);

        __m512d diff = is_buy ? _mm512_sub_pd(price_vec, entry) : _mm512_sub_pd(entry, price_vec);
        __m512d pnl = _mm512_mul_pd(_mm512_mul_pd(diff, lots), contract_vec);
        _mm512_storeu_pd(pnl_output + i, pnl);
    }

    // Scalar remainder
    for (; i < n; ++i) {
        double diff = is_buy ? (current_price - entry_prices[i]) : (entry_prices[i] - current_price);
        pnl_output[i] = diff * lot_sizes[i] * contract_size;
    }
}
#endif

// Auto-select best sum function
inline double sum(const double* data, size_t n) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f) return sum_avx512(data, n);
#endif
    if (g_cpu_features.avx2) return sum_avx2(data, n);
    return sum_sse2(data, n);
}

//=============================================================================
// Simple Moving Average (SMA)
//=============================================================================

inline void sma_scalar(const double* input, double* output, size_t n, int period) {
    if (n < static_cast<size_t>(period)) return;

    // First SMA value
    double sum = 0;
    for (int i = 0; i < period; ++i) {
        sum += input[i];
    }
    output[period - 1] = sum / period;

    // Subsequent values (sliding window)
    for (size_t i = period; i < n; ++i) {
        sum += input[i] - input[i - period];
        output[i] = sum / period;
    }
}

inline void sma_avx2(const double* input, double* output, size_t n, int period) {
    if (n < static_cast<size_t>(period)) return;

    // Initial sum using vectorized sum
    double sum_val = sum_avx2(input, period);
    output[period - 1] = sum_val / period;

    __m256d inv_period = _mm256_set1_pd(1.0 / period);

    // Process in chunks of 4
    size_t i = period;
    for (; i + 4 <= n; i += 4) {
        // Load 4 new values and 4 old values
        __m256d new_vals = _mm256_loadu_pd(input + i);
        __m256d old_vals = _mm256_loadu_pd(input + i - period);

        // Calculate differences
        __m256d diff = _mm256_sub_pd(new_vals, old_vals);

        // Cumulative update (need to scan-add the differences)
        // This is tricky for SIMD, so we do a partial optimization
        double d0 = input[i] - input[i - period];
        double d1 = input[i+1] - input[i+1 - period];
        double d2 = input[i+2] - input[i+2 - period];
        double d3 = input[i+3] - input[i+3 - period];

        sum_val += d0; output[i] = sum_val / period;
        sum_val += d1; output[i+1] = sum_val / period;
        sum_val += d2; output[i+2] = sum_val / period;
        sum_val += d3; output[i+3] = sum_val / period;
    }

    // Handle remainder
    for (; i < n; ++i) {
        sum_val += input[i] - input[i - period];
        output[i] = sum_val / period;
    }
}

inline void sma_vectorized(const double* input, double* output, size_t n, int period) {
    if (g_cpu_features.avx2) {
        sma_avx2(input, output, n, period);
    } else {
        sma_scalar(input, output, n, period);
    }
}

//=============================================================================
// Exponential Moving Average (EMA)
//=============================================================================

inline void ema_scalar(const double* input, double* output, size_t n, int period) {
    if (n == 0) return;

    double k = 2.0 / (period + 1);
    double k_inv = 1.0 - k;

    output[0] = input[0];
    for (size_t i = 1; i < n; ++i) {
        output[i] = input[i] * k + output[i-1] * k_inv;
    }
}

// EMA is inherently sequential, but we can still use SIMD for the multiply-add
inline void ema_optimized(const double* input, double* output, size_t n, int period) {
    if (n == 0) return;

    double k = 2.0 / (period + 1);
    double k_inv = 1.0 - k;

    output[0] = input[0];

    // Use FMA if available
    if (g_cpu_features.fma) {
        for (size_t i = 1; i < n; ++i) {
            // ema = input * k + prev_ema * (1-k)
            // Using FMA: ema = fma(input, k, prev_ema * (1-k))
            output[i] = std::fma(input[i], k, output[i-1] * k_inv);
        }
    } else {
        for (size_t i = 1; i < n; ++i) {
            output[i] = input[i] * k + output[i-1] * k_inv;
        }
    }
}

inline void ema_vectorized(const double* input, double* output, size_t n, int period) {
    ema_optimized(input, output, n, period);
}

//=============================================================================
// RSI (Relative Strength Index)
//=============================================================================

// Vectorized calculation of price changes, gains, and losses
inline void calculate_changes_gains_losses_avx2(
    const double* input, double* changes, double* gains, double* losses, size_t n) {

    __m256d zero = _mm256_setzero_pd();

    size_t i = 1;
    // Process 4 changes at a time
    for (; i + 4 <= n; i += 4) {
        __m256d curr = _mm256_loadu_pd(input + i);
        __m256d prev = _mm256_loadu_pd(input + i - 1);

        __m256d change = _mm256_sub_pd(curr, prev);
        _mm256_storeu_pd(changes + i, change);

        // gains = max(change, 0)
        __m256d gain = _mm256_max_pd(change, zero);
        _mm256_storeu_pd(gains + i, gain);

        // losses = max(-change, 0) = -min(change, 0)
        __m256d loss = _mm256_sub_pd(zero, _mm256_min_pd(change, zero));
        _mm256_storeu_pd(losses + i, loss);
    }

    // Handle remainder
    for (; i < n; ++i) {
        double change = input[i] - input[i-1];
        changes[i] = change;
        gains[i] = change > 0 ? change : 0;
        losses[i] = change < 0 ? -change : 0;
    }
}

inline void rsi_avx2(const double* input, double* output, size_t n, int period) {
    if (n < static_cast<size_t>(period) + 1) return;

    // Allocate temporary arrays for vectorized gain/loss calculation
    std::vector<double> changes(n), gains(n), losses(n);

    // Vectorized calculation of all changes, gains, losses
    calculate_changes_gains_losses_avx2(input, changes.data(), gains.data(), losses.data(), n);

    // Sum initial gains/losses using vectorized sum
    double avg_gain = sum_avx2(gains.data() + 1, period) / period;
    double avg_loss = sum_avx2(losses.data() + 1, period) / period;

    // First RSI value
    if (avg_loss == 0) output[period] = 100.0;
    else {
        double rs = avg_gain / avg_loss;
        output[period] = 100.0 - 100.0 / (1.0 + rs);
    }

    // Subsequent values with Wilder smoothing (sequential due to dependency)
    double period_minus_1 = period - 1;
    double inv_period = 1.0 / period;

    for (size_t i = period + 1; i < n; ++i) {
        // Use FMA for smoothing calculation
        avg_gain = std::fma(avg_gain, period_minus_1 * inv_period, gains[i] * inv_period);
        avg_loss = std::fma(avg_loss, period_minus_1 * inv_period, losses[i] * inv_period);

        if (avg_loss == 0) output[i] = 100.0;
        else {
            double rs = avg_gain / avg_loss;
            output[i] = 100.0 - 100.0 / (1.0 + rs);
        }
    }
}

inline void rsi_vectorized(const double* input, double* output, size_t n, int period) {
    if (g_cpu_features.avx2) {
        rsi_avx2(input, output, n, period);
    } else {
        // Scalar fallback
        if (n < static_cast<size_t>(period) + 1) return;

        double avg_gain = 0, avg_loss = 0;
        for (int i = 1; i <= period; ++i) {
            double change = input[i] - input[i-1];
            if (change > 0) avg_gain += change;
            else avg_loss -= change;
        }
        avg_gain /= period;
        avg_loss /= period;

        if (avg_loss == 0) output[period] = 100.0;
        else output[period] = 100.0 - 100.0 / (1.0 + avg_gain / avg_loss);

        double period_minus_1 = period - 1;
        for (size_t i = period + 1; i < n; ++i) {
            double change = input[i] - input[i-1];
            double gain = change > 0 ? change : 0;
            double loss = change < 0 ? -change : 0;
            avg_gain = (avg_gain * period_minus_1 + gain) / period;
            avg_loss = (avg_loss * period_minus_1 + loss) / period;
            if (avg_loss == 0) output[i] = 100.0;
            else output[i] = 100.0 - 100.0 / (1.0 + avg_gain / avg_loss);
        }
    }
}

//=============================================================================
// ATR (Average True Range)
//=============================================================================

inline void atr_avx2(const double* high, const double* low, const double* close,
                     double* output, size_t n, int period) {
    if (n < 2) return;

    // Calculate True Range first
    std::vector<double> tr(n);
    tr[0] = high[0] - low[0];

    // Vectorized TR calculation
    size_t i = 1;
    for (; i + 4 <= n; i += 4) {
        __m256d h = _mm256_loadu_pd(high + i);
        __m256d l = _mm256_loadu_pd(low + i);
        __m256d c_prev = _mm256_loadu_pd(close + i - 1);

        // HL = high - low
        __m256d hl = _mm256_sub_pd(h, l);

        // HC = |high - close_prev|
        __m256d hc = _mm256_sub_pd(h, c_prev);
        hc = _mm256_andnot_pd(_mm256_set1_pd(-0.0), hc);  // abs

        // LC = |low - close_prev|
        __m256d lc = _mm256_sub_pd(l, c_prev);
        lc = _mm256_andnot_pd(_mm256_set1_pd(-0.0), lc);  // abs

        // TR = max(HL, HC, LC)
        __m256d tr_vec = _mm256_max_pd(hl, _mm256_max_pd(hc, lc));

        _mm256_storeu_pd(tr.data() + i, tr_vec);
    }

    // Handle remainder
    for (; i < n; ++i) {
        double hl = high[i] - low[i];
        double hc = std::abs(high[i] - close[i-1]);
        double lc = std::abs(low[i] - close[i-1]);
        tr[i] = std::max({hl, hc, lc});
    }

    // Calculate ATR using EMA of TR
    ema_vectorized(tr.data(), output, n, period);
}

inline void atr_vectorized(const double* high, const double* low, const double* close,
                           double* output, size_t n, int period) {
    if (g_cpu_features.avx2) {
        atr_avx2(high, low, close, output, n, period);
    } else {
        // Scalar fallback
        std::vector<double> tr(n);
        tr[0] = high[0] - low[0];
        for (size_t i = 1; i < n; ++i) {
            double hl = high[i] - low[i];
            double hc = std::abs(high[i] - close[i-1]);
            double lc = std::abs(low[i] - close[i-1]);
            tr[i] = std::max({hl, hc, lc});
        }
        ema_vectorized(tr.data(), output, n, period);
    }
}

//=============================================================================
// Standard Deviation (for Bollinger Bands)
//=============================================================================

inline double stddev_avx2(const double* data, size_t n, double mean) {
    __m256d mean_vec = _mm256_set1_pd(mean);
    __m256d sum_sq = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d v = _mm256_loadu_pd(data + i);
        __m256d diff = _mm256_sub_pd(v, mean_vec);
        sum_sq = _mm256_fmadd_pd(diff, diff, sum_sq);  // FMA: diff*diff + sum_sq
    }

    // Horizontal sum
    __m128d lo = _mm256_castpd256_pd128(sum_sq);
    __m128d hi = _mm256_extractf128_pd(sum_sq, 1);
    lo = _mm_add_pd(lo, hi);
    __m128d shuf = _mm_shuffle_pd(lo, lo, 1);
    lo = _mm_add_pd(lo, shuf);

    double result;
    _mm_store_sd(&result, lo);

    // Handle remainder
    for (; i < n; ++i) {
        double diff = data[i] - mean;
        result += diff * diff;
    }

    return std::sqrt(result / n);
}

inline double stddev(const double* data, size_t n, double mean) {
    if (g_cpu_features.avx2 && g_cpu_features.fma) {
        return stddev_avx2(data, n, mean);
    }

    // Scalar fallback
    double sum_sq = 0;
    for (size_t i = 0; i < n; ++i) {
        double diff = data[i] - mean;
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq / n);
}

//=============================================================================
// Bollinger Bands
//=============================================================================

inline void bollinger_bands_vectorized(const double* input,
                                        double* middle, double* upper, double* lower,
                                        size_t n, int period, double num_std) {
    // Calculate middle band (SMA)
    sma_vectorized(input, middle, n, period);

    // Calculate upper and lower bands
    for (size_t i = period - 1; i < n; ++i) {
        double std = stddev(input + i - period + 1, period, middle[i]);
        upper[i] = middle[i] + num_std * std;
        lower[i] = middle[i] - num_std * std;
    }
}

//=============================================================================
// MACD
//=============================================================================

inline void macd_vectorized(const double* input,
                            double* macd_line, double* signal_line, double* histogram,
                            size_t n, int fast_period, int slow_period, int signal_period) {
    std::vector<double> fast_ema(n), slow_ema(n);

    ema_vectorized(input, fast_ema.data(), n, fast_period);
    ema_vectorized(input, slow_ema.data(), n, slow_period);

    // MACD = Fast EMA - Slow EMA (vectorized subtraction)
    size_t i = 0;
    if (g_cpu_features.avx2) {
        for (; i + 4 <= n; i += 4) {
            __m256d fast = _mm256_loadu_pd(fast_ema.data() + i);
            __m256d slow = _mm256_loadu_pd(slow_ema.data() + i);
            __m256d macd = _mm256_sub_pd(fast, slow);
            _mm256_storeu_pd(macd_line + i, macd);
        }
    }
    for (; i < n; ++i) {
        macd_line[i] = fast_ema[i] - slow_ema[i];
    }

    // Signal line = EMA of MACD
    ema_vectorized(macd_line, signal_line, n, signal_period);

    // Histogram = MACD - Signal (vectorized)
    i = 0;
    if (g_cpu_features.avx2) {
        for (; i + 4 <= n; i += 4) {
            __m256d macd = _mm256_loadu_pd(macd_line + i);
            __m256d signal = _mm256_loadu_pd(signal_line + i);
            __m256d hist = _mm256_sub_pd(macd, signal);
            _mm256_storeu_pd(histogram + i, hist);
        }
    }
    for (; i < n; ++i) {
        histogram[i] = macd_line[i] - signal_line[i];
    }
}

//=============================================================================
// Profit/Loss Calculation (batch)
//=============================================================================

// Calculate P/L for multiple positions at once
inline void calculate_pnl_batch_avx2(
    const double* entry_prices,   // Entry prices
    const double* lot_sizes,      // Lot sizes
    double current_price,         // Current market price
    double contract_size,         // Contract size
    double* pnl_output,           // Output P/L array
    size_t n,                     // Number of positions
    bool is_buy)                  // True = long, False = short
{
    __m256d price_vec = _mm256_set1_pd(current_price);
    __m256d contract_vec = _mm256_set1_pd(contract_size);

    // Prefetch first cache lines
    _mm_prefetch(reinterpret_cast<const char*>(entry_prices), _MM_HINT_T0);
    _mm_prefetch(reinterpret_cast<const char*>(lot_sizes), _MM_HINT_T0);

    size_t i = 0;

    // 2x unrolled loop - process 8 positions per iteration
    for (; i + 8 <= n; i += 8) {
        // Prefetch ahead
        _mm_prefetch(reinterpret_cast<const char*>(entry_prices + i + 16), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(lot_sizes + i + 16), _MM_HINT_T0);

        __m256d entry0 = _mm256_loadu_pd(entry_prices + i);
        __m256d entry1 = _mm256_loadu_pd(entry_prices + i + 4);
        __m256d lots0 = _mm256_loadu_pd(lot_sizes + i);
        __m256d lots1 = _mm256_loadu_pd(lot_sizes + i + 4);

        __m256d price_diff0, price_diff1;
        if (is_buy) {
            price_diff0 = _mm256_sub_pd(price_vec, entry0);
            price_diff1 = _mm256_sub_pd(price_vec, entry1);
        } else {
            price_diff0 = _mm256_sub_pd(entry0, price_vec);
            price_diff1 = _mm256_sub_pd(entry1, price_vec);
        }

        // PnL = price_diff * lots * contract_size (using FMA for second multiply)
        __m256d pnl0 = _mm256_mul_pd(price_diff0, lots0);
        __m256d pnl1 = _mm256_mul_pd(price_diff1, lots1);
        pnl0 = _mm256_mul_pd(pnl0, contract_vec);
        pnl1 = _mm256_mul_pd(pnl1, contract_vec);

        _mm256_storeu_pd(pnl_output + i, pnl0);
        _mm256_storeu_pd(pnl_output + i + 4, pnl1);
    }

    // Handle remaining 4-element chunk
    if (i + 4 <= n) {
        __m256d entry = _mm256_loadu_pd(entry_prices + i);
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);

        __m256d price_diff;
        if (is_buy) {
            price_diff = _mm256_sub_pd(price_vec, entry);
        } else {
            price_diff = _mm256_sub_pd(entry, price_vec);
        }

        __m256d pnl = _mm256_mul_pd(price_diff, lots);
        pnl = _mm256_mul_pd(pnl, contract_vec);
        _mm256_storeu_pd(pnl_output + i, pnl);
        i += 4;
    }

    // Handle remainder
    for (; i < n; ++i) {
        double diff = is_buy ? (current_price - entry_prices[i])
                            : (entry_prices[i] - current_price);
        pnl_output[i] = diff * lot_sizes[i] * contract_size;
    }
}

// Sum total P/L from batch
inline double total_pnl_batch(const double* entry_prices, const double* lot_sizes,
                              double current_price, double contract_size, size_t n, bool is_buy) {
    std::vector<double> pnl(n);
    calculate_pnl_batch_avx2(entry_prices, lot_sizes, current_price, contract_size,
                             pnl.data(), n, is_buy);
    return sum(pnl.data(), n);
}

//=============================================================================
// Margin Calculation (batch)
//=============================================================================

inline double total_margin_batch_avx2(const double* lot_sizes, size_t n,
                                       double contract_size, double price, double leverage) {
    __m256d contract_vec = _mm256_set1_pd(contract_size);
    __m256d price_vec = _mm256_set1_pd(price);
    __m256d leverage_inv = _mm256_set1_pd(1.0 / leverage);
    __m256d sum = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);

        // margin = lots * contract_size * price / leverage
        __m256d margin = _mm256_mul_pd(lots, contract_vec);
        margin = _mm256_mul_pd(margin, price_vec);
        margin = _mm256_mul_pd(margin, leverage_inv);

        sum = _mm256_add_pd(sum, margin);
    }

    // Horizontal sum
    __m128d lo = _mm256_castpd256_pd128(sum);
    __m128d hi = _mm256_extractf128_pd(sum, 1);
    lo = _mm_add_pd(lo, hi);
    __m128d shuf = _mm_shuffle_pd(lo, lo, 1);
    lo = _mm_add_pd(lo, shuf);

    double result;
    _mm_store_sd(&result, lo);

    // Handle remainder
    for (; i < n; ++i) {
        result += lot_sizes[i] * contract_size * price / leverage;
    }

    return result;
}

//=============================================================================
// Vectorized Min/Max (for drawdown calculations)
//=============================================================================

// Auto-select best max function
inline double max_value(const double* data, size_t n);

// Auto-select best min function
inline double min_value(const double* data, size_t n);

// Auto-select best P/L batch function
inline void calculate_pnl_batch(
    const double* entry_prices, const double* lot_sizes,
    double current_price, double contract_size,
    double* pnl_output, size_t n, bool is_buy);

// Find maximum value in array
inline double max_avx2(const double* data, size_t n) {
    if (n == 0) return 0.0;

    __m256d max_vec = _mm256_set1_pd(-1e308);  // Start with very small number
    size_t i = 0;

    // 2x unrolled
    for (; i + 8 <= n; i += 8) {
        __m256d v0 = _mm256_loadu_pd(data + i);
        __m256d v1 = _mm256_loadu_pd(data + i + 4);
        max_vec = _mm256_max_pd(max_vec, v0);
        max_vec = _mm256_max_pd(max_vec, v1);
    }

    for (; i + 4 <= n; i += 4) {
        __m256d v = _mm256_loadu_pd(data + i);
        max_vec = _mm256_max_pd(max_vec, v);
    }

    // Horizontal max
    __m128d lo = _mm256_castpd256_pd128(max_vec);
    __m128d hi = _mm256_extractf128_pd(max_vec, 1);
    lo = _mm_max_pd(lo, hi);
    __m128d shuf = _mm_shuffle_pd(lo, lo, 1);
    lo = _mm_max_pd(lo, shuf);

    double result;
    _mm_store_sd(&result, lo);

    // Handle remainder
    for (; i < n; ++i) {
        if (data[i] > result) result = data[i];
    }

    return result;
}

// Find minimum value in array
inline double min_avx2(const double* data, size_t n) {
    if (n == 0) return 0.0;

    __m256d min_vec = _mm256_set1_pd(1e308);  // Start with very large number
    size_t i = 0;

    // 2x unrolled
    for (; i + 8 <= n; i += 8) {
        __m256d v0 = _mm256_loadu_pd(data + i);
        __m256d v1 = _mm256_loadu_pd(data + i + 4);
        min_vec = _mm256_min_pd(min_vec, v0);
        min_vec = _mm256_min_pd(min_vec, v1);
    }

    for (; i + 4 <= n; i += 4) {
        __m256d v = _mm256_loadu_pd(data + i);
        min_vec = _mm256_min_pd(min_vec, v);
    }

    // Horizontal min
    __m128d lo = _mm256_castpd256_pd128(min_vec);
    __m128d hi = _mm256_extractf128_pd(min_vec, 1);
    lo = _mm_min_pd(lo, hi);
    __m128d shuf = _mm_shuffle_pd(lo, lo, 1);
    lo = _mm_min_pd(lo, shuf);

    double result;
    _mm_store_sd(&result, lo);

    // Handle remainder
    for (; i < n; ++i) {
        if (data[i] < result) result = data[i];
    }

    return result;
}

// Auto-select implementations
inline double max_value(const double* data, size_t n) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f) return max_avx512(data, n);
#endif
    return max_avx2(data, n);
}

inline double min_value(const double* data, size_t n) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f) return min_avx512(data, n);
#endif
    return min_avx2(data, n);
}

inline void calculate_pnl_batch(
    const double* entry_prices, const double* lot_sizes,
    double current_price, double contract_size,
    double* pnl_output, size_t n, bool is_buy) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f) {
        calculate_pnl_batch_avx512(entry_prices, lot_sizes, current_price,
                                   contract_size, pnl_output, n, is_buy);
        return;
    }
#endif
    calculate_pnl_batch_avx2(entry_prices, lot_sizes, current_price,
                             contract_size, pnl_output, n, is_buy);
}

// Running maximum (peak for drawdown calculation)
inline void running_max_avx2(const double* input, double* output, size_t n) {
    if (n == 0) return;

    double current_max = input[0];
    output[0] = current_max;

    // This is inherently sequential, but we can optimize memory access
    size_t i = 1;
    for (; i + 4 <= n; i += 4) {
        // Prefetch ahead
        _mm_prefetch(reinterpret_cast<const char*>(input + i + 16), _MM_HINT_T0);

        for (size_t j = 0; j < 4; ++j) {
            if (input[i + j] > current_max) current_max = input[i + j];
            output[i + j] = current_max;
        }
    }

    for (; i < n; ++i) {
        if (input[i] > current_max) current_max = input[i];
        output[i] = current_max;
    }
}

// Calculate drawdown series: (peak - value) / peak
inline void drawdown_avx2(const double* equity, double* drawdown, size_t n) {
    if (n == 0) return;

    std::vector<double> peaks(n);
    running_max_avx2(equity, peaks.data(), n);

    __m256d one = _mm256_set1_pd(1.0);

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d eq = _mm256_loadu_pd(equity + i);
        __m256d pk = _mm256_loadu_pd(peaks.data() + i);

        // drawdown = 1 - (equity / peak)
        __m256d ratio = _mm256_div_pd(eq, pk);
        __m256d dd = _mm256_sub_pd(one, ratio);

        _mm256_storeu_pd(drawdown + i, dd);
    }

    for (; i < n; ++i) {
        drawdown[i] = 1.0 - (equity[i] / peaks[i]);
    }
}

// Find maximum drawdown value
inline double max_drawdown(const double* equity, size_t n) {
    if (n == 0) return 0.0;

    std::vector<double> dd(n);
    drawdown_avx2(equity, dd.data(), n);
    return max_avx2(dd.data(), n);
}

//=============================================================================
// Batch SL/TP checking (returns bitmask of positions to close)
//=============================================================================

// AVX2: Check 4 BUY positions at once for SL/TP hits
// Returns number of hits found; hit_indices filled with position indices
inline int check_sl_tp_buy_avx2(
    const double* stop_losses,     // SL prices (0 = no SL)
    const double* take_profits,    // TP prices (0 = no TP)
    double bid_price,              // Current bid for BUY exit
    size_t n,                      // Number of positions
    int* hit_indices,              // Output: indices of positions to close
    int* hit_types)                // Output: 1=SL, 2=TP
{
    __m256d bid_vec = _mm256_set1_pd(bid_price);
    __m256d zero = _mm256_setzero_pd();
    int hit_count = 0;

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d sl = _mm256_loadu_pd(stop_losses + i);
        __m256d tp = _mm256_loadu_pd(take_profits + i);

        // SL hit: sl > 0 AND bid <= sl
        __m256d sl_active = _mm256_cmp_pd(sl, zero, _CMP_GT_OQ);
        __m256d sl_hit = _mm256_cmp_pd(bid_vec, sl, _CMP_LE_OQ);
        sl_hit = _mm256_and_pd(sl_active, sl_hit);

        // TP hit: tp > 0 AND bid >= tp
        __m256d tp_active = _mm256_cmp_pd(tp, zero, _CMP_GT_OQ);
        __m256d tp_hit = _mm256_cmp_pd(bid_vec, tp, _CMP_GE_OQ);
        tp_hit = _mm256_and_pd(tp_active, tp_hit);

        // Extract masks
        int sl_mask = _mm256_movemask_pd(sl_hit);
        int tp_mask = _mm256_movemask_pd(tp_hit);

        // Process hits (SL takes priority over TP)
        int combined = sl_mask | tp_mask;
        if (combined) {
            for (int j = 0; j < 4; ++j) {
                if (sl_mask & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
                    hit_types[hit_count] = 1; // SL
                    hit_count++;
                } else if (tp_mask & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
                    hit_types[hit_count] = 2; // TP
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

// AVX2: Check 4 SELL positions at once for SL/TP hits
inline int check_sl_tp_sell_avx2(
    const double* stop_losses,
    const double* take_profits,
    double ask_price,
    size_t n,
    int* hit_indices,
    int* hit_types)
{
    __m256d ask_vec = _mm256_set1_pd(ask_price);
    __m256d zero = _mm256_setzero_pd();
    int hit_count = 0;

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d sl = _mm256_loadu_pd(stop_losses + i);
        __m256d tp = _mm256_loadu_pd(take_profits + i);

        // SL hit: sl > 0 AND ask >= sl
        __m256d sl_active = _mm256_cmp_pd(sl, zero, _CMP_GT_OQ);
        __m256d sl_hit = _mm256_cmp_pd(ask_vec, sl, _CMP_GE_OQ);
        sl_hit = _mm256_and_pd(sl_active, sl_hit);

        // TP hit: tp > 0 AND ask <= tp
        __m256d tp_active = _mm256_cmp_pd(tp, zero, _CMP_GT_OQ);
        __m256d tp_hit = _mm256_cmp_pd(ask_vec, tp, _CMP_LE_OQ);
        tp_hit = _mm256_and_pd(tp_active, tp_hit);

        int sl_mask = _mm256_movemask_pd(sl_hit);
        int tp_mask = _mm256_movemask_pd(tp_hit);

        int combined = sl_mask | tp_mask;
        if (combined) {
            for (int j = 0; j < 4; ++j) {
                if (sl_mask & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
                    hit_types[hit_count] = 1;
                    hit_count++;
                } else if (tp_mask & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
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

#ifdef __AVX512F__
// AVX-512: Check 8 BUY positions at once
inline int check_sl_tp_buy_avx512(
    const double* stop_losses, const double* take_profits,
    double bid_price, size_t n, int* hit_indices, int* hit_types)
{
    __m512d bid_vec = _mm512_set1_pd(bid_price);
    __m512d zero = _mm512_setzero_pd();
    int hit_count = 0;

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m512d sl = _mm512_loadu_pd(stop_losses + i);
        __m512d tp = _mm512_loadu_pd(take_profits + i);

        // SL: sl > 0 AND bid <= sl
        __mmask8 sl_active = _mm512_cmp_pd_mask(sl, zero, _CMP_GT_OQ);
        __mmask8 sl_hit = _mm512_mask_cmp_pd_mask(sl_active, bid_vec, sl, _CMP_LE_OQ);

        // TP: tp > 0 AND bid >= tp
        __mmask8 tp_active = _mm512_cmp_pd_mask(tp, zero, _CMP_GT_OQ);
        __mmask8 tp_hit = _mm512_mask_cmp_pd_mask(tp_active, bid_vec, tp, _CMP_GE_OQ);

        __mmask8 combined = sl_hit | tp_hit;
        if (combined) {
            for (int j = 0; j < 8; ++j) {
                if (sl_hit & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
                    hit_types[hit_count] = 1;
                    hit_count++;
                } else if (tp_hit & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
                    hit_types[hit_count] = 2;
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

// AVX-512: Check 8 SELL positions at once
inline int check_sl_tp_sell_avx512(
    const double* stop_losses, const double* take_profits,
    double ask_price, size_t n, int* hit_indices, int* hit_types)
{
    __m512d ask_vec = _mm512_set1_pd(ask_price);
    __m512d zero = _mm512_setzero_pd();
    int hit_count = 0;

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m512d sl = _mm512_loadu_pd(stop_losses + i);
        __m512d tp = _mm512_loadu_pd(take_profits + i);

        __mmask8 sl_active = _mm512_cmp_pd_mask(sl, zero, _CMP_GT_OQ);
        __mmask8 sl_hit = _mm512_mask_cmp_pd_mask(sl_active, ask_vec, sl, _CMP_GE_OQ);

        __mmask8 tp_active = _mm512_cmp_pd_mask(tp, zero, _CMP_GT_OQ);
        __mmask8 tp_hit = _mm512_mask_cmp_pd_mask(tp_active, ask_vec, tp, _CMP_LE_OQ);

        __mmask8 combined = sl_hit | tp_hit;
        if (combined) {
            for (int j = 0; j < 8; ++j) {
                if (sl_hit & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
                    hit_types[hit_count] = 1;
                    hit_count++;
                } else if (tp_hit & (1 << j)) {
                    hit_indices[hit_count] = static_cast<int>(i + j);
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

// AVX-512 margin batch (with per-position prices)
inline double total_margin_batch_avx512(
    const double* lot_sizes, const double* prices, size_t n,
    double contract_size, double leverage) {

    __m512d contract_vec = _mm512_set1_pd(contract_size);
    __m512d leverage_inv = _mm512_set1_pd(1.0 / leverage);
    __m512d sum0 = _mm512_setzero_pd();
    __m512d sum1 = _mm512_setzero_pd();

    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        _mm_prefetch(reinterpret_cast<const char*>(lot_sizes + i + 32), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(prices + i + 32), _MM_HINT_T0);

        __m512d lots0 = _mm512_loadu_pd(lot_sizes + i);
        __m512d lots1 = _mm512_loadu_pd(lot_sizes + i + 8);
        __m512d prc0 = _mm512_loadu_pd(prices + i);
        __m512d prc1 = _mm512_loadu_pd(prices + i + 8);

        __m512d m0 = _mm512_mul_pd(_mm512_mul_pd(lots0, prc0), contract_vec);
        __m512d m1 = _mm512_mul_pd(_mm512_mul_pd(lots1, prc1), contract_vec);
        sum0 = _mm512_add_pd(sum0, _mm512_mul_pd(m0, leverage_inv));
        sum1 = _mm512_add_pd(sum1, _mm512_mul_pd(m1, leverage_inv));
    }

    for (; i + 8 <= n; i += 8) {
        __m512d lots = _mm512_loadu_pd(lot_sizes + i);
        __m512d prc = _mm512_loadu_pd(prices + i);
        __m512d m = _mm512_mul_pd(_mm512_mul_pd(lots, prc), contract_vec);
        sum0 = _mm512_add_pd(sum0, _mm512_mul_pd(m, leverage_inv));
    }

    sum0 = _mm512_add_pd(sum0, sum1);
    double result = _mm512_reduce_add_pd(sum0);

    for (; i < n; ++i) {
        result += lot_sizes[i] * prices[i] * contract_size / leverage;
    }
    return result;
}
#endif

// Auto-dispatch: SL/TP batch check for BUY positions
inline int check_sl_tp_buy(
    const double* stop_losses, const double* take_profits,
    double bid_price, size_t n, int* hit_indices, int* hit_types) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f)
        return check_sl_tp_buy_avx512(stop_losses, take_profits, bid_price, n, hit_indices, hit_types);
#endif
    if (g_cpu_features.avx2)
        return check_sl_tp_buy_avx2(stop_losses, take_profits, bid_price, n, hit_indices, hit_types);
    // Scalar fallback
    int hit_count = 0;
    for (size_t i = 0; i < n; ++i) {
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

// Auto-dispatch: SL/TP batch check for SELL positions
inline int check_sl_tp_sell(
    const double* stop_losses, const double* take_profits,
    double ask_price, size_t n, int* hit_indices, int* hit_types) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f)
        return check_sl_tp_sell_avx512(stop_losses, take_profits, ask_price, n, hit_indices, hit_types);
#endif
    if (g_cpu_features.avx2)
        return check_sl_tp_sell_avx2(stop_losses, take_profits, ask_price, n, hit_indices, hit_types);
    // Scalar fallback
    int hit_count = 0;
    for (size_t i = 0; i < n; ++i) {
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

//=============================================================================
// Aligned Memory Allocation
//=============================================================================

// Allocate aligned memory (32-byte for AVX2, 64-byte for AVX-512)
inline void* aligned_alloc_simd(size_t size, size_t alignment = 32) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
#endif
}

inline void aligned_free_simd(void* ptr) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// RAII wrapper for aligned memory
template<typename T>
class AlignedArray {
public:
    explicit AlignedArray(size_t count, size_t alignment = 32)
        : size_(count)
        , data_(static_cast<T*>(aligned_alloc_simd(count * sizeof(T), alignment))) {}

    ~AlignedArray() {
        if (data_) aligned_free_simd(data_);
    }

    // Non-copyable
    AlignedArray(const AlignedArray&) = delete;
    AlignedArray& operator=(const AlignedArray&) = delete;

    // Movable
    AlignedArray(AlignedArray&& other) noexcept : size_(other.size_), data_(other.data_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    AlignedArray& operator=(AlignedArray&& other) noexcept {
        if (this != &other) {
            if (data_) aligned_free_simd(data_);
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    T* data() { return data_; }
    const T* data() const { return data_; }
    size_t size() const { return size_; }

    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

private:
    size_t size_;
    T* data_;
};

//=============================================================================
// Batch Operations for Position Management
//=============================================================================

// Calculate margin for multiple positions at once (2x unrolled)
inline double total_margin_batch_avx2_optimized(
    const double* lot_sizes, const double* prices, size_t n,
    double contract_size, double leverage) {

    __m256d contract_vec = _mm256_set1_pd(contract_size);
    __m256d leverage_inv = _mm256_set1_pd(1.0 / leverage);
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();

    _mm_prefetch(reinterpret_cast<const char*>(lot_sizes), _MM_HINT_T0);
    _mm_prefetch(reinterpret_cast<const char*>(prices), _MM_HINT_T0);

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        _mm_prefetch(reinterpret_cast<const char*>(lot_sizes + i + 16), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(prices + i + 16), _MM_HINT_T0);

        __m256d lots0 = _mm256_loadu_pd(lot_sizes + i);
        __m256d lots1 = _mm256_loadu_pd(lot_sizes + i + 4);
        __m256d prc0 = _mm256_loadu_pd(prices + i);
        __m256d prc1 = _mm256_loadu_pd(prices + i + 4);

        // margin = lots * price * contract_size / leverage
        __m256d margin0 = _mm256_mul_pd(lots0, prc0);
        __m256d margin1 = _mm256_mul_pd(lots1, prc1);
        margin0 = _mm256_mul_pd(margin0, contract_vec);
        margin1 = _mm256_mul_pd(margin1, contract_vec);
        margin0 = _mm256_mul_pd(margin0, leverage_inv);
        margin1 = _mm256_mul_pd(margin1, leverage_inv);

        sum0 = _mm256_add_pd(sum0, margin0);
        sum1 = _mm256_add_pd(sum1, margin1);
    }

    // Combine and horizontal sum
    sum0 = _mm256_add_pd(sum0, sum1);

    __m128d lo = _mm256_castpd256_pd128(sum0);
    __m128d hi = _mm256_extractf128_pd(sum0, 1);
    lo = _mm_add_pd(lo, hi);
    __m128d shuf = _mm_shuffle_pd(lo, lo, 1);
    lo = _mm_add_pd(lo, shuf);

    double result;
    _mm_store_sd(&result, lo);

    // Handle remainder
    for (; i < n; ++i) {
        result += lot_sizes[i] * prices[i] * contract_size / leverage;
    }

    return result;
}

// Auto-dispatch: margin batch with per-position prices (AVX-512 preferred, AVX2 fallback)
inline double total_margin_batch(
    const double* lot_sizes, const double* prices, size_t n,
    double contract_size, double leverage) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f)
        return total_margin_batch_avx512(lot_sizes, prices, n, contract_size, leverage);
#endif
    return total_margin_batch_avx2_optimized(lot_sizes, prices, n, contract_size, leverage);
}

//=============================================================================
// Margin Calculation — ALL TradeCalcMode variants
// AVX-512 (8-wide double), AVX2 fallback (4-wide double), scalar remainder
//=============================================================================

// ---------------------------------------------------------------------------
// CFD_LEVERAGE (mode 4): margin = lots * cs * price / leverage * margin_rate
// ---------------------------------------------------------------------------
inline double total_margin_cfd_leverage_avx512(
    const double* lot_sizes, const double* prices, size_t n,
    double contract_size, double leverage, double margin_rate) {

    const double factor = contract_size / leverage * margin_rate;
    __m512d fac_vec = _mm512_set1_pd(factor);
    __m512d sum0 = _mm512_setzero_pd();
    __m512d sum1 = _mm512_setzero_pd();

    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m512d lots0 = _mm512_loadu_pd(lot_sizes + i);
        __m512d lots1 = _mm512_loadu_pd(lot_sizes + i + 8);
        __m512d prc0 = _mm512_loadu_pd(prices + i);
        __m512d prc1 = _mm512_loadu_pd(prices + i + 8);
        // margin = lots * price * factor
        sum0 = _mm512_fmadd_pd(_mm512_mul_pd(lots0, prc0), fac_vec, sum0);
        sum1 = _mm512_fmadd_pd(_mm512_mul_pd(lots1, prc1), fac_vec, sum1);
    }
    for (; i + 8 <= n; i += 8) {
        __m512d lots = _mm512_loadu_pd(lot_sizes + i);
        __m512d prc = _mm512_loadu_pd(prices + i);
        sum0 = _mm512_fmadd_pd(_mm512_mul_pd(lots, prc), fac_vec, sum0);
    }
    sum0 = _mm512_add_pd(sum0, sum1);
    double result = _mm512_reduce_add_pd(sum0);
    for (; i < n; ++i)
        result += lot_sizes[i] * prices[i] * factor;
    return result;
}

inline double total_margin_cfd_leverage_avx2(
    const double* lot_sizes, const double* prices, size_t n,
    double contract_size, double leverage, double margin_rate) {

    const double factor = contract_size / leverage * margin_rate;
    __m256d fac_vec = _mm256_set1_pd(factor);
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256d lots0 = _mm256_loadu_pd(lot_sizes + i);
        __m256d lots1 = _mm256_loadu_pd(lot_sizes + i + 4);
        __m256d prc0 = _mm256_loadu_pd(prices + i);
        __m256d prc1 = _mm256_loadu_pd(prices + i + 4);
        sum0 = _mm256_fmadd_pd(_mm256_mul_pd(lots0, prc0), fac_vec, sum0);
        sum1 = _mm256_fmadd_pd(_mm256_mul_pd(lots1, prc1), fac_vec, sum1);
    }
    for (; i + 4 <= n; i += 4) {
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);
        __m256d prc = _mm256_loadu_pd(prices + i);
        sum0 = _mm256_fmadd_pd(_mm256_mul_pd(lots, prc), fac_vec, sum0);
    }
    sum0 = _mm256_add_pd(sum0, sum1);
    __m128d lo = _mm256_castpd256_pd128(sum0);
    __m128d hi = _mm256_extractf128_pd(sum0, 1);
    lo = _mm_add_pd(lo, hi);
    lo = _mm_add_pd(lo, _mm_shuffle_pd(lo, lo, 1));
    double result;
    _mm_store_sd(&result, lo);
    for (; i < n; ++i)
        result += lot_sizes[i] * prices[i] * factor;
    return result;
}

// ---------------------------------------------------------------------------
// CFD / CFD_INDEX (mode 2/3): margin = lots * cs * price * margin_rate  (NO leverage)
// ---------------------------------------------------------------------------
inline double total_margin_cfd_avx512(
    const double* lot_sizes, const double* prices, size_t n,
    double contract_size, double margin_rate) {

    const double factor = contract_size * margin_rate;
    __m512d fac_vec = _mm512_set1_pd(factor);
    __m512d sum0 = _mm512_setzero_pd();
    __m512d sum1 = _mm512_setzero_pd();

    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m512d lots0 = _mm512_loadu_pd(lot_sizes + i);
        __m512d lots1 = _mm512_loadu_pd(lot_sizes + i + 8);
        __m512d prc0 = _mm512_loadu_pd(prices + i);
        __m512d prc1 = _mm512_loadu_pd(prices + i + 8);
        sum0 = _mm512_fmadd_pd(_mm512_mul_pd(lots0, prc0), fac_vec, sum0);
        sum1 = _mm512_fmadd_pd(_mm512_mul_pd(lots1, prc1), fac_vec, sum1);
    }
    for (; i + 8 <= n; i += 8) {
        __m512d lots = _mm512_loadu_pd(lot_sizes + i);
        __m512d prc = _mm512_loadu_pd(prices + i);
        sum0 = _mm512_fmadd_pd(_mm512_mul_pd(lots, prc), fac_vec, sum0);
    }
    sum0 = _mm512_add_pd(sum0, sum1);
    double result = _mm512_reduce_add_pd(sum0);
    for (; i < n; ++i)
        result += lot_sizes[i] * prices[i] * factor;
    return result;
}

inline double total_margin_cfd_avx2(
    const double* lot_sizes, const double* prices, size_t n,
    double contract_size, double margin_rate) {

    const double factor = contract_size * margin_rate;
    __m256d fac_vec = _mm256_set1_pd(factor);
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256d lots0 = _mm256_loadu_pd(lot_sizes + i);
        __m256d lots1 = _mm256_loadu_pd(lot_sizes + i + 4);
        __m256d prc0 = _mm256_loadu_pd(prices + i);
        __m256d prc1 = _mm256_loadu_pd(prices + i + 4);
        sum0 = _mm256_fmadd_pd(_mm256_mul_pd(lots0, prc0), fac_vec, sum0);
        sum1 = _mm256_fmadd_pd(_mm256_mul_pd(lots1, prc1), fac_vec, sum1);
    }
    for (; i + 4 <= n; i += 4) {
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);
        __m256d prc = _mm256_loadu_pd(prices + i);
        sum0 = _mm256_fmadd_pd(_mm256_mul_pd(lots, prc), fac_vec, sum0);
    }
    sum0 = _mm256_add_pd(sum0, sum1);
    __m128d lo = _mm256_castpd256_pd128(sum0);
    __m128d hi = _mm256_extractf128_pd(sum0, 1);
    lo = _mm_add_pd(lo, hi);
    lo = _mm_add_pd(lo, _mm_shuffle_pd(lo, lo, 1));
    double result;
    _mm_store_sd(&result, lo);
    for (; i < n; ++i)
        result += lot_sizes[i] * prices[i] * factor;
    return result;
}

// ---------------------------------------------------------------------------
// FOREX (mode 0): margin = lots * cs / leverage * margin_rate  (NO price)
// ---------------------------------------------------------------------------
inline double total_margin_forex_avx512(
    const double* lot_sizes, size_t n,
    double contract_size, double leverage, double margin_rate) {

    const double factor = contract_size / leverage * margin_rate;
    __m512d fac_vec = _mm512_set1_pd(factor);
    __m512d sum0 = _mm512_setzero_pd();
    __m512d sum1 = _mm512_setzero_pd();

    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m512d lots0 = _mm512_loadu_pd(lot_sizes + i);
        __m512d lots1 = _mm512_loadu_pd(lot_sizes + i + 8);
        sum0 = _mm512_fmadd_pd(lots0, fac_vec, sum0);
        sum1 = _mm512_fmadd_pd(lots1, fac_vec, sum1);
    }
    for (; i + 8 <= n; i += 8) {
        __m512d lots = _mm512_loadu_pd(lot_sizes + i);
        sum0 = _mm512_fmadd_pd(lots, fac_vec, sum0);
    }
    sum0 = _mm512_add_pd(sum0, sum1);
    double result = _mm512_reduce_add_pd(sum0);
    for (; i < n; ++i)
        result += lot_sizes[i] * factor;
    return result;
}

inline double total_margin_forex_avx2(
    const double* lot_sizes, size_t n,
    double contract_size, double leverage, double margin_rate) {

    const double factor = contract_size / leverage * margin_rate;
    __m256d fac_vec = _mm256_set1_pd(factor);
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256d lots0 = _mm256_loadu_pd(lot_sizes + i);
        __m256d lots1 = _mm256_loadu_pd(lot_sizes + i + 4);
        sum0 = _mm256_fmadd_pd(lots0, fac_vec, sum0);
        sum1 = _mm256_fmadd_pd(lots1, fac_vec, sum1);
    }
    for (; i + 4 <= n; i += 4) {
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);
        sum0 = _mm256_fmadd_pd(lots, fac_vec, sum0);
    }
    sum0 = _mm256_add_pd(sum0, sum1);
    __m128d lo = _mm256_castpd256_pd128(sum0);
    __m128d hi = _mm256_extractf128_pd(sum0, 1);
    lo = _mm_add_pd(lo, hi);
    lo = _mm_add_pd(lo, _mm_shuffle_pd(lo, lo, 1));
    double result;
    _mm_store_sd(&result, lo);
    for (; i < n; ++i)
        result += lot_sizes[i] * factor;
    return result;
}

// ---------------------------------------------------------------------------
// FUTURES (mode 1): margin = lots * margin_initial  (fixed per lot)
// ---------------------------------------------------------------------------
inline double total_margin_futures_avx512(
    const double* lot_sizes, size_t n, double margin_initial) {

    __m512d mi_vec = _mm512_set1_pd(margin_initial);
    __m512d sum0 = _mm512_setzero_pd();
    __m512d sum1 = _mm512_setzero_pd();

    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m512d lots0 = _mm512_loadu_pd(lot_sizes + i);
        __m512d lots1 = _mm512_loadu_pd(lot_sizes + i + 8);
        sum0 = _mm512_fmadd_pd(lots0, mi_vec, sum0);
        sum1 = _mm512_fmadd_pd(lots1, mi_vec, sum1);
    }
    for (; i + 8 <= n; i += 8) {
        __m512d lots = _mm512_loadu_pd(lot_sizes + i);
        sum0 = _mm512_fmadd_pd(lots, mi_vec, sum0);
    }
    sum0 = _mm512_add_pd(sum0, sum1);
    double result = _mm512_reduce_add_pd(sum0);
    for (; i < n; ++i)
        result += lot_sizes[i] * margin_initial;
    return result;
}

inline double total_margin_futures_avx2(
    const double* lot_sizes, size_t n, double margin_initial) {

    __m256d mi_vec = _mm256_set1_pd(margin_initial);
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256d lots0 = _mm256_loadu_pd(lot_sizes + i);
        __m256d lots1 = _mm256_loadu_pd(lot_sizes + i + 4);
        sum0 = _mm256_fmadd_pd(lots0, mi_vec, sum0);
        sum1 = _mm256_fmadd_pd(lots1, mi_vec, sum1);
    }
    for (; i + 4 <= n; i += 4) {
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);
        sum0 = _mm256_fmadd_pd(lots, mi_vec, sum0);
    }
    sum0 = _mm256_add_pd(sum0, sum1);
    __m128d lo = _mm256_castpd256_pd128(sum0);
    __m128d hi = _mm256_extractf128_pd(sum0, 1);
    lo = _mm_add_pd(lo, hi);
    lo = _mm_add_pd(lo, _mm_shuffle_pd(lo, lo, 1));
    double result;
    _mm_store_sd(&result, lo);
    for (; i < n; ++i)
        result += lot_sizes[i] * margin_initial;
    return result;
}

// ---------------------------------------------------------------------------
// FOREX_NO_LEVERAGE (mode 5): margin = lots * cs * margin_rate  (NO price, NO leverage)
// ---------------------------------------------------------------------------
inline double total_margin_forex_nolev_avx512(
    const double* lot_sizes, size_t n,
    double contract_size, double margin_rate) {

    const double factor = contract_size * margin_rate;
    __m512d fac_vec = _mm512_set1_pd(factor);
    __m512d sum0 = _mm512_setzero_pd();
    __m512d sum1 = _mm512_setzero_pd();

    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m512d lots0 = _mm512_loadu_pd(lot_sizes + i);
        __m512d lots1 = _mm512_loadu_pd(lot_sizes + i + 8);
        sum0 = _mm512_fmadd_pd(lots0, fac_vec, sum0);
        sum1 = _mm512_fmadd_pd(lots1, fac_vec, sum1);
    }
    for (; i + 8 <= n; i += 8) {
        __m512d lots = _mm512_loadu_pd(lot_sizes + i);
        sum0 = _mm512_fmadd_pd(lots, fac_vec, sum0);
    }
    sum0 = _mm512_add_pd(sum0, sum1);
    double result = _mm512_reduce_add_pd(sum0);
    for (; i < n; ++i)
        result += lot_sizes[i] * factor;
    return result;
}

inline double total_margin_forex_nolev_avx2(
    const double* lot_sizes, size_t n,
    double contract_size, double margin_rate) {

    const double factor = contract_size * margin_rate;
    __m256d fac_vec = _mm256_set1_pd(factor);
    __m256d sum0 = _mm256_setzero_pd();
    __m256d sum1 = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256d lots0 = _mm256_loadu_pd(lot_sizes + i);
        __m256d lots1 = _mm256_loadu_pd(lot_sizes + i + 4);
        sum0 = _mm256_fmadd_pd(lots0, fac_vec, sum0);
        sum1 = _mm256_fmadd_pd(lots1, fac_vec, sum1);
    }
    for (; i + 4 <= n; i += 4) {
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);
        sum0 = _mm256_fmadd_pd(lots, fac_vec, sum0);
    }
    sum0 = _mm256_add_pd(sum0, sum1);
    __m128d lo = _mm256_castpd256_pd128(sum0);
    __m128d hi = _mm256_extractf128_pd(sum0, 1);
    lo = _mm_add_pd(lo, hi);
    lo = _mm_add_pd(lo, _mm_shuffle_pd(lo, lo, 1));
    double result;
    _mm_store_sd(&result, lo);
    for (; i < n; ++i)
        result += lot_sizes[i] * factor;
    return result;
}

//=============================================================================
// Utility: Print CPU Features
//=============================================================================

inline void print_cpu_features() {
    printf("SIMD CPU Features:\n");
    printf("  SSE2:     %s\n", g_cpu_features.sse2 ? "Yes" : "No");
    printf("  SSE4.1:   %s\n", g_cpu_features.sse41 ? "Yes" : "No");
    printf("  AVX:      %s\n", g_cpu_features.avx ? "Yes" : "No");
    printf("  AVX2:     %s\n", g_cpu_features.avx2 ? "Yes" : "No");
    printf("  AVX-512F: %s\n", g_cpu_features.avx512f ? "Yes" : "No");
    printf("  FMA:      %s\n", g_cpu_features.fma ? "Yes" : "No");
}

} // namespace simd

#endif // SIMD_INTRINSICS_H
