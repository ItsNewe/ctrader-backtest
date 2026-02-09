#ifndef BACKTEST_ENGINE_H
#define BACKTEST_ENGINE_H

#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <atomic>
#include <functional>
#include <map>
#include "margin_manager.h"
#include "swap_manager.h"
#include "mt5_validated_constants.h"
#include "currency_converter.h"
#include "position_validator.h"
#include "currency_rate_manager.h"

// ==================== Data Structures ====================

#include <stdexcept>
#include <sstream>

#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_WARN(msg) std::cout << "[WARN] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

namespace backtest {

class EngineException : public std::runtime_error {
public:
    explicit EngineException(const std::string& message) : std::runtime_error(message) {}
};

struct Tick {
  uint64_t time;
  uint64_t time_msc;  // Milliseconds
  double bid;
  double ask;
  double last;
  uint64_t volume;
  int flags;  // Tick flags (buy/sell)

  Tick()
      : time(0), time_msc(0), bid(0), ask(0), last(0), volume(0), flags(0) {}
  Tick(uint64_t t, uint64_t t_msc, double b, double a, double l,
       uint64_t v, int f)
      : time(t), time_msc(t_msc), bid(b), ask(a), last(l), volume(v),
        flags(f) {}

  double GetSpread() const { return ask - bid; }
  double GetMid() const { return (bid + ask) / 2.0; }
};

struct Bar {
  uint64_t time;
  double open;
  double high;
  double low;
  double close;
  uint64_t volume;
  uint64_t tick_volume;

  Bar()
      : time(0), open(0), high(0), low(0), close(0), volume(0),
        tick_volume(0) {}
  Bar(uint64_t t, double o, double h, double l, double c, uint64_t v,
      uint64_t tv = 0)
      : time(t), open(o), high(h), low(l), close(c), volume(v),
        tick_volume(tv) {}
};

struct Trade {
  uint64_t entry_time;
  uint64_t entry_time_msc;
  uint64_t exit_time;
  uint64_t exit_time_msc;
  double entry_price;
  double exit_price;
  double volume;
  bool is_buy;
  double profit;
  double commission;
  double swap;
  int slippage_points;
  std::string exit_reason;  // "signal", "sl", "tp", "manual"

  Trade()
      : entry_time(0), entry_time_msc(0), exit_time(0), exit_time_msc(0),
        entry_price(0), exit_price(0), volume(0), is_buy(true), profit(0),
        commission(0), swap(0), slippage_points(0), exit_reason("") {}

  double GetDurationSeconds() const {
    return (exit_time - entry_time) +
           (exit_time_msc - entry_time_msc) / 1000.0;
  }
};

struct Position {
  bool is_open;
  bool is_buy;
  uint64_t entry_time;
  uint64_t entry_time_msc;
  double entry_price;
  double volume;
  double stop_loss;
  double take_profit;
  double unrealized_profit;
  int magic_number;
  double margin;                // Required margin for this position
  double swap_accumulated;      // Accumulated swap charges

  Position()
      : is_open(false), is_buy(true), entry_time(0), entry_time_msc(0),
        entry_price(0), volume(0), stop_loss(0), take_profit(0),
        unrealized_profit(0), magic_number(0), margin(0), swap_accumulated(0) {}
};

enum class BacktestMode {
  BAR_BY_BAR,      // Traditional bar-based backtesting
  EVERY_TICK,      // Tick-by-tick with real tick data
  EVERY_TICK_OHLC  // Generate ticks from OHLC bars
};

struct BacktestConfig {
  BacktestMode mode;
  double initial_balance;
  double commission_per_lot;
  int spread_points;
  double point_value;
  double lot_size;
  bool enable_slippage;
  int max_slippage_points;
  double swap_long_per_lot;    // Per day
  double swap_short_per_lot;   // Per day
  bool realistic_slippage;     // Model slippage based on spread widening
  bool model_spread_widening;  // Model wider spreads during volatility
  int leverage;
  double margin_call_level;   // Percentage
  double stop_out_level;      // Percentage

  // Broker-specific parameters (query from MT symbol/account info)
  int triple_swap_day;         // Day for triple swap (0-6: Sun-Sat, default 3=Wed)
  int margin_mode;             // Margin calculation mode (0=FOREX, 3=CFD_LEVERAGE, etc.)
  int symbol_leverage;         // Symbol-specific leverage (for CFD_LEVERAGE mode)

