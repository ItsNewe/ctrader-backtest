#include "backtest_engine.h"
#include <iostream>
#include <deque>
#include <string>

using namespace backtest;

// ==================== Example 1: Bar-Based MA Crossover ====================

struct MACrossoverParams : public StrategyParams {
  int fast_period;
  int slow_period;
  double lot_size;
  double stop_loss_points;
  double take_profit_points;

  MACrossoverParams(int fast = 0, int slow = 0, double lots = 0.1,
                   double sl = 0, double tp = 0)
      : fast_period(fast),
        slow_period(slow),
        lot_size(lots),
        stop_loss_points(sl),
        take_profit_points(tp) {}
};

class MACrossoverStrategy : public IStrategy {
 private:
  MACrossoverParams* params_;

  double CalculateSMA(const std::vector<Bar>& bars, int end_index,
                     int period) const {
    if (end_index < period - 1) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < period; ++i) {
      sum += bars[end_index - i].close;
    }
    return sum / period;
  }

 public:
  MACrossoverStrategy(MACrossoverParams* params) : params_(params) {}

  void OnBar(const std::vector<Bar>& bars, int current_index,
             Position& position, std::vector<Trade>& trades,
             const BacktestConfig& config) override {

    if (current_index < params_->slow_period) return;

    double fast_ma_curr =
        CalculateSMA(bars, current_index, params_->fast_period);
    double slow_ma_curr =
        CalculateSMA(bars, current_index, params_->slow_period);
    double fast_ma_prev =
        CalculateSMA(bars, current_index - 1, params_->fast_period);
    double slow_ma_prev =
        CalculateSMA(bars, current_index - 1, params_->slow_period);

    const Bar& current = bars[current_index];

    if (position.is_open) {
      bool should_close = false;

      if (position.is_buy && fast_ma_curr < slow_ma_curr &&
          fast_ma_prev >= slow_ma_prev) {
        should_close = true;
      } else if (!position.is_buy && fast_ma_curr > slow_ma_curr &&
                 fast_ma_prev <= slow_ma_prev) {
        should_close = true;
      }

      if (should_close) {
        position.is_open = false;
      }
    }

    if (!position.is_open) {
      if (fast_ma_curr > slow_ma_curr && fast_ma_prev <= slow_ma_prev) {
        position.is_open = true;
        position.is_buy = true;
        position.entry_time = current.time;
        position.entry_price =
            current.close + config.spread_points * config.point_value;
        position.volume = params_->lot_size;

        if (params_->stop_loss_points > 0) {
          position.stop_loss = position.entry_price -
                               params_->stop_loss_points * config.point_value;
        }
        if (params_->take_profit_points > 0) {
          position.take_profit = position.entry_price +
                                 params_->take_profit_points * config.point_value;
        }
      } else if (fast_ma_curr < slow_ma_curr && fast_ma_prev >= slow_ma_prev) {
        position.is_open = true;
        position.is_buy = false;
        position.entry_time = current.time;
        position.entry_price = current.close;
        position.volume = params_->lot_size;

        if (params_->stop_loss_points > 0) {
          position.stop_loss = position.entry_price +
                               params_->stop_loss_points * config.point_value;
        }
        if (params_->take_profit_points > 0) {
          position.take_profit = position.entry_price -
                                 params_->take_profit_points * config.point_value;
        }
      }
    }
  }

  void OnTick(const Tick& tick, const std::vector<Bar>& bars,
              Position& position, std::vector<Trade>& trades,
              const BacktestConfig& config) override {}

  IStrategy* Clone() const override {
    return new MACrossoverStrategy(params_);
  }
};

// ==================== Example 2: Tick-Based Scalping Strategy ====================

struct ScalpingParams : public StrategyParams {
  int tick_buffer_size;
  double price_threshold;
  double lot_size;
  int stop_loss_points;
  int take_profit_points;
  int max_spread_points;

