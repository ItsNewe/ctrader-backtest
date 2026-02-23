export interface ParameterRange {
  name: string;
  label: string;
  min: number;
  max: number;
  step: number;
}

export interface SweepConfig {
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
  sweep_type: 'grid' | 'random';
  num_combinations: number;
  parameter_ranges: { name: string; min: number; max: number; step: number }[];
}

export interface SweepProgress {
  sweep_id: string;
  status: 'running' | 'completed' | 'error' | 'cancelled';
  completed: number;
  total: number;
  percent: number;
  current_result: SweepResultEntry | null;
  best_so_far: SweepResultEntry | null;
  message: string | null;
}

export interface SweepResultEntry {
  parameters: Record<string, number>;
  final_balance: number;
  return_percent: number;
  sharpe_ratio: number;
  max_drawdown: number;
  profit_factor: number;
  win_rate: number;
  total_trades: number;
  sortino_ratio: number;
  recovery_factor: number;
  max_open_positions: number;
  stop_out: boolean;
}