  // Symbol trading limits (from MT symbol specification)
  double volume_min;           // Minimum lot size (e.g., 0.01)
  double volume_max;           // Maximum lot size (e.g., 100.0)
  double volume_step;          // Lot size step (e.g., 0.01)
  int stops_level;             // Minimum SL/TP distance in points

  // Currency information (for cross-currency calculations)
  std::string account_currency;   // Account currency (e.g., "USD")
  std::string symbol_base;        // Symbol base currency (e.g., "EUR" from EURUSD)
  std::string symbol_quote;       // Symbol quote currency (e.g., "USD" from EURUSD)
  std::string margin_currency;    // Margin calculation currency (usually base)
  std::string profit_currency;    // Profit calculation currency (usually quote)

  // Margin specification (for FUTURES and some CFDs)
  double margin_initial;          // Initial margin requirement
  double margin_maintenance;      // Maintenance margin requirement

  BacktestConfig()
      : mode(BacktestMode::BAR_BY_BAR),
        initial_balance(10000.0),
        commission_per_lot(7.0),
        spread_points(10),
        point_value(0.0001),
        lot_size(100000.0),
        enable_slippage(false),
        max_slippage_points(3),
        swap_long_per_lot(-0.5),
        swap_short_per_lot(-0.5),
        realistic_slippage(false),
        model_spread_widening(false),
        leverage(100),
        margin_call_level(100.0),
        stop_out_level(50.0),
        triple_swap_day(3),      // Default: Wednesday
        margin_mode(0),          // Default: FOREX
        symbol_leverage(0),      // Default: use account leverage
        volume_min(0.01),        // Default minimum lot
        volume_max(100.0),       // Default maximum lot
        volume_step(0.01),       // Default step
        stops_level(0),          // Default: no minimum distance
        account_currency("USD"), // Default account currency
        symbol_base("EUR"),      // Default symbol base
        symbol_quote("USD"),     // Default symbol quote
        margin_currency("EUR"),  // Default margin currency
        profit_currency("USD"),  // Default profit currency
        margin_initial(0),       // Default: calculated
        margin_maintenance(0) {} // Default: calculated
};

struct StrategyParams {
  virtual ~StrategyParams() = default;
};

struct BacktestResult {
  StrategyParams* params;
  std::vector<Trade> trades;
  double final_balance;
  double final_equity;
  int total_trades;
  int winning_trades;
  int losing_trades;
  double profit_factor;
  double sharpe_ratio;
  double max_drawdown;
  double max_drawdown_percent;
  double win_rate;
  double avg_win;
  double avg_loss;
  double avg_trade;
  double expectancy;
  double execution_time_ms;
  uint64_t total_ticks_processed;
  double avg_slippage;
  double total_commission;
  double total_swap;

  BacktestResult()
      : params(nullptr),
        final_balance(0),
        final_equity(0),
        total_trades(0),
        winning_trades(0),
        losing_trades(0),
        profit_factor(0),
        sharpe_ratio(0),
        max_drawdown(0),
        max_drawdown_percent(0),
        win_rate(0),
        avg_win(0),
        avg_loss(0),
        avg_trade(0),
        expectancy(0),
        execution_time_ms(0),
        total_ticks_processed(0),
        avg_slippage(0),
        total_commission(0),
        total_swap(0) {}
};

// ==================== Strategy Interfaces ====================

class IStrategy {
 public:
  virtual ~IStrategy() = default;

  /// Called on each new bar
  virtual void OnBar(const std::vector<Bar>& bars, int current_index,
                     Position& position, std::vector<Trade>& trades,
                     const BacktestConfig& config) {}

  /// Called on each tick (only in tick-by-tick mode)
  virtual void OnTick(const Tick& tick, const std::vector<Bar>& bars,
                      Position& position, std::vector<Trade>& trades,
                      const BacktestConfig& config) {}

