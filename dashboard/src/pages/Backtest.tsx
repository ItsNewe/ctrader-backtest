import { useEffect, useState, useMemo } from 'react';
import { Play, Loader2, X, ChevronDown, ChevronRight } from 'lucide-react';
import { useBacktest } from '../hooks/useBacktest';
import { useBroker } from '../hooks/useBroker';
import { EquityChart } from '../components/charts/EquityChart';
import { ResultsSummary } from '../components/backtest/ResultsSummary';
import { TradeTable } from '../components/backtest/TradeTable';
import type { BacktestConfig } from '../types/backtest';
import type { Strategy, StrategyParameter } from '../types/strategy';

// Symbol default broker settings (used when MT5 specs unavailable)
const SYMBOL_DEFAULTS: Record<string, { contract_size: number; leverage: number; pip_size: number; swap_long: number; swap_short: number }> = {
  XAUUSD: { contract_size: 100, leverage: 500, pip_size: 0.01, swap_long: -66.99, swap_short: 41.2 },
  XAGUSD: { contract_size: 5000, leverage: 500, pip_size: 0.001, swap_long: -15.0, swap_short: 13.72 },
};

export function Backtest() {
  const { strategies, selectedStrategy, setSelectedStrategy, result, running, error, fetchStrategies, runBacktest, clearResult } = useBacktest();
  const { activeSymbol, specs } = useBroker();

  // Strategy params form state
  const [params, setParams] = useState<Record<string, number | string | boolean>>({});
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [startDate, setStartDate] = useState('2025.01.01');
  const [endDate, setEndDate] = useState('2025.12.30');
  const [balance, setBalance] = useState(10000);

  // Fetch strategies on mount
  useEffect(() => {
    fetchStrategies();
  }, [fetchStrategies]);

  // Current strategy object
  const strategy = useMemo(
    () => strategies.find((s) => s.id === selectedStrategy) || null,
    [strategies, selectedStrategy]
  );

  // Initialize params with defaults when strategy changes
  useEffect(() => {
    if (!strategy) return;
    const defaults: Record<string, number | string | boolean> = {};
    for (const p of strategy.parameters) {
      defaults[p.name] = p.default;
    }
    setParams(defaults);
  }, [strategy]);

  // Get broker settings from MT5 specs or fallback to defaults
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

  const handleRun = () => {
    const config: BacktestConfig = {
      strategy: selectedStrategy || 'FillUpOscillation',
      symbol,
      start_date: startDate,
      end_date: endDate,
      initial_balance: balance,
      ...brokerSettings,
      strategy_params: params,
    };
    runBacktest(config);
  };

  const updateParam = (name: string, value: number | string | boolean) => {
    setParams((prev) => ({ ...prev, [name]: value }));
  };

  return (
    <div className="space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h1 className="text-lg font-semibold text-[var(--color-text-primary)]">Backtest</h1>
        {result && (
          <button
            onClick={clearResult}
            className="flex items-center gap-1 px-2 py-1 rounded text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)]"
          >
            <X size={10} /> Clear Results
          </button>
        )}
      </div>

      {/* Config Panel */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
        <div className="grid grid-cols-[1fr_1fr_auto] gap-4">
          {/* Left: Strategy + general settings */}
          <div className="space-y-3">
            {/* Strategy selector */}
            <div>
              <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Strategy</label>
              <select
                value={selectedStrategy || ''}
                onChange={(e) => setSelectedStrategy(e.target.value)}
                className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
              >
                {strategies.map((s) => (
                  <option key={s.id} value={s.id}>{s.name}</option>
                ))}
              </select>
              {strategy && (
                <p className="mt-0.5 text-[10px] text-[var(--color-text-muted)]">{strategy.description}</p>
              )}
            </div>

            {/* Symbol + dates */}
            <div className="grid grid-cols-3 gap-2">
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Symbol</label>
                <input
                  type="text"
                  value={symbol}
                  readOnly
                  className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono"
                />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Start</label>
                <input
                  type="text"
                  value={startDate}
                  onChange={(e) => setStartDate(e.target.value)}
                  placeholder="2025.01.01"
                  className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
                />
              </div>
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">End</label>
                <input
                  type="text"
                  value={endDate}
                  onChange={(e) => setEndDate(e.target.value)}
                  placeholder="2025.12.30"
                  className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
                />
              </div>
            </div>

            {/* Balance + broker summary */}
            <div className="grid grid-cols-2 gap-2">
              <div>
                <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Balance</label>
                <input
                  type="number"
                  value={balance}
                  onChange={(e) => setBalance(parseFloat(e.target.value) || 10000)}
                  className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
                />
              </div>
              <div className="flex items-end">
                <div className="text-[10px] text-[var(--color-text-muted)] space-y-0.5">
                  <div>CS: {brokerSettings.contract_size} | Lev: {brokerSettings.leverage}</div>
                  <div>Swap: {brokerSettings.swap_long}/{brokerSettings.swap_short}</div>
                </div>
              </div>
            </div>
          </div>

          {/* Right: Strategy parameters */}
          <div className="space-y-2 max-h-72 overflow-y-auto">
            <div className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)] mb-1">
              Strategy Parameters
            </div>
            {strategy?.parameters
              .filter((p) => !p.advanced)
              .map((p) => (
                <ParameterInput
                  key={p.name}
                  param={p}
                  value={params[p.name]}
                  onChange={(v) => updateParam(p.name, v)}
                />
              ))}

            {/* Advanced parameters toggle */}
            {strategy?.parameters.some((p) => p.advanced) && (
              <>
                <button
                  onClick={() => setShowAdvanced(!showAdvanced)}
                  className="flex items-center gap-1 w-full pt-1 text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] transition-colors"
                >
                  {showAdvanced ? <ChevronDown size={10} /> : <ChevronRight size={10} />}
                  Advanced Parameters
                </button>
                {showAdvanced &&
                  strategy.parameters
                    .filter((p) => p.advanced)
                    .map((p) => (
                      <ParameterInput
                        key={p.name}
                        param={p}
                        value={params[p.name]}
                        onChange={(v) => updateParam(p.name, v)}
                      />
                    ))}
              </>
            )}
          </div>

          {/* Run button */}
          <div className="flex flex-col justify-end">
            <button
              onClick={handleRun}
              disabled={running}
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
                  Run
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

      {/* Results */}
      {result && result.status === 'success' && (
        <>
          <ResultsSummary metrics={result} />

          <EquityChart
            equityCurve={result.equity_curve}
            timestamps={result.equity_timestamps}
            initialBalance={result.initial_balance}
          />

          {result.trades && result.trades.length > 0 && (
            <TradeTable
              trades={result.trades}
              tradesTotal={result.trades_total || result.trades.length}
              truncated={result.trades_truncated || false}
            />
          )}
        </>
      )}
    </div>
  );
}

