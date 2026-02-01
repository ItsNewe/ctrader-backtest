/**
 * GPU Compute Module
 *
 * STATUS: CPU FALLBACK ONLY - WebGPU integration is not implemented
 *
 * This module provides the API design for GPU-accelerated operations but
 * currently runs ALL computations on CPU using multi-threaded execution.
 * The API is designed for future WebGPU (wgpu-native) integration.
 *
 * Current implementation (CPU fallback):
 * - Multi-threaded grid search using std::thread
 * - Vectorized indicator calculations (SMA, EMA, RSI, ATR, BB, MACD)
 * - Monte Carlo simulation with OpenMP (if available)
 * - Walk-forward optimization (partial)
 *
 * For actual GPU acceleration, the following would be needed:
 * 1. wgpu-native library (https://github.com/gfx-rs/wgpu-native)
 * 2. WGSL compute shaders for backtest logic
 * 3. Platform-specific GPU backend (Vulkan/D3D12/Metal)
 *
 * Note: For most backtest scenarios, CPU multi-threading is sufficient.
 * GPU acceleration mainly helps when running 10,000+ parameter combinations.
 *
 * Original design goals:
 * - Cross-platform GPU acceleration using WebGPU (wgpu-native)
 * - Support for AMD, NVIDIA, Intel integrated GPUs
 * - Parallel backtest execution
 * - Grid search optimization
 * - Vectorized indicator calculations
 */

#ifndef GPU_COMPUTE_H
#define GPU_COMPUTE_H

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <cstdint>

namespace backtest {
namespace gpu {

// Forward declarations
struct GPUDevice;
struct GPUBuffer;
struct GPUComputePipeline;

/**
 * GPU Device Info
 */
struct GPUDeviceInfo {
    std::string name;
    std::string vendor;
    std::string backend;  // "Vulkan", "D3D12", "Metal", "OpenGL"
    size_t memory_size;
    bool is_discrete;
    bool is_integrated;
};

/**
 * GPU Compute Configuration
 */
struct GPUConfig {
    bool prefer_discrete_gpu = true;    // Prefer dedicated GPU over integrated
    bool enable_validation = false;     // Enable debug validation (slower)
    size_t max_memory_usage = 0;        // 0 = auto (50% of VRAM)
    int workgroup_size_x = 256;         // Threads per workgroup
    int max_concurrent_dispatches = 4;  // Pipeline parallelism
};

/**
 * Tick data structure for GPU (aligned for efficient memory access)
 */
struct alignas(32) GPUTick {
    int64_t timestamp;      // Unix timestamp in milliseconds
    float bid;              // Bid price
    float ask;              // Ask price
    float volume;           // Tick volume
    int32_t flags;          // Tick flags
    int32_t padding[3];     // Alignment padding
};

/**
 * Strategy parameters for GPU grid search
 */
struct alignas(16) GPUStrategyParams {
    float survive_pct;
    float base_spacing;
    float min_volume;
    float max_volume;
    float lookback_hours;
    float padding[3];
};

/**
 * Backtest result from GPU
 */
struct alignas(32) GPUBacktestResult {
    float final_balance;
    float net_profit;
    float max_drawdown;
    float max_drawdown_pct;
    float profit_factor;
    float sharpe_ratio;
    int32_t total_trades;
    float win_rate;
};

/**
 * GPU Compute Engine
 *
 * Main interface for GPU-accelerated operations
 */
class GPUCompute {
public:
    GPUCompute();
    ~GPUCompute();

    // Initialize GPU (returns false if no compatible GPU found)
    bool Initialize(const GPUConfig& config = GPUConfig());

    // Check if GPU is available and initialized
    bool IsAvailable() const { return m_initialized; }

    // Get device information
    GPUDeviceInfo GetDeviceInfo() const;

    // Get list of all available GPUs
    static std::vector<GPUDeviceInfo> EnumerateDevices();

    /**
     * Upload tick data to GPU memory
     * Returns buffer ID for reference
     */
    uint32_t UploadTickData(const std::vector<GPUTick>& ticks);