  virtual void OnInit() {}
  virtual void OnDeinit() {}
  virtual IStrategy* Clone() const = 0;
};

// ==================== Tick Generator ====================

class TickGenerator {
 public:
  /// Generate realistic tick sequence from OHLC bar
  static std::vector<Tick> GenerateTicksFromBar(const Bar& bar,
                                                int ticks_per_bar = 100) {
    std::vector<Tick> ticks;

    if (ticks_per_bar <= 0) return ticks;

    // Determine price path: O->H->L->C or O->L->H->C
    bool high_first = (bar.open < bar.close);

    std::vector<double> price_points;
    price_points.reserve(4);

    if (high_first) {
      // Bullish bar: open -> high -> low -> close
      price_points = {bar.open, bar.high, bar.low, bar.close};
    } else {
      // Bearish bar: open -> low -> high -> close
      price_points = {bar.open, bar.low, bar.high, bar.close};
    }

    // Distribute ticks across the path
    int ticks_per_segment = ticks_per_bar / 3;
    uint64_t time_per_tick = 1;  // Milliseconds between ticks

    for (size_t seg = 0; seg < 3; ++seg) {
      double start_price = price_points[seg];
      double end_price = price_points[seg + 1];

      for (int i = 0; i < ticks_per_segment; ++i) {
        double progress = static_cast<double>(i) / ticks_per_segment;
        double price = start_price + (end_price - start_price) * progress;

        Tick tick;
        tick.time = bar.time;
        tick.time_msc = bar.time * 1000 + (seg * ticks_per_segment + i) * time_per_tick;
        tick.bid = price;
        tick.ask = price + 0.0001;  // Simplified spread
        tick.last = price;
        tick.volume = bar.volume / ticks_per_bar;
        tick.flags = 0;

        ticks.push_back(tick);
      }
    }

    return ticks;
  }
};

// ==================== Market Data Loader ====================

class DataLoader {
 public:
  static std::vector<Bar> LoadBarsFromCSV(const std::string& filename) {
    std::vector<Bar> bars;
    std::ifstream file(filename);

    if (!file.is_open()) {
      std::cerr << "Failed to open file: " << filename << std::endl;
      return bars;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line)) {
      std::stringstream ss(line);
      std::string item;
      Bar bar;

      std::getline(ss, item, ',');
      bar.time = std::stoull(item);

      std::getline(ss, item, ',');
      bar.open = std::stod(item);

      std::getline(ss, item, ',');
      bar.high = std::stod(item);

      std::getline(ss, item, ',');
      bar.low = std::stod(item);

      std::getline(ss, item, ',');
      bar.close = std::stod(item);

      std::getline(ss, item, ',');
      bar.volume = std::stoull(item);

      if (std::getline(ss, item, ',')) {
        bar.tick_volume = std::stoull(item);
      }

      bars.push_back(bar);
    }

    file.close();
    std::cout << "Loaded " << bars.size() << " bars from " << filename
              << std::endl;
    return bars;
  }

  static std::vector<Tick> LoadTicksFromCSV(const std::string& filename) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);

    if (!file.is_open()) {
      std::cerr << "Failed to open file: " << filename << std::endl;
      return ticks;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line)) {
      std::stringstream ss(line);
      std::string item;
      Tick tick;

      // Format: time,time_msc,bid,ask,last,volume,flags
      std::getline(ss, item, ',');
      tick.time = std::stoull(item);

      std::getline(ss, item, ',');
      tick.time_msc = std::stoull(item);

      std::getline(ss, item, ',');
      tick.bid = std::stod(item);

      std::getline(ss, item, ',');
      tick.ask = std::stod(item);

      std::getline(ss, item, ',');
      tick.last = std::stod(item);

      std::getline(ss, item, ',');
      tick.volume = std::stoull(item);

      if (std::getline(ss, item, ',')) {
        tick.flags = std::stoi(item);
      }

      ticks.push_back(tick);
    }

    file.close();
    std::cout << "Loaded " << ticks.size() << " ticks from " << filename
              << std::endl;
    return ticks;
  }

  static bool SaveTicksToCSV(const std::string& filename,
                            const std::vector<Tick>& ticks) {
    std::ofstream file(filename);

    if (!file.is_open()) {
      std::cerr << "Failed to create file: " << filename << std::endl;
      return false;
    }

    file << "time,time_msc,bid,ask,last,volume,flags\n";

    for (const auto& tick : ticks) {
      file << tick.time << "," << tick.time_msc << ","
           << std::fixed << std::setprecision(5) << tick.bid << ","
           << tick.ask << "," << tick.last << "," << tick.volume << ","
           << tick.flags << "\n";
    }

    file.close();
    return true;
  }

  static bool SaveBarsToCSV(const std::string& filename,
                           const std::vector<Bar>& bars) {
    std::ofstream file(filename);

    if (!file.is_open()) {
      std::cerr << "Failed to create file: " << filename << std::endl;
      return false;
    }

    file << "time,open,high,low,close,volume,tick_volume\n";

    for (const auto& bar : bars) {
      file << bar.time << ","
           << std::fixed << std::setprecision(5) << bar.open << "," << bar.high
           << "," << bar.low << "," << bar.close << "," << bar.volume << ","
           << bar.tick_volume << "\n";
    }

    file.close();
    return true;
  }
};