  ScalpingParams(int buffer = 100, double threshold = 0.0001, double lots = 0.1,
                 int sl = 50, int tp = 100, int max_spread = 20)
      : tick_buffer_size(buffer),
        price_threshold(threshold),
        lot_size(lots),
        stop_loss_points(sl),
        take_profit_points(tp),
        max_spread_points(max_spread) {}
};

class ScalpingStrategy : public IStrategy {
 private:
  ScalpingParams* params_;
  std::deque<double> bid_prices_;
  std::deque<double> ask_prices_;
  int ticks_since_trade_;

  double CalculateVolatility() const {
    if (bid_prices_.size() < 2) return 0.0;

    double sum = 0.0;
    double mean = 0.0;

    for (double price : bid_prices_) {
      mean += price;
    }
    mean /= bid_prices_.size();

    for (double price : bid_prices_) {
      sum += (price - mean) * (price - mean);
    }

    return std::sqrt(sum / bid_prices_.size());
  }

  double CalculateMomentum() const {
    if (bid_prices_.size() < 10) return 0.0;

    int lookback = std::min(20, (int)bid_prices_.size());
    double recent_avg = 0.0;
    double older_avg = 0.0;
    int half = lookback / 2;

    for (int i = 0; i < half; ++i) {
      recent_avg += bid_prices_[i];
      older_avg += bid_prices_[i + half];
    }

    recent_avg /= half;
    older_avg /= half;

    return recent_avg - older_avg;
  }

 public:
  ScalpingStrategy(ScalpingParams* params)
      : params_(params), ticks_since_trade_(0) {}

  void OnInit() override {
    bid_prices_.clear();
    ask_prices_.clear();
    ticks_since_trade_ = 0;
  }

  void OnTick(const Tick& tick, const std::vector<Bar>& bars,
              Position& position, std::vector<Trade>& trades,
              const BacktestConfig& config) override {

    // Update price buffers
    bid_prices_.push_front(tick.bid);
    ask_prices_.push_front(tick.ask);

    if (bid_prices_.size() > (size_t)params_->tick_buffer_size) {
      bid_prices_.pop_back();
      ask_prices_.pop_back();
    }

    ticks_since_trade_++;

    // Need enough data
    if (bid_prices_.size() < (size_t)params_->tick_buffer_size) {
      return;
    }

    // Check spread condition
    int current_spread =
        (int)((tick.ask - tick.bid) / config.point_value);
    if (current_spread > params_->max_spread_points) {
      return;
    }

    // Don't trade too frequently
    if (ticks_since_trade_ < 50) {
      return;
    }

    double volatility = CalculateVolatility();
    double momentum = CalculateMomentum();

    // Entry logic: Mean reversion on high volatility
    if (!position.is_open && volatility > params_->price_threshold) {

      // Buy signal: Strong negative momentum (price dropped)
      if (momentum < -params_->price_threshold * 0.5) {
        position.is_open = true;
        position.is_buy = true;
        position.entry_time = tick.time;
        position.entry_time_msc = tick.time_msc;
        position.entry_price = tick.ask;
        position.volume = params_->lot_size;
        position.stop_loss =
            tick.ask - params_->stop_loss_points * config.point_value;
        position.take_profit =
            tick.ask + params_->take_profit_points * config.point_value;

        ticks_since_trade_ = 0;
      }
      // Sell signal: Strong positive momentum (price surged)
      else if (momentum > params_->price_threshold * 0.5) {
        position.is_open = true;
        position.is_buy = false;
        position.entry_time = tick.time;
        position.entry_time_msc = tick.time_msc;
        position.entry_price = tick.bid;
        position.volume = params_->lot_size;
        position.stop_loss =
            tick.bid + params_->stop_loss_points * config.point_value;
        position.take_profit =
            tick.bid - params_->take_profit_points * config.point_value;

        ticks_since_trade_ = 0;
      }
    }
  }

  void OnBar(const std::vector<Bar>& bars, int current_index,
             Position& position, std::vector<Trade>& trades,
             const BacktestConfig& config) override {}

