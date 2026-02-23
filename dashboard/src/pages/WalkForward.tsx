import { useEffect, useState, useMemo } from 'react';
import { Play, Loader2, X, ChevronDown, ChevronRight, BarChart3 } from 'lucide-react';
import { useBacktest } from '../hooks/useBacktest';
import { useBroker } from '../hooks/useBroker';
import { apiPost, apiGet } from '../api/client';
import type { ParameterRange } from '../types/sweep';

// Symbol default broker settings
const SYMBOL_DEFAULTS: Record<string, { contract_size: number; leverage: number; pip_size: number; swap_long: number; swap_short: number }> = {
  XAUUSD: { contract_size: 100, leverage: 500, pip_size: 0.01, swap_long: -66.99, swap_short: 41.2 },
  XAGUSD: { contract_size: 5000, leverage: 500, pip_size: 0.001, swap_long: -15.0, swap_short: 13.72 },
};

interface WFWindow {
  window_index: number;
  is_start: string;
  is_end: string;
  oos_start: string;
  oos_end: string;
  best_params: Record<string, number>;
  is_score: number;
  oos_return: number;
  oos_sharpe: number;
  oos_max_dd: number;
}

interface WFResult {
  status: string;
  wf_id: string;
  windows: WFWindow[];
  summary: {
    avg_oos_return: number;
    oos_win_rate: number;
    best_oos_return: number;
    worst_oos_return: number;
    total_windows: number;
  };
  progress?: number;
  message?: string;
}

