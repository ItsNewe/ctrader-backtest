import { TrendingUp, TrendingDown, BarChart3, Shield, Activity, DollarSign } from 'lucide-react';

interface BacktestMetrics {
  initial_balance: number;
  final_balance: number;
  total_pnl: number;
  return_percent: number;
  total_trades: number;
  winning_trades: number;
  losing_trades: number;
  win_rate: number;
  max_drawdown_pct: number;
  sharpe_ratio: number;
  sortino_ratio: number;
  profit_factor: number;
  recovery_factor: number;
  average_win: number;
  average_loss: number;
  largest_win: number;
  largest_loss: number;
  total_swap: number;
  peak_equity: number;
  max_open_positions: number;
  stop_out_occurred: boolean;
}

export function ResultsSummary({ metrics }: { metrics: BacktestMetrics }) {
  const isProfit = metrics.total_pnl >= 0;

  return (
    <div className="space-y-3">
      {/* Primary metrics row */}
      <div className="grid grid-cols-6 gap-2">
        <MetricCard
          label="Final Balance"
          value={`$${metrics.final_balance.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`}
          color={isProfit ? 'var(--color-success)' : 'var(--color-danger)'}
          icon={DollarSign}
        />
        <MetricCard
          label="Return"
          value={`${metrics.return_percent >= 0 ? '+' : ''}${metrics.return_percent.toFixed(1)}%`}
          subtitle={`${metrics.return_percent >= 0 ? '+' : ''}$${metrics.total_pnl.toFixed(2)}`}
          color={isProfit ? 'var(--color-success)' : 'var(--color-danger)'}
          icon={isProfit ? TrendingUp : TrendingDown}
        />
        <MetricCard
          label="Win Rate"
          value={`${metrics.win_rate.toFixed(1)}%`}
          subtitle={`${metrics.winning_trades}W / ${metrics.losing_trades}L`}
          color={metrics.win_rate >= 50 ? 'var(--color-success)' : 'var(--color-warning)'}
          icon={BarChart3}
        />
        <MetricCard
          label="Max Drawdown"
          value={`${metrics.max_drawdown_pct.toFixed(1)}%`}
          color={metrics.max_drawdown_pct < 30 ? 'var(--color-success)' : metrics.max_drawdown_pct < 60 ? 'var(--color-warning)' : 'var(--color-danger)'}
          icon={Shield}
        />
        <MetricCard
          label="Sharpe"
          value={metrics.sharpe_ratio.toFixed(2)}
          color={metrics.sharpe_ratio > 1 ? 'var(--color-success)' : 'var(--color-text-secondary)'}
          icon={Activity}
        />
        <MetricCard
          label="Profit Factor"
          value={metrics.profit_factor.toFixed(2)}
          color={metrics.profit_factor > 1 ? 'var(--color-success)' : 'var(--color-danger)'}
          icon={TrendingUp}
        />
      </div>

      {/* Secondary metrics */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-3">
        <div className="grid grid-cols-4 gap-x-6 gap-y-2 text-xs">
          <StatRow label="Total Trades" value={metrics.total_trades.toLocaleString()} />
          <StatRow label="Avg Win" value={`$${metrics.average_win.toFixed(2)}`} color="var(--color-success)" />
          <StatRow label="Avg Loss" value={`$${metrics.average_loss.toFixed(2)}`} color="var(--color-danger)" />
          <StatRow label="Sortino" value={metrics.sortino_ratio.toFixed(2)} />
          <StatRow label="Recovery Factor" value={metrics.recovery_factor.toFixed(2)} />
          <StatRow label="Largest Win" value={`$${metrics.largest_win.toFixed(2)}`} color="var(--color-success)" />
          <StatRow label="Largest Loss" value={`$${metrics.largest_loss.toFixed(2)}`} color="var(--color-danger)" />
          <StatRow label="Total Swap" value={`$${metrics.total_swap.toFixed(2)}`} />
          <StatRow label="Peak Equity" value={`$${metrics.peak_equity.toFixed(2)}`} />
          <StatRow label="Max Positions" value={String(metrics.max_open_positions)} />
          <StatRow
            label="Stop Out"
            value={metrics.stop_out_occurred ? 'YES' : 'No'}
            color={metrics.stop_out_occurred ? 'var(--color-danger)' : 'var(--color-success)'}
          />
        </div>
      </div>
    </div>
  );
}

function MetricCard({
  label,
  value,
  subtitle,
  color,
  icon: Icon,
}: {
  label: string;
  value: string;
  subtitle?: string;
  color: string;
  icon: React.FC<{ size?: number }>;
}) {
  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-2.5">
      <div className="flex items-center gap-1.5 mb-1">
        <Icon size={12} style={{ color }} />
        <span className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)]">{label}</span>
      </div>
      <div className="text-sm font-semibold font-mono" style={{ color }}>{value}</div>
      {subtitle && (
        <div className="text-[10px] text-[var(--color-text-muted)] mt-0.5">{subtitle}</div>
      )}
    </div>
  );
}

function StatRow({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div className="flex justify-between items-center">
      <span className="text-[var(--color-text-muted)]">{label}</span>
      <span className="font-mono" style={color ? { color } : { color: 'var(--color-text-primary)' }}>{value}</span>
    </div>
  );
}