  IStrategy* Clone() const override {
    return new ScalpingStrategy(params_);
  }
};

// ==================== Example 3: Breakout Strategy ====================

struct BreakoutParams : public StrategyParams {
  int lookback_ticks;
  double breakout_threshold;
  double lot_size;
  int stop_loss_points;
  int take_profit_points;

  BreakoutParams(int lookback = 500, double threshold = 0.0002,
                 double lots = 0.1, int sl = 100, int tp = 200)
      : lookback_ticks(lookback),
        breakout_threshold(threshold),
        lot_size(lots),
        stop_loss_points(sl),
        take_profit_points(tp) {}
};

class BreakoutStrategy : public IStrategy {
 private:
  BreakoutParams* params_;
  std::deque<Tick> tick_history_;

  void FindHighLow(double& high, double& low) const {
    if (tick_history_.empty()) {
      high = low = 0.0;
      return;
    }

    high = tick_history_[0].ask;
    low = tick_history_[0].bid;

    for (const auto& tick : tick_history_) {
      if (tick.ask > high) high = tick.ask;
      if (tick.bid < low) low = tick.bid;
    }
  }

 public:
  BreakoutStrategy(BreakoutParams* params) : params_(params) {}

  void OnInit() override { tick_history_.clear(); }

  void OnTick(const Tick& tick, const std::vector<Bar>& bars,
              Position& position, std::vector<Trade>& trades,
              const BacktestConfig& config) override {

    tick_history_.push_front(tick);

    if (tick_history_.size() > (size_t)params_->lookback_ticks) {
      tick_history_.pop_back();
    }

    if (tick_history_.size() < (size_t)params_->lookback_ticks) {
      return;
    }

    double high, low;
    FindHighLow(high, low);
    double range = high - low;

    // Close existing position on opposite breakout
    if (position.is_open) {
      if (position.is_buy && tick.bid < low) {
        position.is_open = false;
      } else if (!position.is_buy && tick.ask > high) {
        position.is_open = false;
      }
    }

    // Entry: Breakout with sufficient range
    if (!position.is_open && range > params_->breakout_threshold) {

      // Bullish breakout
      if (tick.ask > high) {
        position.is_open = true;
        position.is_buy = true;
        position.entry_time = tick.time;
        position.entry_time_msc = tick.time_msc;
        position.entry_price = tick.ask;
        position.volume = params_->lot_size;
        position.stop_loss =
            tick.ask - params_->stop_loss_points * config.point_value;
        position.take_profit =
            tick.ask + params_->take_profit_points * config.point_value;
      }
      // Bearish breakout
      else if (tick.bid < low) {
        position.is_open = true;
        position.is_buy = false;
        position.entry_time = tick.time;
        position.entry_time_msc = tick.time_msc;
        position.entry_price = tick.bid;
        position.volume = params_->lot_size;
        position.stop_loss =
            tick.bid + params_->stop_loss_points * config.point_value;
        position.take_profit =
            tick.bid - params_->take_profit_points * config.point_value;
      }
    }
  }

  void OnBar(const std::vector<Bar>& bars, int current_index,
             Position& position, std::vector<Trade>& trades,
             const BacktestConfig& config) override {}

  IStrategy* Clone() const override {
    return new BreakoutStrategy(params_);
  }
};

// ==================== Main Program ====================

// ==================== Parameter Parsing ====================

struct CLIParams {
  std::string data_file;
  double survive = 1.0;      // Max drawdown tolerance
  double size = 1.0;         // Position sizing multiplier
  double spacing = 1.0;      // Grid spacing in points
  bool json_output = false;
  
  static CLIParams Parse(int argc, char* argv[]) {
    CLIParams params;
    
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      
      if (arg.find("--data=") == 0) {
        params.data_file = arg.substr(7);
      } else if (arg.find("--survive=") == 0) {
        params.survive = std::stod(arg.substr(10));
      } else if (arg.find("--size=") == 0) {
        params.size = std::stod(arg.substr(7));
      } else if (arg.find("--spacing=") == 0) {
        params.spacing = std::stod(arg.substr(10));
      } else if (arg == "--json") {
        params.json_output = true;
      }
    }
    
