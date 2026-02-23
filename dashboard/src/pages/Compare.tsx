import { useEffect, useState, useMemo, useRef, useCallback } from 'react';
import { GitCompare, Check, X, Loader2 } from 'lucide-react';
import { apiGet } from '../api/client';
import { createChart, LineSeries, ColorType } from 'lightweight-charts';
import type { IChartApi, ISeriesApi, UTCTimestamp } from 'lightweight-charts';

interface HistoryEntry {
  id: number;
  timestamp: number;
  strategy: string;
  symbol: string;
  start_date: string;
  end_date: string;
  initial_balance: number;
  final_balance: number;
  return_percent: number;
  total_trades: number;
  win_rate: number;
  sharpe_ratio: number;
  sortino_ratio: number;
  max_drawdown_pct: number;
  profit_factor: number;
  recovery_factor: number;
  max_open_positions: number;
  stop_out_occurred: boolean;
  strategy_params: Record<string, unknown> | null;
}

interface HistoryDetail {
  id: number;
  equity_curve: number[];
  equity_timestamps: string[];
  initial_balance: number;
}

const COMPARE_COLORS = ['#2962FF', '#FF6D00', '#00C853', '#D500F9', '#FF1744'];

export function Compare() {
  const [entries, setEntries] = useState<HistoryEntry[]>([]);
  const [selected, setSelected] = useState<Set<number>>(new Set());
  const [curves, setCurves] = useState<Map<number, HistoryDetail>>(new Map());
  const [loading, setLoading] = useState(false);
  const chartContainerRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<IChartApi | null>(null);
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const seriesRef = useRef<Map<number, any>>(new Map());

  // Fetch history list
  useEffect(() => {
    apiGet<{ status: string; entries: HistoryEntry[]; total: number }>(
      '/api/backtest/history?limit=100&sort_by=timestamp&ascending=false'
    ).then((res) => {
      if (res.status === 'ok') setEntries(res.entries);
    }).catch(console.error);
  }, []);

  // Create chart
  useEffect(() => {
    if (!chartContainerRef.current) return;

    const chart = createChart(chartContainerRef.current, {
      layout: {
        background: { type: ColorType.Solid, color: '#131722' },
        textColor: '#787b86',
        fontSize: 11,
      },
      grid: {
        vertLines: { color: '#1e222d' },
        horzLines: { color: '#1e222d' },
      },
      crosshair: {
        vertLine: { color: '#363a45', width: 1, style: 3 },
        horzLine: { color: '#363a45', width: 1, style: 3 },
      },
      rightPriceScale: {
        borderColor: '#2a2e39',
      },
      timeScale: {
        borderColor: '#2a2e39',
        timeVisible: true,
      },
      width: chartContainerRef.current.clientWidth,
      height: 350,
    });

    chartRef.current = chart;

    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        chart.applyOptions({ width: entry.contentRect.width });
      }
    });
    observer.observe(chartContainerRef.current);

    return () => {
      observer.disconnect();
      chart.remove();
      chartRef.current = null;
      seriesRef.current.clear();
    };
  }, []);

  // Fetch equity curves when selection changes
  useEffect(() => {
    const toFetch = Array.from(selected).filter((id) => !curves.has(id));
    if (toFetch.length === 0) return;

    setLoading(true);
    Promise.all(
      toFetch.map((id) =>
        apiGet<{ status: string; entry: HistoryDetail }>(`/api/backtest/history/${id}`)
          .then((res) => (res.status === 'ok' ? res.entry : null))
      )
    ).then((results) => {
      setCurves((prev) => {
        const next = new Map(prev);
        for (const r of results) {
          if (r) next.set(r.id, r);
        }
        return next;
      });
      setLoading(false);
    });
  }, [selected]);

  // Update chart series when curves or selection changes
  useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;

    // Remove series for deselected entries
    for (const [id, series] of seriesRef.current) {
      if (!selected.has(id)) {
        chart.removeSeries(series);
        seriesRef.current.delete(id);
      }
    }

    // Add/update series for selected entries
    let colorIdx = 0;
    for (const id of selected) {
      const detail = curves.get(id);
      if (!detail || !detail.equity_curve || detail.equity_curve.length === 0) continue;

      const color = COMPARE_COLORS[colorIdx % COMPARE_COLORS.length];
      colorIdx++;

      if (!seriesRef.current.has(id)) {
        const series = chart.addSeries(LineSeries, {
          color,
          lineWidth: 2,
          priceLineVisible: false,
          lastValueVisible: true,
          crosshairMarkerVisible: true,
        });

        // Parse timestamps and build data
        const data = detail.equity_timestamps.map((ts, i) => {
          const parts = ts.split(/[.\s:]/);
          const d = new Date(
            parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]),
            parseInt(parts[3] || '0'), parseInt(parts[4] || '0'), parseInt(parts[5] || '0')
          );
          return {
            time: Math.floor(d.getTime() / 1000) as UTCTimestamp,
            value: detail.equity_curve[i],
          };
        }).filter((d) => !isNaN(d.time) && !isNaN(d.value));

        if (data.length > 0) {
          series.setData(data);
          seriesRef.current.set(id, series);
        }
      }
    }

    chart.timeScale().fitContent();
  }, [selected, curves]);

  const toggleSelect = (id: number) => {
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(id)) {
        next.delete(id);
      } else if (next.size < 5) {
        next.add(id);
      }
      return next;
    });
  };

  const clearAll = () => {
    setSelected(new Set());
  };

  const formatDate = (ts: number) => {
    const d = new Date(ts * 1000);
    return d.toLocaleDateString();
  };

  // Build comparison table for selected entries
  const selectedEntries = useMemo(
    () => entries.filter((e) => selected.has(e.id)),
    [entries, selected]
  );

  return (
    <div className="space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h1 className="text-lg font-semibold text-[var(--color-text-primary)] flex items-center gap-2">
          <GitCompare size={18} /> Compare Backtests
        </h1>
        {selected.size > 0 && (
          <button
            onClick={clearAll}
            className="flex items-center gap-1 px-2 py-1 rounded text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)]"
          >
            <X size={10} /> Clear Selection
          </button>
        )}
      </div>

      {/* Selection list */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
        <div className="px-4 py-2 border-b border-[var(--color-border)] text-xs text-[var(--color-text-muted)]">
          Select 2-5 backtests to compare ({selected.size} selected)
        </div>
        <div className="max-h-48 overflow-y-auto">
          {entries.length === 0 ? (
            <div className="p-4 text-xs text-[var(--color-text-muted)] text-center">
              No backtest history. Run backtests first.
            </div>
          ) : (
            entries.map((entry, idx) => {
              const isSelected = selected.has(entry.id);
              const colorIdx = isSelected ? Array.from(selected).indexOf(entry.id) : -1;
              const color = colorIdx >= 0 ? COMPARE_COLORS[colorIdx % COMPARE_COLORS.length] : undefined;
              const isProfit = entry.return_percent >= 0;

              return (
                <button
                  key={entry.id}
                  onClick={() => toggleSelect(entry.id)}
                  className={`w-full flex items-center gap-3 px-4 py-1.5 text-xs transition-colors hover:bg-[var(--color-bg-tertiary)]/50 ${
                    isSelected ? 'bg-[var(--color-bg-tertiary)]/30' : ''
                  }`}
                >
                  {/* Checkbox */}
                  <div
                    className={`w-4 h-4 rounded border flex items-center justify-center shrink-0 ${
                      isSelected ? 'border-transparent' : 'border-[var(--color-border)]'
                    }`}
                    style={isSelected ? { backgroundColor: color } : {}}
                  >
                    {isSelected && <Check size={10} className="text-white" />}
                  </div>

                  {/* Info */}
                  <span className="text-[var(--color-text-muted)] w-20 shrink-0">{formatDate(entry.timestamp)}</span>
                  <span className="text-[var(--color-text-primary)] font-medium w-32 shrink-0 text-left">{entry.strategy}</span>
                  <span className="text-[var(--color-text-primary)] font-mono w-16 shrink-0">{entry.symbol}</span>
                  <span className="text-[var(--color-text-muted)] w-36 shrink-0">{entry.start_date} - {entry.end_date}</span>
                  <span className="font-mono ml-auto" style={{ color: isProfit ? 'var(--color-success)' : 'var(--color-danger)' }}>
                    {isProfit ? '+' : ''}{entry.return_percent.toFixed(1)}%
                  </span>
                  <span className="text-[var(--color-text-muted)] font-mono w-16 text-right">S: {entry.sharpe_ratio.toFixed(2)}</span>
                  <span className="text-[var(--color-text-muted)] font-mono w-16 text-right">DD: {entry.max_drawdown_pct.toFixed(1)}%</span>
                </button>
              );
            })
          )}
        </div>
      </div>

      {/* Overlaid equity chart */}
      {selected.size >= 2 && (
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
          <div className="flex items-center justify-between px-4 py-2 border-b border-[var(--color-border)]">
            <span className="text-xs font-medium text-[var(--color-text-primary)]">Equity Curves Overlay</span>
            {loading && <Loader2 size={12} className="animate-spin text-[var(--color-accent)]" />}
            {/* Legend */}
            <div className="flex items-center gap-3">
              {selectedEntries.map((e, i) => (
                <div key={e.id} className="flex items-center gap-1 text-[10px]">
                  <div className="w-3 h-0.5 rounded" style={{ backgroundColor: COMPARE_COLORS[i % COMPARE_COLORS.length] }} />
                  <span className="text-[var(--color-text-muted)]">{e.strategy} ({e.symbol})</span>
                </div>
              ))}
            </div>
          </div>
          <div ref={chartContainerRef} className="w-full" />
        </div>
      )}

      {/* Side-by-side metrics comparison table */}
      {selectedEntries.length >= 2 && (
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
          <div className="px-4 py-2 border-b border-[var(--color-border)]">
            <span className="text-xs font-medium text-[var(--color-text-primary)]">Metrics Comparison</span>
          </div>
          <div className="overflow-auto">
            <table className="w-full text-xs">
              <thead>
                <tr className="border-b border-[var(--color-border)]">
                  <th className="px-3 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">Metric</th>
                  {selectedEntries.map((e, i) => (
                    <th key={e.id} className="px-3 py-1.5 text-right text-[10px] font-medium" style={{ color: COMPARE_COLORS[i % COMPARE_COLORS.length] }}>
                      {e.strategy}
                      <div className="text-[var(--color-text-muted)] font-normal">{e.symbol} | {e.start_date}</div>
                    </th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {COMPARE_METRICS.map((m) => (
                  <CompareRow key={m.key} metric={m} entries={selectedEntries} />
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}

      {/* Statistical Analysis */}
      {selectedEntries.length >= 2 && (
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
          <div className="px-4 py-2 border-b border-[var(--color-border)]">
            <span className="text-xs font-medium text-[var(--color-text-primary)]">Statistical Analysis</span>
          </div>
          <div className="p-4 space-y-3">
            {/* Pairwise comparisons */}
            {selectedEntries.map((a, i) =>
              selectedEntries.slice(i + 1).map((b, j) => {
                const retDiff = a.return_percent - b.return_percent;
                const sharpeDiff = a.sharpe_ratio - b.sharpe_ratio;
                const ddDiff = a.max_drawdown_pct - b.max_drawdown_pct;
                const pfDiff = a.profit_factor - b.profit_factor;
                // Calmar = annualized return / max DD
                const calmarA = a.max_drawdown_pct > 0 ? a.return_percent / a.max_drawdown_pct : 0;
                const calmarB = b.max_drawdown_pct > 0 ? b.return_percent / b.max_drawdown_pct : 0;
                const calmarDiff = calmarA - calmarB;
                // Risk-adjusted: Sharpe comparison
                const betterOverall = sharpeDiff > 0 ? a : b;
                const colorA = COMPARE_COLORS[i % COMPARE_COLORS.length];
                const colorB = COMPARE_COLORS[(i + j + 1) % COMPARE_COLORS.length];

                return (
                  <div key={`${a.id}-${b.id}`} className="border border-[var(--color-border)] rounded-lg p-3">
                    <div className="flex items-center gap-2 mb-2 text-xs">
                      <span className="font-medium" style={{ color: colorA }}>{a.strategy}</span>
                      <span className="text-[var(--color-text-muted)]">vs</span>
                      <span className="font-medium" style={{ color: colorB }}>{b.strategy}</span>
                    </div>
                    <div className="grid grid-cols-5 gap-2 text-xs">
                      <StatDiffCell label="Return" diff={retDiff} format={(v) => `${v >= 0 ? '+' : ''}${v.toFixed(1)}%`} higherBetter />
                      <StatDiffCell label="Sharpe" diff={sharpeDiff} format={(v) => `${v >= 0 ? '+' : ''}${v.toFixed(2)}`} higherBetter />
                      <StatDiffCell label="Max DD" diff={ddDiff} format={(v) => `${v >= 0 ? '+' : ''}${v.toFixed(1)}%`} higherBetter={false} />
                      <StatDiffCell label="Profit Factor" diff={pfDiff} format={(v) => `${v >= 0 ? '+' : ''}${v.toFixed(2)}`} higherBetter />
                      <StatDiffCell label="Calmar Ratio" diff={calmarDiff} format={(v) => `${v >= 0 ? '+' : ''}${v.toFixed(2)}`} higherBetter />
                    </div>
                    <div className="mt-2 text-[10px] text-[var(--color-text-muted)]">
                      Risk-adjusted winner:{' '}
                      <span className="text-[var(--color-accent)] font-medium">
                        {betterOverall.strategy} ({betterOverall.symbol})
                      </span>
                    </div>
                  </div>
                );
              })
            )}
          </div>
        </div>
      )}

      {selected.size === 1 && (
        <div className="text-xs text-[var(--color-text-muted)] text-center py-4">
          Select at least 2 backtests to compare
        </div>
      )}
    </div>
  );
}

// ── Comparison metrics ────────────────────────────────

interface MetricDef {
  key: string;
  label: string;
  format: (v: number | boolean) => string;
  higherBetter?: boolean;
}

const COMPARE_METRICS: MetricDef[] = [
  { key: 'return_percent', label: 'Return %', format: (v) => `${(v as number) >= 0 ? '+' : ''}${(v as number).toFixed(1)}%`, higherBetter: true },
  { key: 'final_balance', label: 'Final Balance', format: (v) => `$${(v as number).toLocaleString(undefined, { maximumFractionDigits: 0 })}`, higherBetter: true },
  { key: 'sharpe_ratio', label: 'Sharpe Ratio', format: (v) => (v as number).toFixed(2), higherBetter: true },
  { key: 'sortino_ratio', label: 'Sortino Ratio', format: (v) => (v as number).toFixed(2), higherBetter: true },
  { key: 'max_drawdown_pct', label: 'Max Drawdown %', format: (v) => `${(v as number).toFixed(1)}%`, higherBetter: false },
  { key: 'profit_factor', label: 'Profit Factor', format: (v) => (v as number).toFixed(2), higherBetter: true },
  { key: 'recovery_factor', label: 'Recovery Factor', format: (v) => (v as number).toFixed(2), higherBetter: true },
  { key: 'win_rate', label: 'Win Rate %', format: (v) => `${(v as number).toFixed(1)}%`, higherBetter: true },
  { key: 'total_trades', label: 'Total Trades', format: (v) => (v as number).toLocaleString() },
  { key: 'max_open_positions', label: 'Max Positions', format: (v) => String(v) },
  { key: 'stop_out_occurred', label: 'Stop Out', format: (v) => v ? 'YES' : 'No' },
];

function StatDiffCell({
  label,
  diff,
  format,
  higherBetter,
}: {
  label: string;
  diff: number;
  format: (v: number) => string;
  higherBetter: boolean;
}) {
  const isGood = higherBetter ? diff > 0 : diff < 0;
  const isNeutral = Math.abs(diff) < 0.001;
  return (
    <div className="bg-[var(--color-bg-tertiary)] rounded px-2 py-1.5">
      <div className="text-[10px] text-[var(--color-text-muted)]">{label}</div>
      <div className="flex items-center gap-1 mt-0.5">
        {!isNeutral && (
          <span
            className="text-[10px]"
            style={{ color: isGood ? 'var(--color-success)' : 'var(--color-danger)' }}
          >
            {isGood ? '\u2191' : '\u2193'}
          </span>
        )}
        <span
          className="text-xs font-mono"
          style={{
            color: isNeutral ? 'var(--color-text-muted)' : isGood ? 'var(--color-success)' : 'var(--color-danger)',
          }}
        >
          {format(diff)}
        </span>
      </div>
    </div>
  );
}

function CompareRow({ metric, entries }: { metric: MetricDef; entries: HistoryEntry[] }) {
  const values = entries.map((e) => (e as Record<string, unknown>)[metric.key] as number);

  // Find best value
  let bestIdx = -1;
  if (metric.higherBetter !== undefined && values.length > 0) {
    bestIdx = metric.higherBetter
      ? values.indexOf(Math.max(...values))
      : values.indexOf(Math.min(...values));
  }

  return (
    <tr className="border-b border-[var(--color-border)]/50">
      <td className="px-3 py-1.5 text-[var(--color-text-muted)]">{metric.label}</td>
      {values.map((v, i) => (
        <td
          key={i}
          className={`px-3 py-1.5 text-right font-mono ${
            i === bestIdx ? 'font-semibold' : ''
          }`}
          style={{
            color: i === bestIdx
              ? 'var(--color-accent)'
              : metric.key === 'stop_out_occurred' && v
              ? 'var(--color-danger)'
              : 'var(--color-text-primary)',
          }}
        >
          {metric.format(v)}
        </td>
      ))}
    </tr>
  );
}