function ParameterInput({
  param,
  value,
  onChange,
}: {
  param: StrategyParameter;
  value: number | string | boolean | undefined;
  onChange: (v: number | string | boolean) => void;
}) {
  if (param.type === 'bool') {
    return (
      <div className="flex items-center justify-between py-0.5">
        <label className="text-xs text-[var(--color-text-secondary)]" title={param.description}>
          {param.label}
        </label>
        <button
          onClick={() => onChange(!value)}
          className={`w-8 h-4 rounded-full transition-colors relative ${
            value ? 'bg-[var(--color-accent)]' : 'bg-[var(--color-bg-tertiary)]'
          }`}
        >
          <div
            className={`w-3 h-3 rounded-full bg-white absolute top-0.5 transition-transform ${
              value ? 'translate-x-4' : 'translate-x-0.5'
            }`}
          />
        </button>
      </div>
    );
  }

  if (param.type === 'select') {
    return (
      <div>
        <label className="block text-[10px] text-[var(--color-text-muted)]" title={param.description}>
          {param.label}
        </label>
        <select
          value={String(value ?? param.default)}
          onChange={(e) => onChange(e.target.value)}
          className="w-full px-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
        >
          {param.options?.map((opt) => (
            <option key={opt} value={opt}>{opt}</option>
          ))}
        </select>
      </div>
    );
  }

  // float or int
  return (
    <div>
      <label className="block text-[10px] text-[var(--color-text-muted)]" title={param.description}>
        {param.label}
      </label>
      <input
        type="number"
        value={value as number ?? param.default}
        onChange={(e) => {
          const v = parseFloat(e.target.value);
          if (!isNaN(v)) onChange(v);
        }}
        min={param.min}
        max={param.max}
        step={param.step || 0.1}
        className="w-full px-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono outline-none focus:border-[var(--color-accent)]"
      />
    </div>
  );
}
