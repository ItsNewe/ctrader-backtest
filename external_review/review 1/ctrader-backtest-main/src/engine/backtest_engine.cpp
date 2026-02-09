#include "../include/backtest_engine.h"
#include <iostream>

namespace backtest {

// ==================== BacktestEngine Implementation ====================

BacktestResult BacktestEngine::RunBarByBar(IStrategy* strategy,
                                          StrategyParams* params) {
  std::vector<Trade> trades;
  std::vector<Position> positions(1);  // Single position support for now
  Position& position = positions[0];
  current_balance_ = config_.initial_balance;
  current_equity_ = config_.initial_balance;
  current_margin_used_ = 0;

  strategy->OnInit();

  for (size_t i = 0; i < bars_.size(); ++i) {
    const Bar& bar = bars_[i];

    // Apply daily swap at 00:00
    ApplySwap(positions, bar.time);

    strategy->OnBar(bars_, i, position, trades, config_);

    // Simplified SL/TP check using bar high/low
    if (position.is_open) {
      Tick tick;
      tick.time = bar.time;
      tick.bid = bar.close;
      tick.ask = bar.close + config_.spread_points * config_.point_value;

      if (position.is_buy) {
        if (position.stop_loss > 0 && bar.low <= position.stop_loss) {
          ClosePosition(position, tick, trades, "sl");
        } else if (position.take_profit > 0 && bar.high >= position.take_profit) {
          ClosePosition(position, tick, trades, "tp");
        }
      } else {
        if (position.stop_loss > 0 && bar.high >= position.stop_loss) {
          ClosePosition(position, tick, trades, "sl");
        } else if (position.take_profit > 0 && bar.low <= position.take_profit) {
          ClosePosition(position, tick, trades, "tp");
        }
      }

      // Update equity with unrealized profit
      double current_price = position.is_buy ? tick.bid : tick.ask;
      UpdateUnrealizedProfit(position, current_price, config_);
      current_equity_ = current_balance_ + position.unrealized_profit;
    } else {
      current_equity_ = current_balance_;
    }
  }

  strategy->OnDeinit();
  return CalculateMetrics(trades, params, bars_.size());
}

BacktestResult BacktestEngine::RunTickByTick(IStrategy* strategy,
                                            StrategyParams* params) {
  std::vector<Trade> trades;
  std::vector<Position> positions(1);  // Single position support for now
  Position& position = positions[0];
  current_balance_ = config_.initial_balance;
  current_equity_ = config_.initial_balance;
  current_margin_used_ = 0;
  uint64_t tick_count = 0;

  strategy->OnInit();

  // Build bar index for strategy access
  size_t current_bar_idx = 0;

  for (const auto& tick : ticks_) {
    // Update current bar index
    while (current_bar_idx < bars_.size() - 1 &&
           tick.time >= bars_[current_bar_idx + 1].time) {
      current_bar_idx++;
    }

    // Apply daily swap at 00:00
    ApplySwap(positions, tick.time);

    // Check SL/TP first (before strategy logic)
    if (position.is_open) {
      if (CheckStopLoss(position, tick)) {
        ClosePosition(position, tick, trades, "sl");
      } else if (CheckTakeProfit(position, tick)) {
        ClosePosition(position, tick, trades, "tp");
      }
    }

    // Update unrealized profit
    if (position.is_open) {
      double current_price = position.is_buy ? tick.bid : tick.ask;
      UpdateUnrealizedProfit(position, current_price, config_);
      current_equity_ = current_balance_ + position.unrealized_profit;
    } else {
      current_equity_ = current_balance_;
    }

    // Call strategy
    strategy->OnTick(tick, bars_, position, trades, config_);

    tick_count++;
  }

  strategy->OnDeinit();

  BacktestResult result = CalculateMetrics(trades, params, tick_count);
  result.total_ticks_processed = tick_count;
  return result;
}

BacktestResult BacktestEngine::RunEveryTickOHLC(IStrategy* strategy,
                                               StrategyParams* params) {
  // Generate ticks from OHLC and run tick-by-tick
  std::cout << "Generating ticks from OHLC bars..." << std::endl;

  ticks_.clear();
  for (const auto& bar : bars_) {
    auto bar_ticks = TickGenerator::GenerateTicksFromBar(bar, 50);
    ticks_.insert(ticks_.end(), bar_ticks.begin(), bar_ticks.end());
  }

  std::cout << "Generated " << ticks_.size() << " ticks" << std::endl;
  return RunTickByTick(strategy, params);
}

BacktestResult BacktestEngine::CalculateMetrics(
    const std::vector<Trade>& trades, StrategyParams* params,
    uint64_t data_points) const {
  BacktestResult result;
  result.params = params;
  result.trades = trades;
  result.total_trades = trades.size();
  result.final_balance = current_balance_;
  result.final_equity = current_equity_;

  if (trades.empty()) {
    return result;
  }

  double balance = config_.initial_balance;
  double peak_balance = balance;
  double max_dd = 0.0;
  double gross_profit = 0.0;
  double gross_loss = 0.0;
  double total_slippage = 0.0;
  std::vector<double> returns;

  for (const auto& trade : trades) {
    balance += trade.profit;

    if (trade.profit > 0) {
      result.winning_trades++;
      gross_profit += trade.profit;
      result.avg_win += trade.profit;
    } else if (trade.profit < 0) {
      result.losing_trades++;
      gross_loss += std::abs(trade.profit);
      result.avg_loss += trade.profit;
    }

    result.total_commission += trade.commission;
    result.total_swap += trade.swap;
    total_slippage += std::abs(trade.slippage_points);

    if (balance > peak_balance) {
      peak_balance = balance;
    }
    double dd = peak_balance - balance;
    if (dd > max_dd) {
      max_dd = dd;
    }

    returns.push_back(trade.profit / config_.initial_balance);
  }

  result.max_drawdown = max_dd;
  result.max_drawdown_percent = (max_dd / peak_balance) * 100.0;
  result.avg_slippage = total_slippage / result.total_trades;

  if (result.winning_trades > 0) {
    result.avg_win /= result.winning_trades;
  }
  if (result.losing_trades > 0) {
    result.avg_loss /= result.losing_trades;
  }

  result.win_rate = (result.total_trades > 0)
                        ? (static_cast<double>(result.winning_trades) /
                           result.total_trades * 100.0)
                        : 0.0;

  result.profit_factor = (gross_loss > 0) ? (gross_profit / gross_loss) : 0.0;
  result.avg_trade =
      (balance - config_.initial_balance) / result.total_trades;
  result.expectancy = result.avg_trade;

  if (!returns.empty()) {
    double mean_return = 0.0;
    for (double r : returns) mean_return += r;
    mean_return /= returns.size();

    double variance = 0.0;
    for (double r : returns) {
      variance += (r - mean_return) * (r - mean_return);
    }
    variance /= returns.size();

    double std_dev = std::sqrt(variance);
    result.sharpe_ratio = (std_dev > 0) ? (mean_return / std_dev) * std::sqrt(252.0)
                                        : 0.0;
  }

  return result;
}

BacktestResult BacktestEngine::RunBacktest(IStrategy* strategy,
                                          StrategyParams* params) {
  auto start_time = std::chrono::high_resolution_clock::now();

  BacktestResult result;

  switch (config_.mode) {
    case BacktestMode::BAR_BY_BAR:
      result = RunBarByBar(strategy, params);
      break;
    case BacktestMode::EVERY_TICK:
      result = RunTickByTick(strategy, params);
      break;
    case BacktestMode::EVERY_TICK_OHLC:
      result = RunEveryTickOHLC(strategy, params);
      break;
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  result.execution_time_ms = duration.count();

  return result;
}

}  // namespace backtest