    return params;
  }
};

// ==================== JSON Output Helper ====================

void PrintResultAsJSON(const BacktestResult& result, const CLIParams& params) {
  std::cout << "{" << std::endl;
  std::cout << "  \"parameters\": {" << std::endl;
  std::cout << "    \"survive\": " << params.survive << "," << std::endl;
  std::cout << "    \"size\": " << params.size << "," << std::endl;
  std::cout << "    \"spacing\": " << params.spacing << std::endl;
  std::cout << "  }," << std::endl;
  
  double initial_balance = result.final_balance - (result.final_balance - result.final_equity);
  if (initial_balance <= 0) initial_balance = 10000.0;  // Default initial balance
  
  std::cout << "  \"metrics\": {" << std::endl;
  std::cout << "    \"profit_loss\": " << (result.final_balance - initial_balance) << "," << std::endl;
  std::cout << "    \"return_percent\": " << ((result.final_balance - initial_balance) / initial_balance * 100) << "," << std::endl;
  std::cout << "    \"total_trades\": " << result.total_trades << "," << std::endl;
  std::cout << "    \"winning_trades\": " << result.winning_trades << "," << std::endl;
  std::cout << "    \"losing_trades\": " << result.losing_trades << "," << std::endl;
  std::cout << "    \"win_rate\": " << result.win_rate << "," << std::endl;
  std::cout << "    \"profit_factor\": " << result.profit_factor << "," << std::endl;
  std::cout << "    \"sharpe_ratio\": " << result.sharpe_ratio << "," << std::endl;
  std::cout << "    \"max_drawdown\": " << result.max_drawdown << "," << std::endl;
  std::cout << "    \"max_drawdown_pct\": " << result.max_drawdown_percent << "," << std::endl;
  std::cout << "    \"avg_win\": " << result.avg_win << "," << std::endl;
  std::cout << "    \"avg_loss\": " << result.avg_loss << "," << std::endl;
  std::cout << "    \"expectancy\": " << result.expectancy << "," << std::endl;
  std::cout << "    \"total_commission\": " << result.total_commission << "," << std::endl;
  std::cout << "    \"total_swap\": " << result.total_swap << std::endl;
  std::cout << "  }" << std::endl;
  std::cout << "}" << std::endl;
}

int main(int argc, char* argv[]) {
  CLIParams cli_params = CLIParams::Parse(argc, argv);
  
  if (!cli_params.json_output) {
    std::cout << "=== C++ Backtesting Engine - Parameter Sweep Mode ===" << std::endl
              << std::endl;
  }

  // ==================== Example 1: Bar-Based Backtest ====================

  if (!cli_params.json_output) {
    std::cout << "\n=== Example 1: Bar-Based MA Crossover ===" << std::endl;
  }

  BacktestConfig bar_config;
  bar_config.mode = BacktestMode::BAR_BY_BAR;
  bar_config.initial_balance = 10000.0;
  bar_config.commission_per_lot = 7.0;
  bar_config.spread_points = 10;

  BacktestEngine bar_engine(bar_config);

  // Create sample bars for demonstration
  std::vector<Bar> bars;
  for (int i = 0; i < 500; ++i) {
    Bar bar(1609459200 + i * 3600,
            1.1000 + i * 0.0001,     // open
            1.1000 + i * 0.0001 + 0.0005,  // high
            1.1000 + i * 0.0001 - 0.0003,  // low
            1.1000 + i * 0.0001 + 0.0002,  // close
            1000 + rand() % 500,            // volume
            100 + rand() % 50);
    bars.push_back(bar);
  }

  if (!cli_params.json_output) {
    std::cout << "Created " << bars.size() << " sample bars for testing"
              << std::endl;
  }

  bar_engine.LoadBars(bars);

  // Use CLI parameters for strategy - scale MA parameters based on survive/size/spacing
  int fast_period = static_cast<int>(20 * cli_params.size);
  int slow_period = static_cast<int>(50 * cli_params.size);
  double lot_size = 0.1 * cli_params.size;
  
  MACrossoverParams ma_params(fast_period, slow_period, lot_size, 
                               500 * cli_params.survive, 1000 * cli_params.survive);
  MACrossoverStrategy ma_strategy(&ma_params);

  BacktestResult bar_result = bar_engine.RunBacktest(&ma_strategy, &ma_params);
  
  if (cli_params.json_output) {
    PrintResultAsJSON(bar_result, cli_params);
  } else {
    Reporter::PrintResult(bar_result);
    Reporter::SaveTradesToCSV("bar_trades.csv", bar_result.trades);
  }

  return 0;
}

