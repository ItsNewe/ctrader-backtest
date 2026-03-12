import { useEffect, useState } from 'react';
import { Activity, Database, Cpu, TrendingUp, Clock, Play, Grid3x3 } from 'lucide-react';
import { apiGet } from '../api/client';
import { useBroker } from '../hooks/useBroker';
import type { DataFile } from '../types/backtest';

interface RecentEntry {
  id: number;
  timestamp: number;
  strategy: string;
  symbol: string;
  return_percent: number;
  sharpe_ratio: number;
  max_drawdown_pct: number;
}

export function Dashboard() {
  const { connected, ctraderConfigured, activeSymbol, specs } = useBroker();
  const [health, setHealth] = useState<{ backtest_cli_exists: boolean; data_dir_exists: boolean } | null>(null);
  const [dataFiles, setDataFiles] = useState<DataFile[]>([]);
  const [recentEntries, setRecentEntries] = useState<RecentEntry[]>([]);

  useEffect(() => {
    apiGet<{ status: string } & Record<string, boolean>>('/api/health')
      .then(setHealth)
      .catch(() => {});

    apiGet<{ status: string; files: DataFile[] }>('/api/data/files')
      .then((res) => {
        if (res.status === 'success') setDataFiles(res.files);
      })
      .catch(() => {});

    apiGet<{ status: string; entries: RecentEntry[] }>('/api/backtest/history?limit=5')
      .then((res) => {
        if (res.status === 'ok') setRecentEntries(res.entries);
      })
      .catch(() => {});
  }, []);

  const spec = activeSymbol ? specs[activeSymbol] : null;

  const formatDate = (ts: number) => {
    const d = new Date(ts * 1000);
    return d.toLocaleDateString() + ' ' + d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  };

  return (
    <div className="space-y-4">
      <h1 className="text-lg font-semibold text-[var(--color-text-primary)]">Dashboard</h1>

      {/* Status cards */}
      <div className="grid grid-cols-4 gap-3">
        <StatusCard
          icon={Activity}
          label="Broker"
          value={connected ? 'MT5 Connected' : ctraderConfigured ? 'cTrader' : 'Disconnected'}
          color={connected || ctraderConfigured ? 'var(--color-success)' : 'var(--color-danger)'}
        />
        <StatusCard
          icon={Cpu}
          label="Backtest CLI"
          value={health?.backtest_cli_exists ? 'Ready' : 'Not built'}
          color={health?.backtest_cli_exists ? 'var(--color-success)' : 'var(--color-warning)'}
        />
        <StatusCard
          icon={Database}
          label="Tick Data Files"
          value={`${dataFiles.length} available`}
          color="var(--color-accent)"
        />
        <StatusCard
          icon={TrendingUp}
          label="Active Symbol"
          value={activeSymbol || 'None selected'}
          color="var(--color-text-primary)"
        />
      </div>

      {/* Two-column layout */}
      <div className="grid grid-cols-2 gap-4">
        {/* Recent Backtests */}
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
          <div className="flex items-center gap-2 px-4 py-2 border-b border-[var(--color-border)]">
            <Clock size={12} className="text-[var(--color-text-muted)]" />
            <span className="text-xs font-medium text-[var(--color-text-primary)]">Recent Backtests</span>
          </div>
          {recentEntries.length === 0 ? (
            <div className="p-4 text-xs text-[var(--color-text-muted)] text-center">
              No backtests run yet. Go to the Backtest page to start.
            </div>
          ) : (
            <div className="divide-y divide-[var(--color-border)]/50">
              {recentEntries.map((entry) => {
                const isProfit = entry.return_percent >= 0;
                return (
                  <div key={entry.id} className="flex items-center justify-between px-4 py-2 text-xs hover:bg-[var(--color-bg-tertiary)]/50">
                    <div>
                      <span className="text-[var(--color-text-primary)] font-medium">{entry.strategy}</span>
                      <span className="text-[var(--color-text-muted)] ml-2">{entry.symbol}</span>
                    </div>
                    <div className="flex items-center gap-3">
                      <span className="font-mono" style={{ color: isProfit ? 'var(--color-success)' : 'var(--color-danger)' }}>
                        {isProfit ? '+' : ''}{(entry.return_percent ?? 0).toFixed(1)}%
                      </span>
                      <span className="text-[var(--color-text-muted)] font-mono">S:{(entry.sharpe_ratio ?? 0).toFixed(1)}</span>
                      <span className="text-[var(--color-text-muted)] font-mono">DD:{(entry.max_drawdown_pct ?? 0).toFixed(0)}%</span>
                      <span className="text-[var(--color-text-muted)] text-[10px]">{formatDate(entry.timestamp)}</span>
                    </div>
                  </div>
                );
              })}
            </div>
          )}
        </div>

        {/* Quick Actions / Instrument Details */}
        <div className="space-y-4">
          {/* Quick actions */}
          <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
            <div className="text-xs font-medium text-[var(--color-text-primary)] mb-3">Quick Actions</div>
            <div className="grid grid-cols-2 gap-2">
              <QuickAction icon={Play} label="Run Backtest" page="backtest" />
              <QuickAction icon={Grid3x3} label="Parameter Sweep" page="sweep" />
            </div>
          </div>

          {/* Instrument details */}
          {spec && (
            <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
              <h2 className="text-xs font-medium mb-2 text-[var(--color-text-primary)]">
                {activeSymbol} Properties
              </h2>
              <div className="grid grid-cols-4 gap-2 text-xs">
                <PropItem label="Contract" value={spec.contract_size} />
                <PropItem label="Pip Size" value={spec.pip_size} />
                <PropItem label="Swap L" value={spec.swap_buy} />
                <PropItem label="Swap S" value={spec.swap_sell} />
              </div>
            </div>
          )}
        </div>
      </div>

      {/* Data files */}
      {dataFiles.length > 0 && (
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
          <h2 className="text-xs font-medium mb-2 text-[var(--color-text-primary)]">
            Available Tick Data ({dataFiles.length} files)
          </h2>
          <div className="grid grid-cols-3 gap-1">
            {dataFiles.slice(0, 12).map((f) => (
              <div
                key={f.name}
                className="flex items-center justify-between py-1 px-2 rounded bg-[var(--color-bg-tertiary)] text-xs"
              >
                <span className="font-mono text-[var(--color-text-secondary)] truncate">{f.symbol || f.name}</span>
                <span className="text-[var(--color-text-muted)] text-[10px] shrink-0 ml-2">{(f.size_mb ?? 0).toFixed(0)} MB</span>
              </div>
            ))}
            {dataFiles.length > 12 && (
              <div className="flex items-center justify-center py-1 px-2 text-[10px] text-[var(--color-text-muted)]">
                +{dataFiles.length - 12} more
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}

function StatusCard({
  icon: Icon,
  label,
  value,
  color,
}: {
  icon: React.FC<{ size?: number }>;
  label: string;
  value: string;
  color: string;
}) {
  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-3">
      <div className="flex items-center gap-2 mb-1">
        <Icon size={14} style={{ color }} />
        <span className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)]">
          {label}
        </span>
      </div>
      <span className="text-sm font-medium" style={{ color }}>
        {value}
      </span>
    </div>
  );
}

function QuickAction({ icon: Icon, label, page }: { icon: React.FC<{ size?: number }>; label: string; page: string }) {
  return (
    <button
      onClick={() => {
        // Navigate by dispatching to parent (quick hack — in a real app use router)
        const event = new CustomEvent('navigate', { detail: page });
        window.dispatchEvent(event);
      }}
      className="flex items-center gap-2 px-3 py-2 rounded-lg bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] text-xs text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)] hover:border-[var(--color-accent)]/50 transition-colors"
    >
      <Icon size={14} />
      {label}
    </button>
  );
}

function PropItem({ label, value }: { label: string; value: number }) {
  return (
    <div>
      <div className="text-[10px] text-[var(--color-text-muted)]">{label}</div>
      <div className="font-mono text-[var(--color-text-primary)]">{value}</div>
    </div>
  );
}
