// BacktestPro GPU Compute Shaders
// WebGPU Shading Language (WGSL)

// =============================================================================
// Data Structures
// =============================================================================

struct Tick {
    timestamp: i64,
    bid: f32,
    ask: f32,
    volume: f32,
    flags: i32,
    _padding: array<i32, 3>,
};

struct StrategyParams {
    survive_pct: f32,
    base_spacing: f32,
    min_volume: f32,
    max_volume: f32,
    lookback_hours: f32,
    _padding: array<f32, 3>,
};

struct BacktestResult {
    final_balance: f32,
    net_profit: f32,
    max_drawdown: f32,
    max_drawdown_pct: f32,
    profit_factor: f32,
    sharpe_ratio: f32,
    total_trades: i32,
    win_rate: f32,
};

struct Position {
    entry_price: f32,
    lots: f32,
    is_buy: i32,  // 1 = buy, 0 = sell
    take_profit: f32,
};

struct BacktestConfig {
    initial_balance: f32,
    contract_size: f32,
    leverage: f32,
    tick_count: u32,
    max_positions: u32,
    _padding: array<u32, 3>,
};

// =============================================================================
// Bindings
// =============================================================================

@group(0) @binding(0) var<storage, read> ticks: array<Tick>;
@group(0) @binding(1) var<storage, read> params: array<StrategyParams>;
@group(0) @binding(2) var<storage, read_write> results: array<BacktestResult>;
@group(0) @binding(3) var<uniform> config: BacktestConfig;

// Workgroup local memory for positions (shared within workgroup)
var<workgroup> positions: array<Position, 256>;
var<workgroup> position_count: atomic<u32>;

// =============================================================================
// Helper Functions
// =============================================================================

fn calculate_pnl(pos: Position, current_bid: f32, current_ask: f32, contract_size: f32) -> f32 {
    if pos.is_buy == 1 {
        return (current_bid - pos.entry_price) * pos.lots * contract_size;
    } else {
        return (pos.entry_price - current_ask) * pos.lots * contract_size;
    }
}

fn check_take_profit(pos: Position, current_bid: f32, current_ask: f32) -> bool {
    if pos.is_buy == 1 {
        return current_bid >= pos.take_profit;
    } else {
        return current_ask <= pos.take_profit;
    }
}

// =============================================================================
// Main Backtest Kernel
// =============================================================================