// ==================== Core Backtesting Engine ====================

class BacktestEngine {
 private:
  BacktestConfig config_;
  std::vector<Bar> bars_;
  std::vector<Tick> ticks_;
  double current_balance_;
  double current_equity_;
  double current_margin_used_;  // Total margin used by all positions
  SwapManager swap_manager_;    // MT5-validated swap timing
  CurrencyConverter currency_converter_;  // Cross-currency calculations
  CurrencyRateManager rate_manager_;  // Conversion rate management

  double CalculateProfit(const Trade& trade,
                        const BacktestConfig& config) const {
    double price_diff = trade.is_buy ? (trade.exit_price - trade.entry_price)
                                     : (trade.entry_price - trade.exit_price);

    double profit = price_diff * trade.volume * config.lot_size;
    return profit;
  }

  int SimulateSlippage(bool is_buy, const BacktestConfig& config) const {
    if (!config.enable_slippage || config.max_slippage_points == 0) {
      return 0;
    }

    // Random slippage between 0 and max_slippage_points
    int slippage = rand() % (config.max_slippage_points + 1);
    return is_buy ? slippage : -slippage;
  }

  void UpdateUnrealizedProfit(Position& position, double current_price,
                             const BacktestConfig& config) {
    if (!position.is_open) {
      position.unrealized_profit = 0;
      return;
    }

    double price_diff = position.is_buy ? (current_price - position.entry_price)
                                        : (position.entry_price - current_price);

    position.unrealized_profit = price_diff * position.volume * config.lot_size;
  }

  bool CheckStopLoss(const Position& position, const Tick& tick) const {
    if (!position.is_open || position.stop_loss == 0) return false;

    if (position.is_buy) {
      return tick.bid <= position.stop_loss;
    } else {
      return tick.ask >= position.stop_loss;
    }
  }

  bool CheckTakeProfit(const Position& position, const Tick& tick) const {
    if (!position.is_open || position.take_profit == 0) return false;

    if (position.is_buy) {
      return tick.bid >= position.take_profit;
    } else {
      return tick.ask <= position.take_profit;
    }
  }

  void ClosePosition(Position& position, const Tick& tick,
                   std::vector<Trade>& trades, const std::string& reason) {
    if (!position.is_open) return;

    Trade trade;
    trade.entry_time = position.entry_time;
    trade.entry_time_msc = position.entry_time_msc;
    trade.exit_time = tick.time;
    trade.exit_time_msc = tick.time_msc;
    trade.entry_price = position.entry_price;
    trade.is_buy = position.is_buy;
    trade.volume = position.volume;
    trade.exit_reason = reason;

    // Apply slippage
    trade.slippage_points = SimulateSlippage(!trade.is_buy, config_);

    // Determine exit price
    if (reason == "sl") {
      trade.exit_price = position.stop_loss;
    } else if (reason == "tp") {
      trade.exit_price = position.take_profit;
    } else {
      trade.exit_price = trade.is_buy ? tick.bid : tick.ask;
      trade.exit_price += trade.slippage_points * config_.point_value;
    }

    // Calculate profit/loss in symbol's profit currency (usually quote)
    double profit_in_symbol_currency = CalculateProfit(trade, config_);

    // Convert profit to account currency if needed
    double profit_conversion_rate = rate_manager_.GetProfitConversionRate(
        config_.profit_currency,
        trade.exit_price
    );

    double profit_in_account_currency = currency_converter_.ConvertProfit(
        profit_in_symbol_currency,
        config_.profit_currency,
        profit_conversion_rate
    );

    // Calculate commission
    trade.commission = config_.commission_per_lot * trade.volume * 2;

    // Use accumulated swap from position (MT5-validated daily application)
    trade.swap = position.swap_accumulated;

    // Store profit in account currency
    trade.profit = profit_in_account_currency - trade.commission + trade.swap;

    current_balance_ += trade.profit;
    trades.push_back(trade);
    position.is_open = false;

    // Release margin
    current_margin_used_ -= position.margin;
  }

