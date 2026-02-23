export interface BacktestConfig {
  strategy: string;
  symbol: string;
  start_date: string;
  end_date: string;
  initial_balance: number;
  contract_size: number;
  leverage: number;
  pip_size: number;
  swap_long: number;
  swap_short: number;
  tick_file_path?: string;
  strategy_params: Record<string, number | string | boolean>;
  max_equity_samples?: number;
  verbose?: boolean;
}

export interface TradeRecord {
  id: number;
  direction: string;
  entry_price: number;
  exit_price: number;
  entry_time: string;
  exit_time: string;
  lot_size: number;
  profit_loss: number;
  commission: number;
  exit_reason: string;
}

export interface BacktestResult {
  status: string;
  symbol: string;
  strategy: string;
  start_date: string;
  end_date: string;

  // Account metrics
  initial_balance: number;
  final_balance: number;
  total_pnl: number;
  return_percent: number;

  // Trade stats
  total_trades: number;
  total_trades_opened: number;
  winning_trades: number;
  losing_trades: number;
  win_rate: number;

  // Trade metrics
  average_win: number;
  average_loss: number;
  largest_win: number;
  largest_loss: number;

  // Risk metrics
  max_drawdown: number;
  max_drawdown_pct: number;
  sharpe_ratio: number;
  sortino_ratio: number;
  profit_factor: number;
  recovery_factor: number;
  total_swap: number;

  // Peak metrics
  peak_equity: number;
  peak_balance: number;
  max_open_positions: number;
  max_used_margin: number;
  stop_out_occurred: boolean;

  // Equity curve
  equity_curve: number[];
  equity_timestamps: string[];
  equity_curve_sampled: boolean;
  equity_curve_original_size: number;

  // Trade list
  trades: TradeRecord[];
  trades_total: number;
  trades_truncated: boolean;

  // Broker settings used
  broker_settings?: {
    contract_size: number;
    leverage: number;
    pip_size: number;
    swap_long: number;
    swap_short: number;
  };

  // Error
  message?: string;
}

export interface DataFile {
  name: string;
  path: string;
  symbol: string;
  size_mb: number;
  modified: string;
}
