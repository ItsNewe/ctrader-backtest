import { useEffect, useState, useRef, useCallback } from 'react';
import { Shield, Loader2 } from 'lucide-react';
import { apiGet } from '../api/client';
import { createChart, AreaSeries, LineSeries, ColorType } from 'lightweight-charts';
import type { IChartApi, UTCTimestamp } from 'lightweight-charts';

interface HistoryEntry {
  id: number;
  timestamp: number;
  strategy: string;
  symbol: string;
  start_date: string;
  end_date: string;
  return_percent: number;
}

interface RiskMetrics {
  status: string;
  calmar_ratio: number;
  ulcer_index: number;
  tail_ratio: number;
  max_consecutive_losses: number;
  max_consecutive_wins: number;
  avg_dd_duration_hours: number;
  drawdown_series: { time: string; value: number }[];
  rolling_sharpe: { time: string; value: number }[];
  monthly_returns: { month: string; return_pct: number }[];
  message?: string;
}

export function RiskDashboard() {
  const [entries, setEntries] = useState<HistoryEntry[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState<RiskMetrics | null>(null);
  const [error, setError] = useState<string | null>(null);

  const ddChartContainerRef = useRef<HTMLDivElement>(null);
  const ddChartRef = useRef<IChartApi | null>(null);
  const sharpeChartContainerRef = useRef<HTMLDivElement>(null);
  const sharpeChartRef = useRef<IChartApi | null>(null);

  // Fetch history entries
  useEffect(() => {
    apiGet<{ status: string; entries: HistoryEntry[]; total: number }>(
      '/api/backtest/history?limit=100&sort_by=timestamp&ascending=false'
    ).then((res) => {
      if (res.status === 'ok') setEntries(res.entries);
    }).catch(console.error);
  }, []);

  const formatDate = (ts: number) => {
    const d = new Date(ts * 1000);
    return d.toLocaleDateString();
  };

  // Fetch risk data when selectedId changes
  useEffect(() => {
    if (!selectedId) return;
    setLoading(true);
    setError(null);
    setResult(null);

    apiGet<RiskMetrics>(`/api/analysis/risk/${selectedId}`)
      .then((res) => {
        if (res.status === 'error') {
          setError(res.message || 'Failed to fetch risk metrics');
        } else {
          setResult(res);
        }
      })
      .catch((e) => setError(String(e)))
      .finally(() => setLoading(false));
  }, [selectedId]);

  // Parse timestamp helper
  const parseTs = useCallback((ts: string): UTCTimestamp => {
    const parts = ts.split(/[.\s:/-]/);
    const d = new Date(
      parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]),
      parseInt(parts[3] || '0'), parseInt(parts[4] || '0'), parseInt(parts[5] || '0')
    );
    return Math.floor(d.getTime() / 1000) as UTCTimestamp;
  }, []);

  // Draw drawdown chart
  useEffect(() => {
    if (!result?.drawdown_series || !ddChartContainerRef.current) return;

    // Cleanup previous
    if (ddChartRef.current) {
      ddChartRef.current.remove();
      ddChartRef.current = null;
    }

    const chart = createChart(ddChartContainerRef.current, {
      layout: {
        background: { type: ColorType.Solid, color: '#131722' },
        textColor: '#787b86',
        fontSize: 11,
      },
      grid: { vertLines: { color: '#1e222d' }, horzLines: { color: '#1e222d' } },
      crosshair: {
        vertLine: { color: '#363a45', width: 1, style: 3 },
        horzLine: { color: '#363a45', width: 1, style: 3 },
      },
      rightPriceScale: { borderColor: '#2a2e39' },
      timeScale: { borderColor: '#2a2e39', timeVisible: true },
      width: ddChartContainerRef.current.clientWidth,
      height: 200,
    });

    ddChartRef.current = chart;

    const series = chart.addSeries(AreaSeries, {
      lineColor: '#FF1744',
      topColor: '#FF174430',
      bottomColor: '#FF174405',
      lineWidth: 1,
      priceLineVisible: false,
      lastValueVisible: true,
    });

    const data = result.drawdown_series
      .map((d) => ({
        time: parseTs(d.time),
        value: -Math.abs(d.value),
      }))
      .filter((d) => !isNaN(d.time) && !isNaN(d.value));

    if (data.length > 0) {
      series.setData(data);
      chart.timeScale().fitContent();
    }

    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        chart.applyOptions({ width: entry.contentRect.width });
      }
    });
    observer.observe(ddChartContainerRef.current);

    return () => {
      observer.disconnect();
      chart.remove();
      ddChartRef.current = null;
    };
  }, [result, parseTs]);

  // Draw rolling Sharpe chart
  useEffect(() => {
    if (!result?.rolling_sharpe || !sharpeChartContainerRef.current) return;

    if (sharpeChartRef.current) {
      sharpeChartRef.current.remove();
      sharpeChartRef.current = null;
    }

    const chart = createChart(sharpeChartContainerRef.current, {
      layout: {
        background: { type: ColorType.Solid, color: '#131722' },
        textColor: '#787b86',
        fontSize: 11,
      },
      grid: { vertLines: { color: '#1e222d' }, horzLines: { color: '#1e222d' } },
      crosshair: {
        vertLine: { color: '#363a45', width: 1, style: 3 },
        horzLine: { color: '#363a45', width: 1, style: 3 },
      },
      rightPriceScale: { borderColor: '#2a2e39' },
      timeScale: { borderColor: '#2a2e39', timeVisible: true },
      width: sharpeChartContainerRef.current.clientWidth,
      height: 200,
    });

    sharpeChartRef.current = chart;

    const series = chart.addSeries(LineSeries, {
      color: '#2962FF',
      lineWidth: 2,
      priceLineVisible: false,
      lastValueVisible: true,
    });

    const data = result.rolling_sharpe
      .map((d) => ({
        time: parseTs(d.time),
        value: d.value,
      }))
      .filter((d) => !isNaN(d.time) && !isNaN(d.value));

    if (data.length > 0) {
      series.setData(data);
      chart.timeScale().fitContent();
    }

    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        chart.applyOptions({ width: entry.contentRect.width });
      }
    });
    observer.observe(sharpeChartContainerRef.current);

    return () => {
      observer.disconnect();
      chart.remove();
      sharpeChartRef.current = null;
    };
  }, [result, parseTs]);

  return (
    <div className="space-y-4">
      {/* Header */}
      <h1 className="text-lg font-semibold text-[var(--color-text-primary)] flex items-center gap-2">
        <Shield size={18} /> Risk Dashboard
      </h1>

      {/* Selector */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
        <div className="flex items-end gap-4">
          <div className="flex-1">
            <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Select Backtest</label>
            <select
              value={selectedId ?? ''}
              onChange={(e) => setSelectedId(e.target.value ? parseInt(e.target.value) : null)}
              disabled={loading}
              className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
            >
              <option value="">-- Select a backtest --</option>
              {entries.map((e) => (
                <option key={e.id} value={e.id}>
                  [{formatDate(e.timestamp)}] {e.strategy} / {e.symbol} | {e.return_percent >= 0 ? '+' : ''}{e.return_percent.toFixed(1)}%
                </option>
              ))}
            </select>
          </div>
          {loading && <Loader2 size={16} className="animate-spin text-[var(--color-accent)] mb-1" />}
        </div>
      </div>

      {/* Error */}
      {error && (
        <div className="p-3 rounded bg-[var(--color-danger)]/10 border border-[var(--color-danger)]/30 text-xs text-[var(--color-danger)]">
          {error}
        </div>
      )}

      {/* Results */}
      {result && (
        <>
          {/* Risk metric cards */}
          <div className="grid grid-cols-6 gap-3">
            <MetricCard label="Calmar Ratio" value={result.calmar_ratio.toFixed(2)} />
            <MetricCard label="Ulcer Index" value={result.ulcer_index.toFixed(2)} />
            <MetricCard label="Tail Ratio" value={result.tail_ratio.toFixed(2)} />
            <MetricCard label="Max Consec Losses" value={String(result.max_consecutive_losses)} color={result.max_consecutive_losses > 10 ? 'var(--color-danger)' : undefined} />
            <MetricCard label="Max Consec Wins" value={String(result.max_consecutive_wins)} color="var(--color-success)" />
            <MetricCard label="Avg DD Duration" value={`${result.avg_dd_duration_hours.toFixed(0)}h`} />
          </div>

          {/* Drawdown chart */}
          {result.drawdown_series && result.drawdown_series.length > 0 && (
            <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
              <div className="px-4 py-2 border-b border-[var(--color-border)]">
                <span className="text-xs font-medium text-[var(--color-text-primary)]">Drawdown Over Time</span>
              </div>
              <div ref={ddChartContainerRef} className="w-full" />
            </div>
          )}

          {/* Rolling Sharpe chart */}
          {result.rolling_sharpe && result.rolling_sharpe.length > 0 && (
            <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
              <div className="px-4 py-2 border-b border-[var(--color-border)]">
                <span className="text-xs font-medium text-[var(--color-text-primary)]">Rolling Sharpe Ratio</span>
              </div>
              <div ref={sharpeChartContainerRef} className="w-full" />
            </div>
          )}

          {/* Monthly returns bars */}
          {result.monthly_returns && result.monthly_returns.length > 0 && (
            <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
              <div className="text-xs font-medium text-[var(--color-text-primary)] mb-3">Monthly Returns</div>
              <div className="flex items-end gap-1 h-32">
                {result.monthly_returns.map((m, i) => {
                  const maxAbs = Math.max(...result.monthly_returns.map((r) => Math.abs(r.return_pct)), 1);
                  const height = (Math.abs(m.return_pct) / maxAbs) * 100;
                  const isPositive = m.return_pct >= 0;
                  return (
                    <div key={i} className="flex-1 flex flex-col items-center justify-end h-full relative group">
                      <div
                        className="w-full rounded-t"
                        style={{
                          height: `${Math.max(height, 2)}%`,
                          backgroundColor: isPositive ? 'var(--color-success)' : 'var(--color-danger)',
                          opacity: 0.7,
                        }}
                      />
                      <div className="absolute bottom-full mb-1 hidden group-hover:block bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded px-2 py-1 text-[10px] text-[var(--color-text-primary)] whitespace-nowrap z-10 font-mono">
                        {m.month}: {isPositive ? '+' : ''}{m.return_pct.toFixed(1)}%
                      </div>
                      <div className="text-[8px] text-[var(--color-text-muted)] mt-1 truncate w-full text-center">{m.month}</div>
                    </div>
                  );
                })}
              </div>
            </div>
          )}
        </>
      )}

      {!selectedId && !loading && (
        <div className="text-xs text-[var(--color-text-muted)] text-center py-8">
          Select a backtest from history to view risk analysis
        </div>
      )}
    </div>
  );
}

function MetricCard({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-3">
      <div className="text-[10px] text-[var(--color-text-muted)] uppercase tracking-wider">{label}</div>
      <div className="text-sm font-bold font-mono mt-1" style={{ color: color || 'var(--color-text-primary)' }}>{value}</div>
    </div>
  );
}