  bool OpenPosition(Position& position, bool is_buy, double volume,
                   double entry_price, double stop_loss, double take_profit,
                   uint64_t time, uint64_t time_msc) {
    // Step 1: Validate lot size
    std::string validation_error;
    if (!PositionValidator::ValidateLotSize(
        volume, config_.volume_min, config_.volume_max,
        config_.volume_step, &validation_error
    )) {
      // Lot size invalid - could normalize or reject
      // For now, reject to maintain strict broker rules
      return false;
    }

    // Step 2: Calculate margin in symbol's margin currency (usually base)
    double margin_in_symbol_currency = MarginManager::CalculateMargin(
        volume,
        config_.lot_size,
        entry_price,
        config_.leverage,
        static_cast<MarginManager::CalcMode>(config_.margin_mode),
        config_.symbol_leverage  // For CFD_LEVERAGE mode
    );

    // Step 3: Convert margin to account currency if needed
    double margin_conversion_rate = rate_manager_.GetMarginConversionRate(
        config_.margin_currency,
        config_.profit_currency,
        entry_price
    );

    double required_margin = currency_converter_.ConvertMargin(
        margin_in_symbol_currency,
        config_.margin_currency,
        margin_conversion_rate
    );

    // Step 4: Calculate available margin
    double available_margin = current_equity_ - current_margin_used_;

    // Step 5: Validate stop loss and take profit distances
    if (stop_loss != 0) {
      if (!PositionValidator::ValidateStopDistance(
          entry_price, stop_loss, is_buy, config_.stops_level,
          config_.point_value, &validation_error
      )) {
        return false;  // SL too close to entry
      }
    }

    if (take_profit != 0) {
      if (!PositionValidator::ValidateStopDistance(
          entry_price, take_profit, !is_buy, config_.stops_level,
          config_.point_value, &validation_error
      )) {
        return false;  // TP too close to entry
      }
    }

    // Step 6: Validate sufficient margin
    if (!PositionValidator::ValidateMargin(
        required_margin, available_margin, &validation_error
    )) {
      return false;  // Insufficient margin
    }

    // All validations passed - open the position
    position.is_open = true;
    position.is_buy = is_buy;
    position.volume = volume;
    position.entry_price = entry_price;
    position.stop_loss = stop_loss;
    position.take_profit = take_profit;
    position.entry_time = time;
    position.entry_time_msc = time_msc;
    position.margin = required_margin;
    position.swap_accumulated = 0;
    position.unrealized_profit = 0;

    // Reserve margin
    current_margin_used_ += required_margin;

    return true;
  }

  void ApplySwap(std::vector<Position>& positions, uint64_t current_time) {
    if (positions.empty()) return;

    if (!swap_manager_.ShouldApplySwap(current_time)) return;

    int day_of_week = SwapManager::GetDayOfWeek(current_time);

    for (auto& pos : positions) {
      if (!pos.is_open) continue;

      // Use swap manager's instance method with configured triple_swap_day
      double swap = swap_manager_.CalculateSwapForPosition(
          pos.volume,
          pos.is_buy,
          config_.swap_long_per_lot,
          config_.swap_short_per_lot,
          config_.point_value,
          config_.lot_size,
          day_of_week
      );

      pos.swap_accumulated += swap;
      current_balance_ += swap;
    }
  }

  BacktestResult RunBarByBar(IStrategy* strategy, StrategyParams* params);
  BacktestResult RunTickByTick(IStrategy* strategy, StrategyParams* params);
  BacktestResult RunEveryTickOHLC(IStrategy* strategy, StrategyParams* params);

