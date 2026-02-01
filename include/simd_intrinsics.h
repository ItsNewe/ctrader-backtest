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

// AVX2 version - sum 4 doubles at once
inline double sum_avx2(const double* data, size_t n) {
    __m256d sum = _mm256_setzero_pd();
    size_t i = 0;

    for (; i + 4 <= n; i += 4) {
        __m256d v = _mm256_loadu_pd(data + i);
        sum = _mm256_add_pd(sum, v);
    }

    // Horizontal sum (256-bit to 128-bit)
    __m128d lo = _mm256_castpd256_pd128(sum);
    __m128d hi = _mm256_extractf128_pd(sum, 1);
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
// AVX-512 version - sum 8 doubles at once
inline double sum_avx512(const double* data, size_t n) {
    __m512d sum = _mm512_setzero_pd();
    size_t i = 0;

    for (; i + 8 <= n; i += 8) {
        __m512d v = _mm512_loadu_pd(data + i);
        sum = _mm512_add_pd(sum, v);
    }

    double result = _mm512_reduce_add_pd(sum);

    // Handle remainder
    for (; i < n; ++i) {
        result += data[i];
    }

    return result;
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

inline void rsi_avx2(const double* input, double* output, size_t n, int period) {
    if (n < static_cast<size_t>(period) + 1) return;

    // Initialize with scalar for first period
    double avg_gain = 0, avg_loss = 0;

    for (int i = 1; i <= period; ++i) {
        double change = input[i] - input[i-1];
        if (change > 0) avg_gain += change;
        else avg_loss -= change;
    }
    avg_gain /= period;
    avg_loss /= period;

    // First RSI value
    if (avg_loss == 0) output[period] = 100.0;
    else {
        double rs = avg_gain / avg_loss;
        output[period] = 100.0 - 100.0 / (1.0 + rs);
    }

    // Subsequent values with Wilder smoothing
    double period_minus_1 = period - 1;

    for (size_t i = period + 1; i < n; ++i) {
        double change = input[i] - input[i-1];
        double gain = change > 0 ? change : 0;
        double loss = change < 0 ? -change : 0;

        avg_gain = (avg_gain * period_minus_1 + gain) / period;
        avg_loss = (avg_loss * period_minus_1 + loss) / period;

        if (avg_loss == 0) output[i] = 100.0;
        else {
            double rs = avg_gain / avg_loss;
            output[i] = 100.0 - 100.0 / (1.0 + rs);
        }
    }
}

inline void rsi_vectorized(const double* input, double* output, size_t n, int period) {
    rsi_avx2(input, output, n, period);
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

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d entry = _mm256_loadu_pd(entry_prices + i);
        __m256d lots = _mm256_loadu_pd(lot_sizes + i);

        __m256d price_diff;
        if (is_buy) {
            price_diff = _mm256_sub_pd(price_vec, entry);
        } else {
            price_diff = _mm256_sub_pd(entry, price_vec);
        }

        // PnL = price_diff * lots * contract_size
        __m256d pnl = _mm256_mul_pd(price_diff, lots);
        pnl = _mm256_mul_pd(pnl, contract_vec);

        _mm256_storeu_pd(pnl_output + i, pnl);
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
