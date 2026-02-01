/**
 * GPU Compute Implementation
 *
 * Note: This is a stub implementation. Full WebGPU integration requires:
 * 1. wgpu-native library (https://github.com/gfx-rs/wgpu-native)
 * 2. Compiled WGSL shaders
 *
 * For now, this provides CPU fallback for all operations.
 */

#include "gpu_compute.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <numeric>
#include <thread>

namespace backtest {
namespace gpu {

// Internal implementation structure
struct GPUCompute::Impl {
    std::vector<std::vector<GPUTick>> tick_buffers;
    uint32_t next_buffer_id = 0;
    size_t used_memory = 0;

    // CPU fallback implementations
    GPUBacktestResult RunBacktestCPU(
        const std::vector<GPUTick>& ticks,
        const GPUStrategyParams& params,
        double initial_balance,
        double contract_size,
        double leverage
    );
};

GPUCompute::GPUCompute()
    : m_initialized(false)
    , m_impl(std::make_unique<Impl>())
{
}

GPUCompute::~GPUCompute() = default;

bool GPUCompute::Initialize(const GPUConfig& config)
{
    m_config = config;

    // TODO: Initialize wgpu-native
    // For now, we'll use CPU fallback
    //
    // Full implementation would:
    // 1. wgpuCreateInstance()
    // 2. wgpuInstanceRequestAdapter()
    // 3. wgpuAdapterRequestDevice()
    // 4. Create command queue and buffers

    m_initialized = true;  // CPU fallback always available
    return true;
}

GPUDeviceInfo GPUCompute::GetDeviceInfo() const
{
    GPUDeviceInfo info;
    info.name = "CPU Fallback";
    info.vendor = "Software";
    info.backend = "CPU";
    info.memory_size = 0;
    info.is_discrete = false;
    info.is_integrated = false;

    // TODO: Return actual GPU info when wgpu is initialized

    return info;
}

std::vector<GPUDeviceInfo> GPUCompute::EnumerateDevices()
{
    std::vector<GPUDeviceInfo> devices;

    // Always add CPU fallback
    GPUDeviceInfo cpu;
    cpu.name = "CPU Fallback";
    cpu.vendor = "Software";
    cpu.backend = "CPU";
    cpu.memory_size = 0;
    cpu.is_discrete = false;
    cpu.is_integrated = false;
    devices.push_back(cpu);

    // TODO: Enumerate actual GPUs via wgpu-native
    // wgpuInstanceEnumerateAdapters()

    return devices;
}

uint32_t GPUCompute::UploadTickData(const std::vector<GPUTick>& ticks)
{
    uint32_t id = m_impl->next_buffer_id++;
    m_impl->tick_buffers.push_back(ticks);
    m_impl->used_memory += ticks.size() * sizeof(GPUTick);
    return id;
}

std::vector<GPUBacktestResult> GPUCompute::RunGridSearch(
    uint32_t tick_buffer_id,
    const std::vector<GPUStrategyParams>& param_combinations,
    double initial_balance,
    double contract_size,
    double leverage,
    std::function<void(int completed, int total)> progress_callback)
{
    if (tick_buffer_id >= m_impl->tick_buffers.size()) {
        return {};
    }

    const auto& ticks = m_impl->tick_buffers[tick_buffer_id];
    std::vector<GPUBacktestResult> results(param_combinations.size());

    // CPU fallback: multi-threaded execution
    int num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    std::atomic<int> completed{0};
    int total = static_cast<int>(param_combinations.size());

    auto worker = [&](int start, int end) {
        for (int i = start; i < end; ++i) {
            results[i] = m_impl->RunBacktestCPU(
                ticks, param_combinations[i],
                initial_balance, contract_size, leverage
            );

            int done = ++completed;
            if (progress_callback && done % 100 == 0) {
                progress_callback(done, total);
            }
        }
    };

    // Distribute work across threads
    int chunk_size = (total + num_threads - 1) / num_threads;
    for (int t = 0; t < num_threads; ++t) {
        int start = t * chunk_size;
        int end = std::min(start + chunk_size, total);
        if (start < end) {
            threads.emplace_back(worker, start, end);
        }
    }

    // Wait for completion
    for (auto& thread : threads) {
        thread.join();
    }

    if (progress_callback) {
        progress_callback(total, total);
    }

    return results;
}

// CPU fallback for single backtest
GPUBacktestResult GPUCompute::Impl::RunBacktestCPU(
    const std::vector<GPUTick>& ticks,
    const GPUStrategyParams& params,
    double initial_balance,
    double contract_size,
    double leverage)
{
    GPUBacktestResult result = {};
    result.final_balance = static_cast<float>(initial_balance);

    // Simplified FillUp strategy simulation
    double balance = initial_balance;
    double equity = initial_balance;
    double max_equity = initial_balance;
    double max_drawdown = 0;
    double gross_profit = 0;
    double gross_loss = 0;
    int total_trades = 0;
    int winning_trades = 0;

    struct Position {
        double entry_price;
        double lots;
        bool is_buy;
        double take_profit;
    };
    std::vector<Position> positions;

    double last_buy_price = 0;
    double last_sell_price = 0;
    double survive_distance = 0;

    for (const auto& tick : ticks) {
        double bid = tick.bid;
        double ask = tick.ask;
        double mid = (bid + ask) / 2.0;

        // Calculate survive distance
        survive_distance = mid * params.survive_pct / 100.0;

        // Check TPs and close positions
        for (auto it = positions.begin(); it != positions.end();) {
            bool closed = false;
            double pnl = 0;

            if (it->is_buy) {
                if (bid >= it->take_profit) {
                    pnl = (bid - it->entry_price) * it->lots * contract_size;
                    closed = true;
                }
            } else {
                if (ask <= it->take_profit) {
                    pnl = (it->entry_price - ask) * it->lots * contract_size;
                    closed = true;
                }
            }

            if (closed) {
                balance += pnl;
                total_trades++;
                if (pnl > 0) {
                    winning_trades++;
                    gross_profit += pnl;
                } else {
                    gross_loss -= pnl;
                }
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Calculate floating P/L
        double floating_pnl = 0;
        for (const auto& pos : positions) {
            if (pos.is_buy) {
                floating_pnl += (bid - pos.entry_price) * pos.lots * contract_size;
            } else {
                floating_pnl += (pos.entry_price - ask) * pos.lots * contract_size;
            }
        }
        equity = balance + floating_pnl;

        // Update drawdown
        if (equity > max_equity) {
            max_equity = equity;
        }
        double dd = max_equity - equity;
        if (dd > max_drawdown) {
            max_drawdown = dd;
        }

        // Margin stop-out check (20%)
        double margin_used = 0;
        for (const auto& pos : positions) {
            margin_used += (pos.lots * contract_size * mid) / leverage;
        }
        if (equity < margin_used * 0.2 && !positions.empty()) {
            // Close all positions at stop-out
            balance = equity;
            positions.clear();
            continue;
        }

        // Entry logic
        double spacing = params.base_spacing;

        // Position limits
        if (positions.size() >= 200) continue;

        // Initial position
        if (positions.empty()) {
            // Open buy and sell
            Position buy_pos = {ask, params.min_volume, true, ask + spacing};
            Position sell_pos = {bid, params.min_volume, false, bid - spacing};
            positions.push_back(buy_pos);
            positions.push_back(sell_pos);
            last_buy_price = ask;
            last_sell_price = bid;
        } else {
            // Grid logic - add positions when price moves away
            if (ask < last_buy_price - spacing &&
                ask > mid - survive_distance) {
                Position buy_pos = {ask, params.min_volume, true, ask + spacing};
                positions.push_back(buy_pos);
                last_buy_price = ask;
            }

            if (bid > last_sell_price + spacing &&
                bid < mid + survive_distance) {
                Position sell_pos = {bid, params.min_volume, false, bid - spacing};
                positions.push_back(sell_pos);
                last_sell_price = bid;
            }
        }
    }

    // Close remaining positions at end
    for (const auto& pos : positions) {
        double pnl;
        if (pos.is_buy) {
            pnl = (ticks.back().bid - pos.entry_price) * pos.lots * contract_size;
        } else {
            pnl = (pos.entry_price - ticks.back().ask) * pos.lots * contract_size;
        }
        balance += pnl;
        total_trades++;
        if (pnl > 0) {
            winning_trades++;
            gross_profit += pnl;
        } else {
            gross_loss -= pnl;
        }
    }

    // Calculate results
    result.final_balance = static_cast<float>(balance);
    result.net_profit = static_cast<float>(balance - initial_balance);
    result.max_drawdown = static_cast<float>(max_drawdown);
    result.max_drawdown_pct = max_equity > 0 ?
        static_cast<float>(max_drawdown / max_equity * 100.0) : 0;
    result.profit_factor = gross_loss > 0 ?
        static_cast<float>(gross_profit / gross_loss) : 0;
    result.total_trades = total_trades;
    result.win_rate = total_trades > 0 ?
        static_cast<float>(winning_trades * 100.0 / total_trades) : 0;

    // Simplified Sharpe ratio (annualized)
    if (ticks.size() > 1) {
        double days = (ticks.back().timestamp - ticks.front().timestamp) /
                      (1000.0 * 60 * 60 * 24);
        if (days > 0) {
            double annual_return = result.net_profit / initial_balance * (365.0 / days);
            result.sharpe_ratio = static_cast<float>(annual_return /
                (result.max_drawdown_pct / 100.0 + 0.01));  // Simplified
        }
    }

    return result;
}

// Vectorized indicator calculations
std::vector<float> GPUCompute::CalculateSMA(
    const std::vector<float>& prices,
    int period)
{
    std::vector<float> result(prices.size(), 0.0f);
    if (prices.empty() || period <= 0) return result;

    double sum = 0;
    for (size_t i = 0; i < prices.size(); ++i) {
        sum += prices[i];
        if (i >= static_cast<size_t>(period)) {
            sum -= prices[i - period];
        }
        if (i >= static_cast<size_t>(period - 1)) {
            result[i] = static_cast<float>(sum / period);
        }
    }
    return result;
}

std::vector<float> GPUCompute::CalculateEMA(
    const std::vector<float>& prices,
    int period)
{
    std::vector<float> result(prices.size(), 0.0f);
    if (prices.empty() || period <= 0) return result;

    double multiplier = 2.0 / (period + 1);
    result[0] = prices[0];

    for (size_t i = 1; i < prices.size(); ++i) {
        result[i] = static_cast<float>(
            (prices[i] - result[i-1]) * multiplier + result[i-1]
        );
    }
    return result;
}

std::vector<float> GPUCompute::CalculateRSI(
    const std::vector<float>& prices,
    int period)
{
    std::vector<float> result(prices.size(), 50.0f);
    if (prices.size() < 2 || period <= 0) return result;

    double avg_gain = 0, avg_loss = 0;

    // Initial average
    for (int i = 1; i <= period && i < static_cast<int>(prices.size()); ++i) {
        double change = prices[i] - prices[i-1];
        if (change > 0) avg_gain += change;
        else avg_loss -= change;
    }
    avg_gain /= period;
    avg_loss /= period;

    // Calculate RSI
    for (size_t i = period; i < prices.size(); ++i) {
        double change = prices[i] - prices[i-1];
        double gain = change > 0 ? change : 0;
        double loss = change < 0 ? -change : 0;

        avg_gain = (avg_gain * (period - 1) + gain) / period;
        avg_loss = (avg_loss * (period - 1) + loss) / period;

        if (avg_loss == 0) {
            result[i] = 100.0f;
        } else {
            double rs = avg_gain / avg_loss;
            result[i] = static_cast<float>(100.0 - (100.0 / (1.0 + rs)));
        }
    }
    return result;
}

std::vector<float> GPUCompute::CalculateATR(
    const std::vector<float>& high,
    const std::vector<float>& low,
    const std::vector<float>& close,
    int period)
{
    size_t n = std::min({high.size(), low.size(), close.size()});
    std::vector<float> result(n, 0.0f);
    if (n < 2 || period <= 0) return result;

    std::vector<float> tr(n);
    tr[0] = high[0] - low[0];

    for (size_t i = 1; i < n; ++i) {
        float hl = high[i] - low[i];
        float hc = std::abs(high[i] - close[i-1]);
        float lc = std::abs(low[i] - close[i-1]);
        tr[i] = std::max({hl, hc, lc});
    }

    // ATR is EMA of TR
    double multiplier = 2.0 / (period + 1);
    result[0] = tr[0];

    for (size_t i = 1; i < n; ++i) {
        result[i] = static_cast<float>(
            (tr[i] - result[i-1]) * multiplier + result[i-1]
        );
    }
    return result;
}

std::tuple<std::vector<float>, std::vector<float>, std::vector<float>>
GPUCompute::CalculateBollingerBands(
    const std::vector<float>& prices,
    int period,
    float num_std_dev)
{
    std::vector<float> mid = CalculateSMA(prices, period);
    std::vector<float> upper(prices.size(), 0.0f);
    std::vector<float> lower(prices.size(), 0.0f);

    for (size_t i = period - 1; i < prices.size(); ++i) {
        // Calculate standard deviation
        double sum_sq = 0;
        for (int j = 0; j < period; ++j) {
            double diff = prices[i - j] - mid[i];
            sum_sq += diff * diff;
        }
        float std_dev = static_cast<float>(std::sqrt(sum_sq / period));

        upper[i] = mid[i] + num_std_dev * std_dev;
        lower[i] = mid[i] - num_std_dev * std_dev;
    }

    return {mid, upper, lower};
}

std::tuple<std::vector<float>, std::vector<float>, std::vector<float>>
GPUCompute::CalculateMACD(
    const std::vector<float>& prices,
    int fast_period,
    int slow_period,
    int signal_period)
{
    auto fast_ema = CalculateEMA(prices, fast_period);
    auto slow_ema = CalculateEMA(prices, slow_period);

    std::vector<float> macd(prices.size());
    for (size_t i = 0; i < prices.size(); ++i) {
        macd[i] = fast_ema[i] - slow_ema[i];
    }

    auto signal = CalculateEMA(macd, signal_period);

    std::vector<float> histogram(prices.size());
    for (size_t i = 0; i < prices.size(); ++i) {
        histogram[i] = macd[i] - signal[i];
    }

    return {macd, signal, histogram};
}

void GPUCompute::ReleaseBuffer(uint32_t buffer_id)
{
    if (buffer_id < m_impl->tick_buffers.size()) {
        m_impl->used_memory -= m_impl->tick_buffers[buffer_id].size() * sizeof(GPUTick);
        m_impl->tick_buffers[buffer_id].clear();
    }
}

void GPUCompute::ReleaseAllBuffers()
{
    m_impl->tick_buffers.clear();
    m_impl->used_memory = 0;
}

size_t GPUCompute::GetUsedMemory() const
{
    return m_impl->used_memory;
}

size_t GPUCompute::GetAvailableMemory() const
{
    // TODO: Query actual GPU memory
    return 4ULL * 1024 * 1024 * 1024;  // 4 GB placeholder
}

void GPUCompute::WaitForCompletion()
{
    // No-op for CPU fallback
    // TODO: wgpuDevicePoll() for GPU
}

// Monte Carlo Implementation
GPUMonteCarlo::GPUMonteCarlo(GPUCompute& compute)
    : m_compute(compute)
{
}

GPUMonteCarlo::Result GPUMonteCarlo::Run(
    const std::vector<float>& trade_profits,
    float initial_balance,
    const Config& config)
{
    Result result = {};
    if (trade_profits.empty()) return result;

    std::mt19937 rng(std::random_device{}());
    std::normal_distribution<float> slippage_dist(0.0f, config.slippage_stddev);
    std::uniform_real_distribution<float> skip_dist(0.0f, 1.0f);

    std::vector<float> final_profits(config.num_simulations);

    // Run simulations (CPU multi-threaded)
    #pragma omp parallel for
    for (int sim = 0; sim < config.num_simulations; ++sim) {
        std::mt19937 local_rng(rng() + sim);

        std::vector<float> trades = trade_profits;

        // Shuffle if enabled
        if (config.shuffle_trades) {
            std::shuffle(trades.begin(), trades.end(), local_rng);
        }

        float balance = initial_balance;

        for (float profit : trades) {
            // Skip trade randomly
            if (skip_dist(local_rng) < config.skip_trade_probability) {
                continue;
            }

            // Apply slippage
            if (config.vary_slippage) {
                profit += slippage_dist(local_rng);
            }

            balance += profit;
        }

        final_profits[sim] = balance - initial_balance;
    }

    // Sort for percentile calculations
    std::sort(final_profits.begin(), final_profits.end());

    // Calculate statistics
    float sum = std::accumulate(final_profits.begin(), final_profits.end(), 0.0f);
    result.mean_profit = sum / config.num_simulations;
    result.median_profit = final_profits[config.num_simulations / 2];
    result.profit_5th_percentile = final_profits[config.num_simulations * 5 / 100];
    result.profit_95th_percentile = final_profits[config.num_simulations * 95 / 100];

    int losses = 0;
    for (float p : final_profits) {
        if (p < 0) losses++;
    }
    result.probability_of_loss = static_cast<float>(losses) / config.num_simulations;

    return result;
}

// Walk-Forward Implementation
GPUWalkForward::GPUWalkForward(GPUCompute& compute)
    : m_compute(compute)
{
}

GPUWalkForward::Result GPUWalkForward::Run(
    uint32_t tick_buffer_id,
    const std::vector<GPUStrategyParams>& param_space,
    double initial_balance,
    double contract_size,
    double leverage,
    int64_t start_timestamp,
    int64_t end_timestamp,
    const Config& config)
{
    Result result;
    result.total_oos_profit = 0;
    result.robustness_score = 0;
    result.degradation_pct = 0;

    // TODO: Implement walk-forward optimization
    // This requires time-windowed backtesting which is more complex

    return result;
}

} // namespace gpu
} // namespace backtest