    /**
     * Run parallel grid search optimization
     *
     * @param tick_buffer_id - ID of uploaded tick data
     * @param param_combinations - Vector of parameter sets to test
     * @param initial_balance - Starting balance
     * @param contract_size - Contract size for the instrument
     * @param leverage - Account leverage
     * @param progress_callback - Optional callback for progress updates
     * @return Vector of results (same order as param_combinations)
     */
    std::vector<GPUBacktestResult> RunGridSearch(
        uint32_t tick_buffer_id,
        const std::vector<GPUStrategyParams>& param_combinations,
        double initial_balance,
        double contract_size,
        double leverage,
        std::function<void(int completed, int total)> progress_callback = nullptr
    );

    /**
     * Vectorized indicator calculations
     */

    // Simple Moving Average
    std::vector<float> CalculateSMA(
        const std::vector<float>& prices,
        int period
    );

    // Exponential Moving Average
    std::vector<float> CalculateEMA(
        const std::vector<float>& prices,
        int period
    );

    // RSI
    std::vector<float> CalculateRSI(
        const std::vector<float>& prices,
        int period
    );

    // ATR (Average True Range)
    std::vector<float> CalculateATR(
        const std::vector<float>& high,
        const std::vector<float>& low,
        const std::vector<float>& close,
        int period
    );

    // Bollinger Bands (returns mid, upper, lower)
    std::tuple<std::vector<float>, std::vector<float>, std::vector<float>>
    CalculateBollingerBands(
        const std::vector<float>& prices,
        int period,
        float num_std_dev = 2.0f
    );

    // MACD (returns macd, signal, histogram)
    std::tuple<std::vector<float>, std::vector<float>, std::vector<float>>
    CalculateMACD(
        const std::vector<float>& prices,
        int fast_period = 12,
        int slow_period = 26,
        int signal_period = 9
    );

    /**
     * Memory management
     */
    void ReleaseBuffer(uint32_t buffer_id);
    void ReleaseAllBuffers();
    size_t GetUsedMemory() const;
    size_t GetAvailableMemory() const;

    /**
     * Synchronization
     */
    void WaitForCompletion();

private:
    bool m_initialized;
    GPUConfig m_config;

    // Implementation details (pimpl pattern)
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // Shader compilation
    bool CompileShaders();

    // Create compute pipelines
    bool CreatePipelines();
};

/**
 * GPU-accelerated Monte Carlo simulation
 */
class GPUMonteCarlo {
public:
    GPUMonteCarlo(GPUCompute& compute);

    struct Config {
        int num_simulations = 10000;
        bool shuffle_trades = true;
        bool vary_slippage = true;
        float slippage_stddev = 0.3f;  // Points
        float skip_trade_probability = 0.1f;
    };

    struct Result {
        float mean_profit;
        float median_profit;
        float profit_5th_percentile;
        float profit_95th_percentile;
        float probability_of_loss;
        float max_drawdown_95th_percentile;
    };

    Result Run(
        const std::vector<float>& trade_profits,
        float initial_balance,
        const Config& config = Config()
    );

private:
    GPUCompute& m_compute;
};

/**
 * GPU-accelerated Walk-Forward Optimization
 */
class GPUWalkForward {
public:
    GPUWalkForward(GPUCompute& compute);

    struct Config {
        int in_sample_days = 90;
        int out_of_sample_days = 30;
        int step_days = 30;
        int param_combinations_per_window = 1000;
    };

    struct WindowResult {
        int64_t is_start;
        int64_t is_end;
        int64_t oos_start;
        int64_t oos_end;
        GPUStrategyParams best_params;
        float is_profit;
        float oos_profit;
    };

    struct Result {
        std::vector<WindowResult> windows;
        float total_oos_profit;
        float robustness_score;  // 0-100
        float degradation_pct;   // IS vs OOS gap
    };

    Result Run(
        uint32_t tick_buffer_id,
        const std::vector<GPUStrategyParams>& param_space,
        double initial_balance,
        double contract_size,
        double leverage,
        int64_t start_timestamp,
        int64_t end_timestamp,
        const Config& config = Config()
    );

private:
    GPUCompute& m_compute;
};

} // namespace gpu
} // namespace backtest

#endif // GPU_COMPUTE_H
