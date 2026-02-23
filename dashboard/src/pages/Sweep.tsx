import { useEffect, useState, useMemo } from 'react';
import { Play, X, Grid3x3, Shuffle, ArrowUpDown, Trophy, AlertTriangle, ChevronDown, ChevronRight, ShieldAlert } from 'lucide-react';
import { useBacktest } from '../hooks/useBacktest';
import { useBroker } from '../hooks/useBroker';
import { useSweep } from '../hooks/useSweep';
import { Heatmap } from '../components/sweep/Heatmap';
import { apiPost } from '../api/client';
import type { SweepConfig, SweepResultEntry, ParameterRange } from '../types/sweep';

// Symbol default broker settings
const SYMBOL_DEFAULTS: Record<string, { contract_size: number; leverage: number; pip_size: number; swap_long: number; swap_short: number }> = {
  XAUUSD: { contract_size: 100, leverage: 500, pip_size: 0.01, swap_long: -66.99, swap_short: 41.2 },
  XAGUSD: { contract_size: 5000, leverage: 500, pip_size: 0.001, swap_long: -15.0, swap_short: 13.72 },
};

export function Sweep() {
  const { strategies, selectedStrategy, setSelectedStrategy, fetchStrategies } = useBacktest();
  const { activeSymbol, specs } = useBroker();
  const { progress, results, running, error, startSweep, cancelSweep, reset } = useSweep();

  // Sweep config state
  const [sweepType, setSweepType] = useState<'grid' | 'random'>('grid');
  const [numCombinations, setNumCombinations] = useState(100);
  const [startDate, setStartDate] = useState('2025.01.01');
  const [endDate, setEndDate] = useState('2025.12.30');
  const [balance, setBalance] = useState(10000);
  const [paramRanges, setParamRanges] = useState<ParameterRange[]>([]);
  const [showAdvancedRanges, setShowAdvancedRanges] = useState(false);
  const [sortBy, setSortBy] = useState('return_percent');
  const [sortAsc, setSortAsc] = useState(false);

  // Validation guardrail state
  const [validationWarnings, setValidationWarnings] = useState<string[]>([]);
  const [showConfirmDialog, setShowConfirmDialog] = useState(false);
  const [estimatedTime, setEstimatedTime] = useState<string | null>(null);

  // Fetch strategies on mount
  useEffect(() => {
    fetchStrategies();
  }, [fetchStrategies]);

  // Current strategy object
  const strategy = useMemo(
    () => strategies.find((s) => s.id === selectedStrategy) || null,
    [strategies, selectedStrategy]
  );

  // Initialize param ranges when strategy changes (only numeric params)
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

  // Calculate combination count
  const comboCount = useMemo(() => {
    if (sweepType === 'random') return numCombinations;
    if (paramRanges.length === 0) return 0;
    // Only count ranges that are enabled (min !== max with non-zero step)
    const activeRanges = paramRanges.filter((r) => r.min !== r.max && r.step > 0);
    if (activeRanges.length === 0) return 1;
    let total = 1;
    for (const r of activeRanges) {
      const steps = Math.floor((r.max - r.min) / r.step) + 1;
      total *= steps;
    }
    return total;
  }, [paramRanges, sweepType, numCombinations]);

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

  const buildConfig = (): SweepConfig => ({
    strategy: selectedStrategy || 'FillUpOscillation',
    symbol,
    start_date: startDate,
    end_date: endDate,
    initial_balance: balance,
    ...brokerSettings,
    sweep_type: sweepType,
    num_combinations: numCombinations,
    parameter_ranges: paramRanges
      .filter((r) => r.min !== r.max || sweepType === 'random')
      .map((r) => ({ name: r.name, min: r.min, max: r.max, step: r.step })),
  });

  const handleStart = async () => {
    const config = buildConfig();

    // Pre-validate
    try {
      const validation = await apiPost<{
        status: string;
        warnings: string[];
        combination_count: number;
        estimated_time?: string;
      }>('/api/analysis/sweep/validate', config);

      if (validation.warnings && validation.warnings.length > 0) {
        setValidationWarnings(validation.warnings);
        setEstimatedTime(validation.estimated_time || null);
        setShowConfirmDialog(true);
        return;
      }

      if (validation.estimated_time) {
        setEstimatedTime(validation.estimated_time);
      }
    } catch {
      // If validation endpoint doesn't exist, proceed anyway
    }

    startSweep(config);
  };

  const handleConfirmStart = () => {
    setShowConfirmDialog(false);
    setValidationWarnings([]);
    startSweep(buildConfig());
  };

  const updateRange = (idx: number, field: keyof ParameterRange, value: number | string) => {
    setParamRanges((prev) => {
      const updated = [...prev];
      updated[idx] = { ...updated[idx], [field]: value };
      return updated;
    });
  };

  // Sort results locally
  const sortedResults = useMemo(() => {
    const sorted = [...results];
    sorted.sort((a, b) => {
      const aVal = (a as Record<string, unknown>)[sortBy] as number ?? 0;
      const bVal = (b as Record<string, unknown>)[sortBy] as number ?? 0;
      return sortAsc ? aVal - bVal : bVal - aVal;
    });
    return sorted;
  }, [results, sortBy, sortAsc]);

  const handleSort = (col: string) => {
    if (sortBy === col) {
      setSortAsc(!sortAsc);
    } else {
      setSortBy(col);
      setSortAsc(false);
    }
  };

  return (
    <div className="space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h1 className="text-lg font-semibold text-[var(--color-text-primary)]">Parameter Sweep</h1>
        {(progress || results.length > 0) && !running && (
          <button
            onClick={reset}
            className="flex items-center gap-1 px-2 py-1 rounded text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)]"
          >
            <X size={10} /> Reset
          </button>
        )}
      </div>

      {/* Config Panel */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
        <div className="grid grid-cols-[1fr_1fr_auto] gap-4">
          {/* Left: Strategy + general settings */}
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
                <input type="text" value={startDate} onChange={(e) => setStartDate(e.target.value)} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]" />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">End</label>
                <input type="text" value={endDate} onChange={(e) => setEndDate(e.target.value)} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]" />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-2">
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Balance</label>
                <input type="number" value={balance} onChange={(e) => setBalance(parseFloat(e.target.value) || 10000)} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]" />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Sweep Type</label>
                <div className="flex gap-1">
                  <button
                    onClick={() => setSweepType('grid')}
                    disabled={running}
                    className={`flex items-center gap-1 flex-1 justify-center px-3 py-1.5 rounded text-xs font-medium transition-colors ${
                      sweepType === 'grid'
                        ? 'bg-[var(--color-accent)] text-white'
                        : 'bg-[var(--color-bg-tertiary)] text-[var(--color-text-secondary)] border border-[var(--color-border)]'
                    }`}
                  >
                    <Grid3x3 size={12} /> Grid
                  </button>
                  <button
                    onClick={() => setSweepType('random')}
                    disabled={running}
                    className={`flex items-center gap-1 flex-1 justify-center px-3 py-1.5 rounded text-xs font-medium transition-colors ${
                      sweepType === 'random'
                        ? 'bg-[var(--color-accent)] text-white'
                        : 'bg-[var(--color-bg-tertiary)] text-[var(--color-text-secondary)] border border-[var(--color-border)]'
                    }`}
                  >
                    <Shuffle size={12} /> Random
                  </button>
                </div>
              </div>
            </div>

            {sweepType === 'random' && (
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Random Combinations</label>
                <input type="number" value={numCombinations} onChange={(e) => setNumCombinations(parseInt(e.target.value) || 100)} min={1} max={10000} className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]" />
              </div>
            )}
          </div>

          {/* Right: Parameter ranges */}
          <div className="space-y-2 max-h-72 overflow-y-auto">
            <div className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)] mb-1">
              Parameter Ranges
            </div>
            {paramRanges
              .filter((r) => !r.advanced)
              .map((r, _) => {
                const idx = paramRanges.findIndex((pr) => pr.name === r.name);
                return (
                  <RangeInput key={r.name} range={r} idx={idx} running={running} updateRange={updateRange} />
                );
              })}

            {/* Advanced ranges toggle */}
            {paramRanges.some((r) => r.advanced) && (
              <>
                <button
                  onClick={() => setShowAdvancedRanges(!showAdvancedRanges)}
                  className="flex items-center gap-1 w-full pt-1 text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] transition-colors"
                >
                  {showAdvancedRanges ? <ChevronDown size={10} /> : <ChevronRight size={10} />}
                  Advanced Parameters
                </button>
                {showAdvancedRanges &&
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

          {/* Start button + combo count */}
          <div className="flex flex-col justify-between items-end">
            <div className="text-right space-y-1">
              <div className="text-[10px] text-[var(--color-text-muted)] uppercase tracking-wider">Combinations</div>
              <div className={`text-lg font-bold font-mono ${comboCount > 1000 ? 'text-[var(--color-warning)]' : 'text-[var(--color-text-primary)]'}`}>
                {comboCount.toLocaleString()}
              </div>
              {comboCount > 1000 && (
                <div className="flex items-center gap-1 text-[10px] text-[var(--color-warning)]">
                  <AlertTriangle size={10} /> May take a while
                </div>
              )}
            </div>

            <div className="flex flex-col gap-2">
              {running ? (
                <button
                  onClick={cancelSweep}
                  className="flex items-center gap-2 px-6 py-3 rounded-lg text-sm font-semibold transition-all bg-[var(--color-danger)] hover:bg-[var(--color-danger)]/80 text-white shadow-lg"
                >
                  <X size={16} />
                  Cancel
                </button>
              ) : (
                <button
                  onClick={handleStart}
                  disabled={comboCount === 0}
                  className="flex items-center gap-2 px-6 py-3 rounded-lg text-sm font-semibold transition-all bg-[var(--color-accent)] hover:bg-[var(--color-accent-hover)] text-white disabled:opacity-50 disabled:cursor-not-allowed shadow-lg shadow-[var(--color-accent)]/20"
                >
                  <Play size={16} />
                  Start Sweep
                </button>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Sweep Validation Confirmation Dialog */}
      {showConfirmDialog && (
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-warning)]/50 p-4 space-y-3">
          <div className="flex items-center gap-2 text-xs font-medium text-[var(--color-warning)]">
            <ShieldAlert size={14} />
            Sweep Validation Warnings
          </div>
          <div className="space-y-1">
            {validationWarnings.map((w, i) => (
              <div key={i} className="flex items-start gap-2 text-xs text-[var(--color-text-secondary)]">
                <AlertTriangle size={10} className="text-[var(--color-warning)] mt-0.5 shrink-0" />
                {w}
              </div>
            ))}
          </div>
          <div className="flex items-center gap-3">
            <div className="text-[10px] text-[var(--color-text-muted)]">
              {comboCount.toLocaleString()} combinations
              {estimatedTime && <> | Est. {estimatedTime}</>}
            </div>
            <div className="flex gap-2 ml-auto">
              <button
                onClick={() => setShowConfirmDialog(false)}
                className="px-3 py-1 rounded text-xs text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)] transition-colors"
              >
                Cancel
              </button>
              <button
                onClick={handleConfirmStart}
                className="flex items-center gap-1 px-3 py-1 rounded text-xs font-medium bg-[var(--color-warning)] text-black hover:bg-[var(--color-warning)]/80 transition-colors"
              >
                <Play size={12} />
                Proceed Anyway
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Error */}
      {error && (
        <div className="p-3 rounded bg-[var(--color-danger)]/10 border border-[var(--color-danger)]/30 text-xs text-[var(--color-danger)]">
          {error}
        </div>
      )}

      {/* Progress */}
      {progress && (
        <SweepProgressBar progress={progress} />
      )}

      {/* Heatmap */}
      {results.length > 0 && (
        <Heatmap results={results} />
      )}

      {/* Results Table */}
      {sortedResults.length > 0 && (
        <SweepResultsTable
          results={sortedResults}
          sortBy={sortBy}
          sortAsc={sortAsc}
          onSort={handleSort}
        />
      )}
    </div>
  );
}

// ── Progress Bar Component ────────────────────────────────

function SweepProgressBar({ progress }: { progress: import('../types/sweep').SweepProgress }) {
  const isFinished = progress.status === 'completed' || progress.status === 'cancelled' || progress.status === 'error';
  const barColor = progress.status === 'error' ? 'var(--color-danger)' :
    progress.status === 'cancelled' ? 'var(--color-warning)' :
    progress.status === 'completed' ? 'var(--color-success)' :
    'var(--color-accent)';

  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4 space-y-3">
      {/* Progress bar */}
      <div className="flex items-center justify-between text-xs text-[var(--color-text-secondary)] mb-1">
        <span className="capitalize font-medium">{progress.status}</span>
        <span className="font-mono">{progress.completed}/{progress.total} ({progress.percent.toFixed(1)}%)</span>
      </div>
      <div className="h-2 bg-[var(--color-bg-tertiary)] rounded-full overflow-hidden">
        <div
          className="h-full rounded-full transition-all duration-300"
          style={{ width: `${progress.percent}%`, backgroundColor: barColor }}
        />
      </div>

      {/* Best so far */}
      {progress.best_so_far && (
        <div className="flex items-center gap-4 pt-2 border-t border-[var(--color-border)]">
          <div className="flex items-center gap-1.5 text-xs">
            <Trophy size={12} className="text-[var(--color-warning)]" />
            <span className="text-[var(--color-text-muted)]">Best so far:</span>
          </div>
          <div className="flex gap-4 text-xs font-mono">
            <span className={progress.best_so_far.return_percent >= 0 ? 'text-[var(--color-success)]' : 'text-[var(--color-danger)]'}>
              {progress.best_so_far.return_percent >= 0 ? '+' : ''}{progress.best_so_far.return_percent.toFixed(1)}%
            </span>
            <span className="text-[var(--color-text-secondary)]">
              DD: {progress.best_so_far.max_drawdown.toFixed(1)}%
            </span>
            <span className="text-[var(--color-text-secondary)]">
              Sharpe: {progress.best_so_far.sharpe_ratio.toFixed(2)}
            </span>
            <span className="text-[var(--color-text-secondary)]">
              PF: {progress.best_so_far.profit_factor.toFixed(2)}
            </span>
          </div>
          {progress.best_so_far.parameters && (
            <div className="flex gap-2 text-[10px] text-[var(--color-text-muted)]">
              {Object.entries(progress.best_so_far.parameters).map(([k, v]) => (
                <span key={k}>{k}={typeof v === 'number' ? v.toFixed(2) : String(v)}</span>
              ))}
            </div>
          )}
        </div>
      )}

      {/* Message */}
      {progress.message && isFinished && (
        <div className="text-xs text-[var(--color-text-muted)]">{progress.message}</div>
      )}
    </div>
  );
}