  BacktestResult CalculateMetrics(const std::vector<Trade>& trades,
                                 StrategyParams* params,
                                 uint64_t data_points) const;

 public:
  BacktestEngine(const BacktestConfig& config)
      : config_(config), current_balance_(0), current_equity_(0),
        current_margin_used_(0),
        swap_manager_(MT5Validated::SWAP_HOUR, config.triple_swap_day),
        currency_converter_(config.account_currency),
        rate_manager_(config.account_currency) {}

  void LoadBars(const std::vector<Bar>& bars) { bars_ = bars; }

  void LoadTicks(const std::vector<Tick>& ticks) { ticks_ = ticks; }

  /**
   * Update conversion rate for a currency
   * Used to set rates for cross-currency calculations
   *
   * @param currency Currency code (e.g., "EUR", "GBP", "JPY")
   * @param rate Exchange rate to account currency
   */
  void UpdateConversionRate(const std::string& currency, double rate) {
    rate_manager_.UpdateRate(currency, rate);
  }

  /**
   * Update conversion rate from a symbol price
   *
   * @param symbol Symbol name (e.g., "EURUSD", "GBPJPY")
   * @param bid Current bid price
   */
  void UpdateConversionRateFromSymbol(const std::string& symbol, double bid) {
    rate_manager_.UpdateRateFromSymbol(symbol, bid);
  }

  /**
   * Get required conversion pairs for the configured symbol
   *
   * @return Vector of symbol names to query (e.g., ["GBPUSD", "USDJPY"])
   */
  std::vector<std::string> GetRequiredConversionPairs() const {
    return rate_manager_.GetRequiredConversionPairs(
        config_.symbol_base,
        config_.symbol_quote
    );
  }

  BacktestResult RunBacktest(IStrategy* strategy, StrategyParams* params);

  const std::vector<Bar>& GetBars() const { return bars_; }
  const std::vector<Tick>& GetTicks() const { return ticks_; }
};

// ==================== Thread Pool ====================

class ThreadPool {
 private:
  std::vector<std::thread> threads_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_;

 public:
  ThreadPool(size_t num_threads) : stop_(false) {
    for (size_t i = 0; i < num_threads; ++i) {
      threads_.emplace_back([this] {
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty()) return;

            task = std::move(tasks_.front());
            tasks_.pop();
          }
          task();
        }
      });
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& thread : threads_) {
      thread.join();
    }
  }

  template <class F>
  void Enqueue(F&& f) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      tasks_.emplace(std::forward<F>(f));
    }
    cv_.notify_one();
  }
};

// ==================== Optimizer ====================

class Optimizer {
 private:
  BacktestEngine& engine_;
  size_t num_threads_;

 public:
  Optimizer(BacktestEngine& engine, size_t num_threads = 0)
      : engine_(engine) {
    if (num_threads == 0) {
      num_threads_ = std::thread::hardware_concurrency() - 1;
      if (num_threads_ < 1) num_threads_ = 1;
    } else {
      num_threads_ = num_threads;
    }
  }

  template <typename ParamsType>
  std::vector<BacktestResult> OptimizeParallel(
      IStrategy* strategy_template,
      const std::vector<ParamsType*>& param_combinations) {
    std::vector<BacktestResult> results;
    results.resize(param_combinations.size());

    std::cout << "Running " << param_combinations.size() << " backtests on "
              << num_threads_ << " threads..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    ThreadPool pool(num_threads_);
    std::atomic<size_t> completed(0);

    for (size_t i = 0; i < param_combinations.size(); ++i) {
      pool.Enqueue([this, strategy_template, &param_combinations, &results,
                   &completed, i]() {
        IStrategy* strategy = strategy_template->Clone();
        results[i] = engine_.RunBacktest(strategy, param_combinations[i]);
        delete strategy;

        size_t done = ++completed;
        if (done % 100 == 0 || done == param_combinations.size()) {
          double progress = (double)done / param_combinations.size() * 100.0;
          std::cout << "Progress: " << done << "/" << param_combinations.size()
                    << " (" << std::fixed << std::setprecision(1) << progress
                    << "%)" << std::endl;
        }
      });
    }

    while (completed < param_combinations.size()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time);

    std::cout << "Optimization completed in " << duration.count() << " seconds"
              << std::endl;

    std::sort(
        results.begin(), results.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
          return a.sharpe_ratio > b.sharpe_ratio;
        });