export function WalkForward() {
  const { strategies, selectedStrategy, setSelectedStrategy, fetchStrategies } = useBacktest();
  const { activeSymbol, specs } = useBroker();

  // Config state
  const [startDate, setStartDate] = useState('2025.01.01');
  const [endDate, setEndDate] = useState('2025.12.30');
  const [inSampleMonths, setInSampleMonths] = useState(3);
  const [outOfSampleMonths, setOutOfSampleMonths] = useState(1);
  const [optimMetric, setOptimMetric] = useState('return_percent');
  const [balance, setBalance] = useState(10000);
  const [paramRanges, setParamRanges] = useState<ParameterRange[]>([]);
  const [showAdvanced, setShowAdvanced] = useState(false);

  // Run state
  const [running, setRunning] = useState(false);
  const [wfId, setWfId] = useState<string | null>(null);
  const [result, setResult] = useState<WFResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [progress, setProgress] = useState<number>(0);

  // Fetch strategies on mount
  useEffect(() => {
    fetchStrategies();
  }, [fetchStrategies]);

  // Current strategy
  const strategy = useMemo(
    () => strategies.find((s) => s.id === selectedStrategy) || null,
    [strategies, selectedStrategy]
  );

  // Initialize param ranges when strategy changes
  useEffect(() => {
    if (!strategy) return;
    const ranges: ParameterRange[] = [];
    for (const p of strategy.parameters) {
      if ((p.type === 'float' || p.type === 'int') && p.min !== undefined && p.max !== undefined) {
        ranges.push({
          name: p.name,
          label: p.label,
          min: p.min ?? (p.default as number) * 0.5,
          max: p.max ?? (p.default as number) * 2,
          step: p.step ?? 1,
          advanced: p.advanced,
        });
      }
    }
    setParamRanges(ranges);
  }, [strategy]);

  // Broker settings
  const symbol = activeSymbol || 'XAUUSD';
  const spec = specs[symbol];
  const brokerDefaults = SYMBOL_DEFAULTS[symbol] || SYMBOL_DEFAULTS.XAUUSD;
  const brokerSettings = {
    contract_size: spec?.contract_size ?? brokerDefaults.contract_size,
    leverage: brokerDefaults.leverage,
    pip_size: spec?.pip_size ?? brokerDefaults.pip_size,
    swap_long: spec?.swap_buy ?? brokerDefaults.swap_long,
    swap_short: spec?.swap_sell ?? brokerDefaults.swap_short,
  };

  const updateRange = (idx: number, field: 'min' | 'max' | 'step', value: number) => {
    setParamRanges((prev) => {
      const updated = [...prev];
      updated[idx] = { ...updated[idx], [field]: value };
      return updated;
    });
  };

  // Poll for results
  useEffect(() => {
    if (!wfId || !running) return;
    const interval = setInterval(async () => {
      try {
        const res = await apiGet<WFResult>(`/api/analysis/walkforward/${wfId}`);
        if (res.status === 'completed') {
          setResult(res);
          setRunning(false);
          setProgress(100);
          clearInterval(interval);
        } else if (res.status === 'error') {
          setError(res.message || 'Walk-forward analysis failed');
          setRunning(false);
          clearInterval(interval);
        } else {
          setProgress(res.progress || 0);
        }
      } catch (e) {
        setError(String(e));
        setRunning(false);
        clearInterval(interval);
      }
    }, 2000);
    return () => clearInterval(interval);
  }, [wfId, running]);

  const handleStart = async () => {
    setRunning(true);
    setError(null);
    setResult(null);
    setProgress(0);

    const config = {
      strategy: selectedStrategy || 'FillUpOscillation',
      symbol,
      start_date: startDate,
      end_date: endDate,
      initial_balance: balance,
      ...brokerSettings,
      in_sample_months: inSampleMonths,
      out_of_sample_months: outOfSampleMonths,
      optimization_metric: optimMetric,
      parameter_ranges: paramRanges
        .filter((r) => r.min !== r.max)
        .map((r) => ({ name: r.name, min: r.min, max: r.max, step: r.step })),
    };

    try {
      const res = await apiPost<{ status: string; wf_id: string; message?: string }>(
        '/api/analysis/walkforward/start',
        config
      );
      if (res.status === 'error') {
        setError(res.message || 'Failed to start walk-forward');
        setRunning(false);
      } else {
        setWfId(res.wf_id);
      }
    } catch (e) {
      setError(String(e));
      setRunning(false);
    }
  };

  const handleReset = () => {
    setResult(null);
    setError(null);
    setWfId(null);
    setProgress(0);
  };

  // OOS bar chart max for scaling
  const maxAbsOos = useMemo(() => {
    if (!result?.windows) return 1;
    return Math.max(...result.windows.map((w) => Math.abs(w.oos_return)), 1);
  }, [result]);

  return (
    <div className="space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h1 className="text-lg font-semibold text-[var(--color-text-primary)] flex items-center gap-2">
          <BarChart3 size={18} /> Walk-Forward Analysis
        </h1>
        {result && (
          <button
            onClick={handleReset}
            className="flex items-center gap-1 px-2 py-1 rounded text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)]"
          >
            <X size={10} /> Reset
          </button>
        )}
      </div>

      {/* Config Panel */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
        <div className="grid grid-cols-[1fr_1fr_auto] gap-4">
          {/* Left: General settings */}
          <div className="space-y-3">
            <div>
              <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Strategy</label>
              <select
                value={selectedStrategy || ''}
                onChange={(e) => setSelectedStrategy(e.target.value)}
                disabled={running}
                className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
              >
                {strategies.map((s) => (
                  <option key={s.id} value={s.id}>{s.name}</option>
                ))}
              </select>
            </div>

            <div className="grid grid-cols-3 gap-2">
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Symbol</label>
                <input type="text" value={symbol} readOnly className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono" />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Start</label>
                <input type="text" value={startDate} onChange={(e) => setStartDate(e.target.value)} disabled={running} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50" />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">End</label>
                <input type="text" value={endDate} onChange={(e) => setEndDate(e.target.value)} disabled={running} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50" />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-2">
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">In-Sample Months</label>
                <input type="number" value={inSampleMonths} onChange={(e) => setInSampleMonths(parseInt(e.target.value) || 3)} min={1} max={24} disabled={running} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50" />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Out-of-Sample Months</label>
                <input type="number" value={outOfSampleMonths} onChange={(e) => setOutOfSampleMonths(parseInt(e.target.value) || 1)} min={1} max={12} disabled={running} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50" />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-2">
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Balance</label>
                <input type="number" value={balance} onChange={(e) => setBalance(parseFloat(e.target.value) || 10000)} disabled={running} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50" />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Optimization Metric</label>
                <select
                  value={optimMetric}
                  onChange={(e) => setOptimMetric(e.target.value)}
                  disabled={running}
                  className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
                >
                  <option value="return_percent">Return %</option>
                  <option value="sharpe_ratio">Sharpe Ratio</option>
                  <option value="profit_factor">Profit Factor</option>
                  <option value="sortino_ratio">Sortino Ratio</option>
                </select>
              </div>
            </div>

            <div className="text-[10px] text-[var(--color-text-muted)]">
              Broker: CS {brokerSettings.contract_size} | Lev {brokerSettings.leverage} | Swap {brokerSettings.swap_long}/{brokerSettings.swap_short}
            </div>
          </div>

          {/* Right: Parameter ranges */}
          <div className="space-y-2 max-h-72 overflow-y-auto">
            <div className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)] mb-1">
              Parameter Ranges
            </div>
            {paramRanges
              .filter((r) => !r.advanced)
              .map((r) => {
                const idx = paramRanges.findIndex((pr) => pr.name === r.name);
                return (
                  <RangeInput key={r.name} range={r} idx={idx} running={running} updateRange={updateRange} />
                );
              })}

            {paramRanges.some((r) => r.advanced) && (
              <>
                <button
                  onClick={() => setShowAdvanced(!showAdvanced)}
                  className="flex items-center gap-1 w-full pt-1 text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] transition-colors"
                >
                  {showAdvanced ? <ChevronDown size={10} /> : <ChevronRight size={10} />}
                  Advanced Parameters
                </button>
                {showAdvanced &&
                  paramRanges
                    .filter((r) => r.advanced)
                    .map((r) => {
                      const idx = paramRanges.findIndex((pr) => pr.name === r.name);
                      return (
                        <RangeInput key={r.name} range={r} idx={idx} running={running} updateRange={updateRange} />
                      );
                    })}
              </>
            )}

            {paramRanges.length === 0 && (
              <div className="text-xs text-[var(--color-text-muted)] py-4 text-center">
                Select a strategy with numeric parameters
              </div>
            )}
          </div>

          {/* Start button */}
          <div className="flex flex-col justify-end">
            <button
              onClick={handleStart}
              disabled={running || paramRanges.filter((r) => r.min !== r.max).length === 0}
              className="flex items-center gap-2 px-6 py-3 rounded-lg text-sm font-semibold transition-all bg-[var(--color-accent)] hover:bg-[var(--color-accent-hover)] text-white disabled:opacity-50 disabled:cursor-not-allowed shadow-lg shadow-[var(--color-accent)]/20"
            >
              {running ? (
                <>
                  <Loader2 size={16} className="animate-spin" />
                  Running...
                </>
              ) : (
                <>
                  <Play size={16} />
                  Start WF
                </>
              )}
            </button>
          </div>
        </div>
      </div>

      {/* Error */}
      {error && (
        <div className="p-3 rounded bg-[var(--color-danger)]/10 border border-[var(--color-danger)]/30 text-xs text-[var(--color-danger)]">
          {error}
        </div>
      )}

      {/* Progress */}
      {running && (
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
          <div className="flex items-center justify-between text-xs text-[var(--color-text-secondary)] mb-2">
            <span className="font-medium">Analyzing...</span>
            <span className="font-mono">{progress.toFixed(1)}%</span>
          </div>
          <div className="h-2 bg-[var(--color-bg-tertiary)] rounded-full overflow-hidden">
            <div
              className="h-full rounded-full transition-all duration-300 bg-[var(--color-accent)]"
              style={{ width: `${progress}%` }}
            />
          </div>
        </div>
      )}

      {/* Results */}
      {result && (
        <>
          {/* Summary cards */}
          <div className="grid grid-cols-4 gap-3">
            <SummaryCard label="Avg OOS Return" value={`${result.summary.avg_oos_return >= 0 ? '+' : ''}${result.summary.avg_oos_return.toFixed(1)}%`} color={result.summary.avg_oos_return >= 0 ? 'var(--color-success)' : 'var(--color-danger)'} />
            <SummaryCard label="OOS Win Rate" value={`${result.summary.oos_win_rate.toFixed(0)}%`} color={result.summary.oos_win_rate >= 50 ? 'var(--color-success)' : 'var(--color-warning)'} />
            <SummaryCard label="Best OOS" value={`${result.summary.best_oos_return >= 0 ? '+' : ''}${result.summary.best_oos_return.toFixed(1)}%`} color="var(--color-success)" />
            <SummaryCard label="Worst OOS" value={`${result.summary.worst_oos_return >= 0 ? '+' : ''}${result.summary.worst_oos_return.toFixed(1)}%`} color="var(--color-danger)" />
          </div>

          {/* OOS Returns bar chart */}
          <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
            <div className="text-xs font-medium text-[var(--color-text-primary)] mb-3">OOS Returns by Window</div>
            <div className="flex items-end gap-1 h-32">
              {result.windows.map((w, i) => {
                const height = Math.abs(w.oos_return) / maxAbsOos * 100;
                const isPositive = w.oos_return >= 0;
                return (
                  <div key={i} className="flex-1 flex flex-col items-center justify-end h-full relative group">
                    <div
                      className="w-full rounded-t transition-all"
                      style={{
                        height: `${Math.max(height, 2)}%`,
                        backgroundColor: isPositive ? 'var(--color-success)' : 'var(--color-danger)',
                        opacity: 0.8,
                        ...(isPositive ? {} : { position: 'absolute', bottom: 0, borderRadius: '0 0 4px 4px', transform: 'none' }),
                      }}
                    />
                    {/* Tooltip */}
                    <div className="absolute bottom-full mb-1 hidden group-hover:block bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded px-2 py-1 text-[10px] text-[var(--color-text-primary)] whitespace-nowrap z-10 font-mono">
                      W{w.window_index + 1}: {isPositive ? '+' : ''}{w.oos_return.toFixed(1)}%
                    </div>
                    <div className="text-[9px] text-[var(--color-text-muted)] mt-1">W{w.window_index + 1}</div>
                  </div>
                );
              })}
            </div>
          </div>

          {/* Windows table */}
          <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
            <div className="px-4 py-2 border-b border-[var(--color-border)]">
              <span className="text-xs font-medium text-[var(--color-text-primary)]">
                Walk-Forward Windows ({result.windows.length})
              </span>
            </div>
            <div className="overflow-auto max-h-[50vh]">
              <table className="w-full text-xs">
                <thead className="sticky top-0 bg-[var(--color-bg-secondary)]">
                  <tr className="border-b border-[var(--color-border)]">
                    <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">#</th>
                    <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">IS Period</th>
                    <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">OOS Period</th>
                    <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">Best IS Params</th>
                    <th className="px-2 py-1.5 text-right text-[10px] text-[var(--color-text-muted)] font-medium">IS Score</th>
                    <th className="px-2 py-1.5 text-right text-[10px] text-[var(--color-text-muted)] font-medium">OOS Return</th>
                    <th className="px-2 py-1.5 text-right text-[10px] text-[var(--color-text-muted)] font-medium">OOS Sharpe</th>
                    <th className="px-2 py-1.5 text-right text-[10px] text-[var(--color-text-muted)] font-medium">OOS DD</th>
                  </tr>
                </thead>
                <tbody>
                  {result.windows.map((w) => {
                    const isPositive = w.oos_return >= 0;
                    return (
                      <tr key={w.window_index} className="border-b border-[var(--color-border)]/50 hover:bg-[var(--color-bg-tertiary)]/50">
                        <td className="px-2 py-1 text-[var(--color-text-muted)] font-mono">{w.window_index + 1}</td>
                        <td className="px-2 py-1 text-[var(--color-text-secondary)] whitespace-nowrap">{w.is_start} - {w.is_end}</td>
                        <td className="px-2 py-1 text-[var(--color-text-secondary)] whitespace-nowrap">{w.oos_start} - {w.oos_end}</td>
                        <td className="px-2 py-1">
                          <div className="flex flex-wrap gap-1">
                            {Object.entries(w.best_params).map(([k, v]) => (
                              <span key={k} className="px-1 py-0.5 bg-[var(--color-bg-tertiary)] rounded text-[9px] font-mono text-[var(--color-text-muted)]">
                                {k}={typeof v === 'number' ? v.toFixed(2) : String(v)}
                              </span>
                            ))}
                          </div>
                        </td>
                        <td className="px-2 py-1 text-right font-mono text-[var(--color-text-primary)]">{w.is_score.toFixed(2)}</td>
                        <td className="px-2 py-1 text-right font-mono" style={{ color: isPositive ? 'var(--color-success)' : 'var(--color-danger)' }}>
                          {isPositive ? '+' : ''}{w.oos_return.toFixed(1)}%
                        </td>
                        <td className="px-2 py-1 text-right font-mono text-[var(--color-text-primary)]">{w.oos_sharpe.toFixed(2)}</td>
                        <td className="px-2 py-1 text-right font-mono" style={{ color: w.oos_max_dd > 50 ? 'var(--color-danger)' : 'var(--color-text-primary)' }}>
                          {w.oos_max_dd.toFixed(1)}%
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          </div>
        </>
      )}
    </div>
  );
}

// ── Subcomponents ────────────────────────────────

function SummaryCard({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-3">
      <div className="text-[10px] text-[var(--color-text-muted)] uppercase tracking-wider">{label}</div>
      <div className="text-lg font-bold font-mono mt-1" style={{ color }}>{value}</div>
    </div>
  );
}

function RangeInput({
  range,
  idx,
  running,
  updateRange,
}: {
  range: ParameterRange;
  idx: number;
  running: boolean;
  updateRange: (idx: number, field: 'min' | 'max' | 'step', value: number) => void;
}) {
  return (
    <div className="space-y-1">
      <label className="block text-[10px] text-[var(--color-text-muted)]" title={range.name}>
        {range.label}
      </label>
      <div className="grid grid-cols-3 gap-1">
        <input
          type="number"
          value={range.min}
          onChange={(e) => updateRange(idx, 'min', parseFloat(e.target.value) || 0)}
          disabled={running}
          className="w-full px-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
          placeholder="Min"
          title="Min"
        />
        <input
          type="number"
          value={range.max}
          onChange={(e) => updateRange(idx, 'max', parseFloat(e.target.value) || 0)}
          disabled={running}
          className="w-full px-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
          placeholder="Max"
          title="Max"
        />
        <input
          type="number"
          value={range.step}
          onChange={(e) => updateRange(idx, 'step', parseFloat(e.target.value) || 0.1)}
          disabled={running}
          className="w-full px-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
          placeholder="Step"
          title="Step"
        />
      </div>
    </div>
  );
}