@compute @workgroup_size(256)
fn backtest_main(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let param_idx = global_id.x;

    // Check bounds
    if param_idx >= arrayLength(&params) {
        return;
    }

    let p = params[param_idx];
    var result = BacktestResult();

    // Initialize
    var balance = config.initial_balance;
    var equity = config.initial_balance;
    var max_equity = config.initial_balance;
    var max_drawdown: f32 = 0.0;
    var gross_profit: f32 = 0.0;
    var gross_loss: f32 = 0.0;
    var total_trades: i32 = 0;
    var winning_trades: i32 = 0;

    // Position tracking (simplified for GPU - using arrays)
    var pos_entries: array<f32, 200>;
    var pos_lots: array<f32, 200>;
    var pos_is_buy: array<i32, 200>;
    var pos_tp: array<f32, 200>;
    var num_positions: u32 = 0;

    var last_buy_price: f32 = 0.0;
    var last_sell_price: f32 = 0.0;

    // Process ticks
    for (var i: u32 = 0; i < config.tick_count; i++) {
        let tick = ticks[i];
        let bid = tick.bid;
        let ask = tick.ask;
        let mid = (bid + ask) * 0.5;

        // Calculate survive distance
        let survive_distance = mid * p.survive_pct / 100.0;

        // Check take profits and close positions
        var j: u32 = 0;
        while j < num_positions {
            var should_close = false;
            var pnl: f32 = 0.0;

            if pos_is_buy[j] == 1 {
                if bid >= pos_tp[j] {
                    pnl = (bid - pos_entries[j]) * pos_lots[j] * config.contract_size;
                    should_close = true;
                }
            } else {
                if ask <= pos_tp[j] {
                    pnl = (pos_entries[j] - ask) * pos_lots[j] * config.contract_size;
                    should_close = true;
                }
            }

            if should_close {
                balance += pnl;
                total_trades++;
                if pnl > 0.0 {
                    winning_trades++;
                    gross_profit += pnl;
                } else {
                    gross_loss -= pnl;
                }

                // Remove position by swapping with last
                num_positions--;
                if j < num_positions {
                    pos_entries[j] = pos_entries[num_positions];
                    pos_lots[j] = pos_lots[num_positions];
                    pos_is_buy[j] = pos_is_buy[num_positions];
                    pos_tp[j] = pos_tp[num_positions];
                }
            } else {
                j++;
            }
        }

        // Calculate floating P/L
        var floating_pnl: f32 = 0.0;
        for (var k: u32 = 0; k < num_positions; k++) {
            if pos_is_buy[k] == 1 {
                floating_pnl += (bid - pos_entries[k]) * pos_lots[k] * config.contract_size;
            } else {
                floating_pnl += (pos_entries[k] - ask) * pos_lots[k] * config.contract_size;
            }
        }
        equity = balance + floating_pnl;

        // Update drawdown
        if equity > max_equity {
            max_equity = equity;
        }
        let dd = max_equity - equity;
        if dd > max_drawdown {
            max_drawdown = dd;
        }

        // Margin stop-out check (20%)
        var margin_used: f32 = 0.0;
        for (var k: u32 = 0; k < num_positions; k++) {
            margin_used += (pos_lots[k] * config.contract_size * mid) / config.leverage;
        }
        if equity < margin_used * 0.2 && num_positions > 0 {
            // Close all positions at stop-out
            balance = equity;
            num_positions = 0;
            continue;
        }

        // Position limits
        if num_positions >= config.max_positions {
            continue;
        }

        let spacing = p.base_spacing;

        // Entry logic
        if num_positions == 0 {
            // Open initial positions
            if num_positions < 200 {
                pos_entries[num_positions] = ask;
                pos_lots[num_positions] = p.min_volume;
                pos_is_buy[num_positions] = 1;
                pos_tp[num_positions] = ask + spacing;
                num_positions++;
                last_buy_price = ask;
            }
            if num_positions < 200 {
                pos_entries[num_positions] = bid;
                pos_lots[num_positions] = p.min_volume;
                pos_is_buy[num_positions] = 0;
                pos_tp[num_positions] = bid - spacing;
                num_positions++;
                last_sell_price = bid;
            }
        } else {
            // Grid logic
            if ask < last_buy_price - spacing && ask > mid - survive_distance {
                if num_positions < 200 {
                    pos_entries[num_positions] = ask;
                    pos_lots[num_positions] = p.min_volume;
                    pos_is_buy[num_positions] = 1;
                    pos_tp[num_positions] = ask + spacing;
                    num_positions++;
                    last_buy_price = ask;
                }
            }

            if bid > last_sell_price + spacing && bid < mid + survive_distance {
                if num_positions < 200 {
                    pos_entries[num_positions] = bid;
                    pos_lots[num_positions] = p.min_volume;
                    pos_is_buy[num_positions] = 0;
                    pos_tp[num_positions] = bid - spacing;
                    num_positions++;
                    last_sell_price = bid;
                }
            }
        }
    }

    // Close remaining positions at end
    let last_tick = ticks[config.tick_count - 1];
    for (var k: u32 = 0; k < num_positions; k++) {
        var pnl: f32;
        if pos_is_buy[k] == 1 {
            pnl = (last_tick.bid - pos_entries[k]) * pos_lots[k] * config.contract_size;
        } else {
            pnl = (pos_entries[k] - last_tick.ask) * pos_lots[k] * config.contract_size;
        }
        balance += pnl;
        total_trades++;
        if pnl > 0.0 {
            winning_trades++;
            gross_profit += pnl;
        } else {
            gross_loss -= pnl;
        }
    }

    // Store results
    result.final_balance = balance;
    result.net_profit = balance - config.initial_balance;
    result.max_drawdown = max_drawdown;
    result.max_drawdown_pct = select(0.0, max_drawdown / max_equity * 100.0, max_equity > 0.0);
    result.profit_factor = select(0.0, gross_profit / gross_loss, gross_loss > 0.0);
    result.total_trades = total_trades;
    result.win_rate = select(0.0, f32(winning_trades) / f32(total_trades) * 100.0, total_trades > 0);

    results[param_idx] = result;
}

// =============================================================================
// Indicator Kernels
// =============================================================================

@group(0) @binding(0) var<storage, read> prices: array<f32>;
@group(0) @binding(1) var<storage, read_write> output: array<f32>;
@group(0) @binding(2) var<uniform> indicator_config: vec4<u32>;  // x=period, y=data_length, z,w=reserved

// Simple Moving Average
@compute @workgroup_size(256)
fn sma_kernel(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let idx = global_id.x;
    let period = indicator_config.x;
    let data_len = indicator_config.y;

    if idx >= data_len || idx < period - 1 {
        output[idx] = 0.0;
        return;
    }

    var sum: f32 = 0.0;
    for (var i: u32 = 0; i < period; i++) {
        sum += prices[idx - i];
    }
    output[idx] = sum / f32(period);
}