    return results;
  }
};

// ==================== Result Reporting ====================

class Reporter {
 public:
  static void PrintResult(const BacktestResult& result, int rank = 0) {
    if (rank > 0) {
      std::cout << "\n=== Rank " << rank << " ===" << std::endl;
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Final Balance: $" << result.final_balance << std::endl;
    std::cout << "Final Equity: $" << result.final_equity << std::endl;
    std::cout << "Total Trades: " << result.total_trades << std::endl;
    std::cout << "Winning Trades: " << result.winning_trades << std::endl;
    std::cout << "Losing Trades: " << result.losing_trades << std::endl;
    std::cout << "Win Rate: " << result.win_rate << "%" << std::endl;
    std::cout << "Profit Factor: " << result.profit_factor << std::endl;
    std::cout << "Sharpe Ratio: " << result.sharpe_ratio << std::endl;
    std::cout << "Max Drawdown: $" << result.max_drawdown << " ("
              << result.max_drawdown_percent << "%)" << std::endl;
    std::cout << "Avg Win: $" << result.avg_win << std::endl;
    std::cout << "Avg Loss: $" << result.avg_loss << std::endl;
    std::cout << "Expectancy: $" << result.expectancy << std::endl;

    if (result.total_ticks_processed > 0) {
      std::cout << "Ticks Processed: " << result.total_ticks_processed
                << std::endl;
      std::cout << "Avg Slippage: " << result.avg_slippage << " points"
                << std::endl;
    }

    std::cout << "Total Commission: $" << result.total_commission << std::endl;
    std::cout << "Total Swap: $" << result.total_swap << std::endl;
    std::cout << "Execution Time: " << result.execution_time_ms << "ms"
              << std::endl;
  }

  static void SaveResultsToCSV(const std::string& filename,
                              const std::vector<BacktestResult>& results) {
    std::ofstream file(filename);

    if (!file.is_open()) {
      std::cerr << "Failed to create file: " << filename << std::endl;
      return;
    }

    file << "rank,final_balance,final_equity,total_trades,win_rate,"
            "profit_factor,"
            "sharpe_ratio,max_drawdown,max_drawdown_pct,avg_win,avg_loss,"
            "expectancy,total_commission,total_swap,avg_slippage,"
            "ticks_processed,execution_time_ms\n";

    for (size_t i = 0; i < results.size(); ++i) {
      const auto& r = results[i];
      file << (i + 1) << "," << std::fixed << std::setprecision(2)
           << r.final_balance << "," << r.final_equity << "," << r.total_trades
           << "," << r.win_rate << "," << r.profit_factor << ","
           << r.sharpe_ratio << "," << r.max_drawdown << ","
           << r.max_drawdown_percent << "," << r.avg_win << "," << r.avg_loss
           << "," << r.expectancy << "," << r.total_commission << ","
           << r.total_swap << "," << r.avg_slippage << ","
           << r.total_ticks_processed << "," << r.execution_time_ms << "\n";
    }

    file.close();
    std::cout << "Results saved to " << filename << std::endl;
  }

  static void SaveTradesToCSV(const std::string& filename,
                             const std::vector<Trade>& trades) {
    std::ofstream file(filename);

    if (!file.is_open()) {
      std::cerr << "Failed to create file: " << filename << std::endl;
      return;
    }

    file << "entry_time,exit_time,type,entry_price,exit_price,volume,"
            "profit,commission,swap,slippage,duration_sec,exit_reason\n";

    for (const auto& t : trades) {
      file << t.entry_time << "," << t.exit_time << ","
           << (t.is_buy ? "BUY" : "SELL") << ","
           << std::fixed << std::setprecision(5) << t.entry_price << ","
           << t.exit_price << "," << t.volume << ","
           << std::setprecision(2) << t.profit << "," << t.commission << ","
           << t.swap << "," << t.slippage_points << ","
           << t.GetDurationSeconds() << "," << t.exit_reason << "\n";
    }

    file.close();
    std::cout << "Trades saved to " << filename << std::endl;
  }
};

}  // namespace backtest

#endif  // BACKTEST_ENGINE_H
