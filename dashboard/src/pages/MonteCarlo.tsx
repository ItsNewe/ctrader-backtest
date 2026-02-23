import { useEffect, useState, useRef, useCallback } from 'react';
import { Dice5, Play, Loader2 } from 'lucide-react';
import { apiGet, apiPost } from '../api/client';

interface HistoryEntry {
  id: number;
  timestamp: number;
  strategy: string;
  symbol: string;
  start_date: string;
  end_date: string;
  return_percent: number;
  sharpe_ratio: number;
  max_drawdown_pct: number;
}

interface MCResult {
  status: string;
  statistics: {
    mean_final: number;
    median_final: number;
    p5_final: number;
    p95_final: number;
    prob_of_profit: number;
    prob_of_ruin: number;
    mean_max_dd: number;
    p95_max_dd: number;
  };
  distribution: number[];
  sample_curves: number[][];
  message?: string;
}

export function MonteCarlo() {
  const [entries, setEntries] = useState<HistoryEntry[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [numSims, setNumSims] = useState(1000);
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<MCResult | null>(null);
  const [error, setError] = useState<string | null>(null);

  const histoCanvasRef = useRef<HTMLCanvasElement>(null);
  const fanCanvasRef = useRef<HTMLCanvasElement>(null);

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

  const handleRun = async () => {
    if (!selectedId) return;
    setRunning(true);
    setError(null);
    setResult(null);
    try {
      const res = await apiPost<MCResult>(
        `/api/analysis/montecarlo/${selectedId}?num_simulations=${numSims}`,
        {}
      );
      if (res.status === 'error') {
        setError(res.message || 'Monte Carlo simulation failed');
      } else {
        setResult(res);
      }
    } catch (e) {
      setError(String(e));
    } finally {
      setRunning(false);
    }
  };

  // Draw histogram
  const drawHistogram = useCallback(() => {
    if (!result?.distribution || !histoCanvasRef.current) return;
    const canvas = histoCanvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;
    const dist = result.distribution;
    if (dist.length === 0) return;

    // Build histogram bins
    const numBins = 40;
    const min = Math.min(...dist);
    const max = Math.max(...dist);
    const range = max - min;
    const binWidth = range > 0 ? range / numBins : 1;
    const bins = new Array(numBins).fill(0);
    if (range > 0) {
      for (const v of dist) {
        const idx = Math.min(Math.floor((v - min) / binWidth), numBins - 1);
        bins[idx]++;
      }
    } else {
      // All values identical — put everything in the middle bin
      bins[Math.floor(numBins / 2)] = dist.length;
    }
    const maxCount = Math.max(...bins, 1);

    const padding = { top: 10, right: 10, bottom: 30, left: 50 };
    const chartW = w - padding.left - padding.right;
    const chartH = h - padding.top - padding.bottom;
    const barW = chartW / numBins;

    // Clear
    ctx.fillStyle = '#131722';
    ctx.fillRect(0, 0, w, h);

    // Draw bars
    for (let i = 0; i < numBins; i++) {
      const barH = (bins[i] / maxCount) * chartH;
      const x = padding.left + i * barW;
      const y = padding.top + chartH - barH;
      const binMid = min + (i + 0.5) * binWidth;
      ctx.fillStyle = binMid >= (result.statistics.mean_final) ? '#00C85340' : '#FF174440';
      ctx.fillRect(x + 1, y, barW - 2, barH);
      ctx.fillStyle = binMid >= (result.statistics.mean_final) ? '#00C853' : '#FF1744';
      ctx.fillRect(x + 1, y, barW - 2, 2);
    }

    // X-axis labels
    ctx.fillStyle = '#787b86';
    ctx.font = '10px monospace';
    ctx.textAlign = 'center';
    for (let i = 0; i <= 4; i++) {
      const val = min + (i / 4) * (max - min);
      const x = padding.left + (i / 4) * chartW;
      ctx.fillText(`$${(val / 1000).toFixed(1)}k`, x, h - 8);
    }

    // Y-axis labels
    ctx.textAlign = 'right';
    for (let i = 0; i <= 3; i++) {
      const val = (i / 3) * maxCount;
      const y = padding.top + chartH - (i / 3) * chartH;
      ctx.fillText(String(Math.round(val)), padding.left - 5, y + 3);
    }

    // Mean line (only draw if there's a range)
    if (range > 0) {
      const meanX = padding.left + ((result.statistics.mean_final - min) / range) * chartW;
      ctx.strokeStyle = '#2962FF';
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(meanX, padding.top);
      ctx.lineTo(meanX, padding.top + chartH);
      ctx.stroke();
      ctx.setLineDash([]);

      // Mean label
      ctx.fillStyle = '#2962FF';
      ctx.font = '9px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Mean', meanX, padding.top - 2);
    } else {
      // All simulations identical — show centered label
      ctx.fillStyle = '#2962FF';
      ctx.font = '11px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(`All sims: $${(min / 1000).toFixed(1)}k`, w / 2, padding.top + 15);
    }
  }, [result]);

  // Draw fan chart
  const drawFanChart = useCallback(() => {
    if (!result?.sample_curves || !fanCanvasRef.current) return;
    const canvas = fanCanvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;
    const curves = result.sample_curves.slice(0, 50);
    if (curves.length === 0) return;

    const padding = { top: 10, right: 10, bottom: 25, left: 55 };
    const chartW = w - padding.left - padding.right;
    const chartH = h - padding.top - padding.bottom;

    // Find min/max across all curves
    let allMin = Infinity;
    let allMax = -Infinity;
    for (const curve of curves) {
      for (const v of curve) {
        if (v < allMin) allMin = v;
        if (v > allMax) allMax = v;
      }
    }
    const range = allMax - allMin || (allMax * 0.1) || 1;

    // Clear
    ctx.fillStyle = '#131722';
    ctx.fillRect(0, 0, w, h);

    // Draw curves
    for (const curve of curves) {
      const points = curve.length;
      const finalVal = curve[curve.length - 1];
      const startVal = curve[0];
      const isProfit = finalVal >= startVal;

      ctx.strokeStyle = isProfit ? '#00C85320' : '#FF174420';
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let i = 0; i < points; i++) {
        const x = padding.left + (i / (points - 1)) * chartW;
        const y = padding.top + chartH - ((curve[i] - allMin) / range) * chartH;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    // Y-axis labels
    ctx.fillStyle = '#787b86';
    ctx.font = '10px monospace';
    ctx.textAlign = 'right';
    for (let i = 0; i <= 4; i++) {
      const val = allMin + (i / 4) * range;
      const y = padding.top + chartH - (i / 4) * chartH;
      ctx.fillText(`$${(val / 1000).toFixed(1)}k`, padding.left - 5, y + 3);
    }

    // X-axis
    ctx.textAlign = 'center';
    ctx.fillText('Start', padding.left, h - 5);
    ctx.fillText('End', w - padding.right, h - 5);
  }, [result]);

  // Redraw when result changes
  useEffect(() => {
    drawHistogram();
    drawFanChart();
  }, [drawHistogram, drawFanChart]);

  // Handle resize
  useEffect(() => {
    const handler = () => {
      drawHistogram();
      drawFanChart();
    };
    window.addEventListener('resize', handler);
    return () => window.removeEventListener('resize', handler);
  }, [drawHistogram, drawFanChart]);

  return (
    <div className="space-y-4">
      {/* Header */}
      <h1 className="text-lg font-semibold text-[var(--color-text-primary)] flex items-center gap-2">
        <Dice5 size={18} /> Monte Carlo Simulation
      </h1>

      {/* Config */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
        <div className="grid grid-cols-[1fr_auto_auto] gap-4 items-end">
          {/* Backtest selector */}
          <div>
            <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Select Backtest</label>
            <select
              value={selectedId ?? ''}
              onChange={(e) => setSelectedId(e.target.value ? parseInt(e.target.value) : null)}
              disabled={running}
              className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
            >
              <option value="">-- Select --</option>
              {entries.map((e) => (
                <option key={e.id} value={e.id}>
                  [{formatDate(e.timestamp)}] {e.strategy} / {e.symbol} | {e.return_percent >= 0 ? '+' : ''}{e.return_percent.toFixed(1)}%
                </option>
              ))}
            </select>
          </div>

          {/* Num simulations */}
          <div>
            <label className="block text-xs text-[var(--color-text-secondary)] mb-1">Simulations</label>
            <input
              type="number"
              value={numSims}
              onChange={(e) => setNumSims(parseInt(e.target.value) || 1000)}
              min={100}
              max={10000}
              disabled={running}
              className="w-32 px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono outline-none focus:border-[var(--color-accent)] disabled:opacity-50"
            />
          </div>

          {/* Run button */}
          <button
            onClick={handleRun}
            disabled={running || !selectedId}
            className="flex items-center gap-2 px-5 py-1.5 rounded-lg text-xs font-semibold transition-all bg-[var(--color-accent)] hover:bg-[var(--color-accent-hover)] text-white disabled:opacity-50 disabled:cursor-not-allowed shadow-lg shadow-[var(--color-accent)]/20"
          >
            {running ? <Loader2 size={14} className="animate-spin" /> : <Play size={14} />}
            {running ? 'Running...' : 'Run Simulation'}
          </button>
        </div>
      </div>

      {/* Error */}
      {error && (
        <div className="p-3 rounded bg-[var(--color-danger)]/10 border border-[var(--color-danger)]/30 text-xs text-[var(--color-danger)]">
          {error}
        </div>
      )}

      {/* Results */}
      {result && result.statistics && (
        <>
          {/* Statistics cards */}
          <div className="grid grid-cols-4 gap-3">
            <StatCard label="Mean Final" value={`$${(result.statistics.mean_final ?? 0).toLocaleString(undefined, { maximumFractionDigits: 0 })}`} />
            <StatCard label="Median Final" value={`$${(result.statistics.median_final ?? 0).toLocaleString(undefined, { maximumFractionDigits: 0 })}`} />
            <StatCard label="P5 Final" value={`$${(result.statistics.p5_final ?? 0).toLocaleString(undefined, { maximumFractionDigits: 0 })}`} color="var(--color-danger)" />
            <StatCard label="P95 Final" value={`$${(result.statistics.p95_final ?? 0).toLocaleString(undefined, { maximumFractionDigits: 0 })}`} color="var(--color-success)" />
          </div>
          <div className="grid grid-cols-4 gap-3">
            <StatCard label="Prob of Profit" value={`${(result.statistics.prob_of_profit ?? 0).toFixed(1)}%`} color={(result.statistics.prob_of_profit ?? 0) >= 50 ? 'var(--color-success)' : 'var(--color-danger)'} />
            <StatCard label="Prob of Ruin" value={`${(result.statistics.prob_of_ruin ?? 0).toFixed(1)}%`} color={(result.statistics.prob_of_ruin ?? 0) <= 10 ? 'var(--color-success)' : 'var(--color-danger)'} />
            <StatCard label="Mean Max DD" value={`${(result.statistics.mean_max_dd ?? 0).toFixed(1)}%`} color={(result.statistics.mean_max_dd ?? 0) <= 50 ? 'var(--color-text-primary)' : 'var(--color-danger)'} />
            <StatCard label="P95 Max DD" value={`${(result.statistics.p95_max_dd ?? 0).toFixed(1)}%`} color={(result.statistics.p95_max_dd ?? 0) <= 70 ? 'var(--color-warning)' : 'var(--color-danger)'} />
          </div>

          {/* Histogram */}
          <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
            <div className="px-4 py-2 border-b border-[var(--color-border)]">
              <span className="text-xs font-medium text-[var(--color-text-primary)]">Final Balance Distribution</span>
            </div>
            <div className="p-2">
              <canvas ref={histoCanvasRef} className="w-full" style={{ height: '200px' }} />
            </div>
          </div>

          {/* Fan chart */}
          <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
            <div className="px-4 py-2 border-b border-[var(--color-border)]">
              <span className="text-xs font-medium text-[var(--color-text-primary)]">Sample Equity Curves (50)</span>
            </div>
            <div className="p-2">
              <canvas ref={fanCanvasRef} className="w-full" style={{ height: '250px' }} />
            </div>
          </div>
        </>
      )}
    </div>
  );
}

function StatCard({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-3">
      <div className="text-[10px] text-[var(--color-text-muted)] uppercase tracking-wider">{label}</div>
      <div className="text-sm font-bold font-mono mt-1" style={{ color: color || 'var(--color-text-primary)' }}>{value}</div>
    </div>
  );
}