// Exponential Moving Average
@compute @workgroup_size(256)
fn ema_kernel(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let idx = global_id.x;
    let period = indicator_config.x;
    let data_len = indicator_config.y;

    if idx >= data_len {
        return;
    }

    let k = 2.0 / (f32(period) + 1.0);

    if idx == 0 {
        output[idx] = prices[idx];
    } else {
        // Note: For proper EMA, this needs sequential execution
        // This is a parallel approximation
        output[idx] = prices[idx] * k + output[idx - 1] * (1.0 - k);
    }
}

// RSI (Relative Strength Index)
@compute @workgroup_size(256)
fn rsi_kernel(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let idx = global_id.x;
    let period = indicator_config.x;
    let data_len = indicator_config.y;

    if idx >= data_len || idx < period {
        output[idx] = 50.0;  // Neutral default
        return;
    }

    var avg_gain: f32 = 0.0;
    var avg_loss: f32 = 0.0;

    // Calculate initial averages
    for (var i: u32 = 1; i <= period; i++) {
        let change = prices[idx - period + i] - prices[idx - period + i - 1];
        if change > 0.0 {
            avg_gain += change;
        } else {
            avg_loss -= change;
        }
    }
    avg_gain /= f32(period);
    avg_loss /= f32(period);

    if avg_loss == 0.0 {
        output[idx] = 100.0;
    } else {
        let rs = avg_gain / avg_loss;
        output[idx] = 100.0 - 100.0 / (1.0 + rs);
    }
}

// Standard Deviation
@compute @workgroup_size(256)
fn stddev_kernel(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let idx = global_id.x;
    let period = indicator_config.x;
    let data_len = indicator_config.y;

    if idx >= data_len || idx < period - 1 {
        output[idx] = 0.0;
        return;
    }

    // Calculate mean
    var sum: f32 = 0.0;
    for (var i: u32 = 0; i < period; i++) {
        sum += prices[idx - i];
    }
    let mean = sum / f32(period);

    // Calculate variance
    var variance: f32 = 0.0;
    for (var i: u32 = 0; i < period; i++) {
        let diff = prices[idx - i] - mean;
        variance += diff * diff;
    }
    variance /= f32(period);

    output[idx] = sqrt(variance);
}

// =============================================================================
// Monte Carlo Kernel
// =============================================================================

struct MonteCarloConfig {
    num_simulations: u32,
    num_trades: u32,
    initial_balance: f32,
    slippage_stddev: f32,
    skip_probability: f32,
    seed: u32,
    _padding: array<u32, 2>,
};

@group(0) @binding(0) var<storage, read> trade_profits: array<f32>;
@group(0) @binding(1) var<storage, read_write> simulation_results: array<f32>;
@group(0) @binding(2) var<uniform> mc_config: MonteCarloConfig;

// Simple pseudo-random number generator (xorshift)
fn random(state: ptr<function, u32>) -> f32 {
    var s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return f32(s) / 4294967295.0;
}

// Box-Muller transform for normal distribution
fn random_normal(state: ptr<function, u32>) -> f32 {
    let u1 = random(state);
    let u2 = random(state);
    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

@compute @workgroup_size(256)
fn monte_carlo_kernel(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let sim_idx = global_id.x;

    if sim_idx >= mc_config.num_simulations {
        return;
    }

    // Initialize RNG state
    var rng_state = mc_config.seed + sim_idx * 1000;

    var balance = mc_config.initial_balance;

    // Shuffle indices (Fisher-Yates-ish, simplified for GPU)
    var shuffled: array<u32, 1024>;  // Max trades supported
    let num_trades = min(mc_config.num_trades, 1024u);

    for (var i: u32 = 0; i < num_trades; i++) {
        shuffled[i] = i;
    }

    for (var i: u32 = num_trades - 1; i > 0; i--) {
        let j = u32(random(&rng_state) * f32(i + 1));
        let temp = shuffled[i];
        shuffled[i] = shuffled[j];
        shuffled[j] = temp;
    }

    // Process shuffled trades
    for (var i: u32 = 0; i < num_trades; i++) {
        // Skip trade randomly
        if random(&rng_state) < mc_config.skip_probability {
            continue;
        }

        var profit = trade_profits[shuffled[i]];

        // Add slippage
        profit += random_normal(&rng_state) * mc_config.slippage_stddev;

        balance += profit;
    }

    simulation_results[sim_idx] = balance - mc_config.initial_balance;
}