// ── Results Table Component ────────────────────────────────

const COLUMNS = [
  { key: 'return_percent', label: 'Return %', format: (v: number) => `${(v ?? 0) >= 0 ? '+' : ''}${(v ?? 0).toFixed(1)}%` },
  { key: 'final_balance', label: 'Balance', format: (v: number) => `$${(v ?? 0).toLocaleString(undefined, { maximumFractionDigits: 0 })}` },
  { key: 'sharpe_ratio', label: 'Sharpe', format: (v: number) => (v ?? 0).toFixed(2) },
  { key: 'sortino_ratio', label: 'Sortino', format: (v: number) => (v ?? 0).toFixed(2) },
  { key: 'max_drawdown', label: 'Max DD%', format: (v: number) => `${(v ?? 0).toFixed(1)}%` },
  { key: 'profit_factor', label: 'PF', format: (v: number) => (v ?? 0).toFixed(2) },
  { key: 'win_rate', label: 'Win%', format: (v: number) => `${(v ?? 0).toFixed(1)}%` },
  { key: 'total_trades', label: 'Trades', format: (v: number) => (v ?? 0).toLocaleString() },
  { key: 'recovery_factor', label: 'RF', format: (v: number) => (v ?? 0).toFixed(2) },
  { key: 'stop_out', label: 'StopOut', format: (v: boolean) => v ? 'YES' : '-' },
];