/* ================================================================================
   METATRADER INTEGRATION EXAMPLES (Commented - Requires MT Installation)
   ================================================================================ */

/*
// Example: Load historical data from MetaTrader 4/5
void ExampleMetaTraderHistoryLoad() {
  using namespace mt;
  
  std::cout << "\n=== MetaTrader History Data Loading ===" << std::endl;
  
  // Auto-detect MT4/MT5 installations
  auto installations = MetaTraderDetector::DetectInstallations();
  std::cout << "Found " << installations.size() << " MT installations" << std::endl;
  
  for (const auto& path : installations) {
    std::cout << "  - " << path << std::endl;
  }
  
  // Load historical OHLC data from MT
  MTHistoryLoader loader;
  
  try {
    // Load from MT4 history (binary HST format)
    auto ticks = loader.LoadTicksFromHST(
        MetaTraderDetector::GetMT4HistoryPath() + "/eurusd_240.hst",
        1000000000,  // Start time
        2000000000   // End time
    );
    
    std::cout << "Loaded " << ticks.size() << " ticks from MT4" << std::endl;
    
  } catch (const std::exception& e) {
    std::cout << "Error loading MT history: " << e.what() << std::endl;
  }
  
  // Load trade history (Orders export)
  try {
    auto history = loader.LoadTradeHistory("orders_history.csv");
    auto stats = mt::MTHistoryParser::CalculateStats(history);
    
    std::cout << "\n=== Trade History Statistics ===" << std::endl;
    std::cout << "Total Trades: " << stats.total_trades << std::endl;
    std::cout << "Win Rate: " << stats.win_rate << "%" << std::endl;
    std::cout << "Profit Factor: " << stats.profit_factor << std::endl;
    std::cout << "Gross Profit: $" << stats.gross_profit << std::endl;
    std::cout << "Gross Loss: $" << stats.gross_loss << std::endl;
    
  } catch (const std::exception& e) {
    std::cout << "Error loading trade history: " << e.what() << std::endl;
  }
}

// Example: Connect to MetaTrader broker and download live data
void ExampleMetaTraderBrokerConnection() {
  using namespace mt;
  
  std::cout << "\n=== MetaTrader Broker Connection ===" << std::endl;
  
  // Configure connection to MT broker
  MTConfig config = MTConfig::Demo("ICMarkets");
  config.server = "icmarkets-mt4.com";
  config.login = "12345678";
  config.password = "mypassword";
  
  auto connection = std::make_shared<MTConnection>(config);
  
  // Connect to broker
  if (!connection->Connect()) {
    std::cout << "Failed to connect to broker" << std::endl;
    return;
  }
  
  if (!connection->Authenticate()) {
    std::cout << "Authentication failed" << std::endl;
    return;
  }
  
  std::cout << "Connected to " << config.broker_name << std::endl;
  
  // Get account info
  auto account = connection->GetAccountInfo();
  std::cout << "\n=== Account Info ===" << std::endl;
  std::cout << "Account: " << account.account_number << std::endl;
  std::cout << "Balance: $" << account.balance << std::endl;
  std::cout << "Equity: $" << account.equity << std::endl;
  std::cout << "Free Margin: $" << account.margin_free << std::endl;
  std::cout << "Margin Level: " << account.margin_level << "%" << std::endl;
  
  // List available symbols
  auto symbols = connection->GetSymbolList();
  std::cout << "\n=== Available Symbols (" << symbols.size() << ") ===" << std::endl;
  for (size_t i = 0; i < std::min(size_t(10), symbols.size()); ++i) {
    const auto& sym = symbols[i];
    std::cout << sym.name << " - Spread: " << sym.spread << " pips" << std::endl;
  }
  
  // Request historical bars
  auto bars = connection->GetBars("EURUSD", MTTimeframe::H1, 100);
  std::cout << "\nReceived " << bars.size() << " hourly bars for EURUSD" << std::endl;
  
  // Create data feed
  MTDataFeed feed(connection);
  feed.CacheHistoricalData("EURUSD", MTTimeframe::H4, 0, 2000000000);
  
  auto cached_bars = feed.GetCachedBars("EURUSD");
  std::cout << "Cached " << cached_bars.size() << " H4 bars" << std::endl;
  
  // Generate synthetic ticks from bars
  auto symbol_info = connection->GetSymbolInfo("EURUSD");
  auto synthetic_ticks = feed.GenerateTicksFromBars(cached_bars, symbol_info, 100);
  std::cout << "Generated " << synthetic_ticks.size() << " synthetic ticks" << std::endl;
  
  // Send a test order
  int ticket = connection->SendOrder(
      "EURUSD",
      MTOrderType::BUY,
      0.1,           // Volume (0.1 lots = 10,000 units)
      symbol_info.ask,
      3,             // Slippage
      symbol_info.ask - 50 * symbol_info.point,  // SL at 50 pips
      symbol_info.ask + 100 * symbol_info.point  // TP at 100 pips
  );
  
  std::cout << "\nOrder sent with ticket: " << ticket << std::endl;
  
  // Close connection
  connection->Disconnect();
  std::cout << "Disconnected from broker" << std::endl;
}

// Example: Backtest using MetaTrader historical data
void ExampleMetaTraderBacktest() {
  using namespace mt;
  using namespace backtest;
  
  std::cout << "\n=== MetaTrader Historical Backtest ===" << std::endl;
  
  MTHistoryLoader loader;
  
  try {
    // Load historical data from MT4
    auto bars = loader.LoadBarsFromCSV(
        "EURUSD_H1.csv",
        "EURUSD",
        1500000000,
        2000000000
    );
    
    std::cout << "Loaded " << bars.size() << " bars from MT4" << std::endl;
    
    // Convert to backtest format
    std::vector<Bar> backtest_bars;
    for (const auto& mt_bar : bars) {
      Bar b;
      b.time = mt_bar.time;
      b.open = mt_bar.open;
      b.high = mt_bar.high;
      b.low = mt_bar.low;
      b.close = mt_bar.close;
      b.volume = mt_bar.volume;
      backtest_bars.push_back(b);
    }
    
    // Run backtest
    BacktestConfig config;
    config.mode = BacktestMode::BAR_BY_BAR;
    config.initial_balance = 10000.0;
    config.leverage = 50.0;
    config.enable_slippage = true;
    config.slippage_pips = 1.0;
    
    BacktestEngine engine(config);
    engine.LoadBars(backtest_bars);
    
    // Use MA Crossover strategy with MT data
    MACrossoverParams params(12, 26, 0.1, 50, 100);
    MACrossoverStrategy strategy(&params);
    
    BacktestResult result = engine.RunBacktest(&strategy);
    
    std::cout << "\n=== Backtest Results (MT4 Data) ===" << std::endl;
    Reporter::PrintResult(result);
    
  } catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << std::endl;
  }
}

*/

// Uncomment the functions below to enable MetaTrader integration examples:
// ExampleMetaTraderHistoryLoad();
// ExampleMetaTraderBrokerConnection();
// ExampleMetaTraderBacktest();