function SweepResultsTable({
  results,
  sortBy,
  sortAsc,
  onSort,
}: {
  results: SweepResultEntry[];
  sortBy: string;
  sortAsc: boolean;
  onSort: (col: string) => void;
}) {
  // Detect which parameters are present
  const paramKeys = useMemo(() => {
    if (results.length === 0) return [];
    const keys = new Set<string>();
    for (const r of results.slice(0, 10)) {
      for (const k of Object.keys(r.parameters)) keys.add(k);
    }
    return Array.from(keys);
  }, [results]);

  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
      <div className="flex items-center justify-between px-4 py-2 border-b border-[var(--color-border)]">
        <span className="text-xs font-medium text-[var(--color-text-primary)]">
          Sweep Results ({results.length})
        </span>
      </div>

      <div className="overflow-auto max-h-[60vh]">
        <table className="w-full text-xs">
          <thead className="sticky top-0 bg-[var(--color-bg-secondary)]">
            <tr className="border-b border-[var(--color-border)]">
              <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">#</th>
              {paramKeys.map((k) => (
                <th key={k} className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">
                  {k.replace(/_/g, ' ')}
                </th>
              ))}
              {COLUMNS.map((col) => (
                <th
                  key={col.key}
                  onClick={() => onSort(col.key)}
                  className="px-2 py-1.5 text-right text-[10px] text-[var(--color-text-muted)] font-medium cursor-pointer hover:text-[var(--color-text-primary)] select-none whitespace-nowrap"
                >
                  <span className="flex items-center justify-end gap-0.5">
                    {col.label}
                    {sortBy === col.key && (
                      <ArrowUpDown size={10} className="text-[var(--color-accent)]" />
                    )}
                  </span>
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {results.map((row, idx) => {
              const isProfit = row.return_percent >= 0;
              const isBest = idx === 0;
              return (
                <tr
                  key={idx}
                  className={`border-b border-[var(--color-border)]/50 hover:bg-[var(--color-bg-tertiary)]/50 ${
                    isBest ? 'bg-[var(--color-accent)]/5' : ''
                  }`}
                >
                  <td className="px-2 py-1 text-[var(--color-text-muted)] font-mono">{idx + 1}</td>
                  {paramKeys.map((k) => (
                    <td key={k} className="px-2 py-1 font-mono text-[var(--color-text-primary)]">
                      {typeof row.parameters[k] === 'number'
                        ? Number(row.parameters[k]).toFixed(2)
                        : String(row.parameters[k] ?? '-')}
                    </td>
                  ))}
                  {COLUMNS.map((col) => {
                    const val = (row as Record<string, unknown>)[col.key];
                    let color = 'var(--color-text-primary)';
                    if (col.key === 'return_percent') color = isProfit ? 'var(--color-success)' : 'var(--color-danger)';
                    if (col.key === 'stop_out' && val) color = 'var(--color-danger)';
                    if (col.key === 'max_drawdown' && (val as number) > 60) color = 'var(--color-danger)';

                    return (
                      <td key={col.key} className="px-2 py-1 text-right font-mono whitespace-nowrap" style={{ color }}>
                        {col.format(val as never)}
                      </td>
                    );
                  })}
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
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
