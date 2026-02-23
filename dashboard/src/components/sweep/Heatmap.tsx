import { useRef, useEffect, useMemo, useState } from 'react';
import type { SweepResultEntry } from '../../types/sweep';

const METRIC_OPTIONS = [
  { key: 'return_percent', label: 'Return %' },
  { key: 'sharpe_ratio', label: 'Sharpe Ratio' },
  { key: 'sortino_ratio', label: 'Sortino Ratio' },
  { key: 'profit_factor', label: 'Profit Factor' },
  { key: 'max_drawdown', label: 'Max Drawdown %', invert: true },
  { key: 'win_rate', label: 'Win Rate %' },
  { key: 'recovery_factor', label: 'Recovery Factor' },
  { key: 'final_balance', label: 'Final Balance' },
  { key: 'total_trades', label: 'Total Trades' },
];

interface HeatmapProps {
  results: SweepResultEntry[];
}

export function Heatmap({ results }: HeatmapProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const tooltipRef = useRef<HTMLDivElement>(null);

  // Detect available parameter names
  const paramKeys = useMemo(() => {
    if (results.length === 0) return [];
    const keys = new Set<string>();
    for (const r of results.slice(0, 20)) {
      for (const k of Object.keys(r.parameters)) keys.add(k);
    }
    return Array.from(keys);
  }, [results]);

  const [xParam, setXParam] = useState(paramKeys[0] || '');
  const [yParam, setYParam] = useState(paramKeys[1] || paramKeys[0] || '');
  const [metric, setMetric] = useState('return_percent');

  // Update axis params when paramKeys change
  useEffect(() => {
    if (paramKeys.length > 0 && !paramKeys.includes(xParam)) setXParam(paramKeys[0]);
    if (paramKeys.length > 1 && !paramKeys.includes(yParam)) setYParam(paramKeys[1]);
    if (paramKeys.length === 1) setYParam(paramKeys[0]);
  }, [paramKeys]);

  // Build heatmap data
  const heatmapData = useMemo(() => {
    if (results.length === 0 || !xParam) return null;

    // Get unique values for each axis
    const xVals = [...new Set(results.map((r) => r.parameters[xParam]))].sort((a, b) => a - b);
    const yVals = [...new Set(results.map((r) => r.parameters[yParam]))].sort((a, b) => a - b);

    if (xVals.length === 0 || yVals.length === 0) return null;

    // Build grid: index by (x, y)
    const metricOpt = METRIC_OPTIONS.find((m) => m.key === metric);
    const invert = metricOpt?.invert ?? false;

    const grid: (number | null)[][] = [];
    const entriesGrid: (SweepResultEntry | null)[][] = [];
    let minVal = Infinity;
    let maxVal = -Infinity;

    for (let yi = 0; yi < yVals.length; yi++) {
      grid[yi] = [];
      entriesGrid[yi] = [];
      for (let xi = 0; xi < xVals.length; xi++) {
        // Find result matching these params
        const match = results.find(
          (r) => r.parameters[xParam] === xVals[xi] && r.parameters[yParam] === yVals[yi]
        );
        const val = match ? (match as Record<string, unknown>)[metric] as number : null;
        grid[yi][xi] = val;
        entriesGrid[yi][xi] = match || null;
        if (val !== null) {
          minVal = Math.min(minVal, val);
          maxVal = Math.max(maxVal, val);
        }
      }
    }

    return { xVals, yVals, grid, entriesGrid, minVal, maxVal, invert };
  }, [results, xParam, yParam, metric]);

  // Draw heatmap
  useEffect(() => {
    if (!heatmapData || !canvasRef.current) return;

    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const { xVals, yVals, grid, minVal, maxVal, invert } = heatmapData;

    const MARGIN_LEFT = 60;
    const MARGIN_BOTTOM = 40;
    const MARGIN_TOP = 10;
    const MARGIN_RIGHT = 10;

    const cols = xVals.length;
    const rows = yVals.length;

    // Set canvas size
    const cellW = Math.max(30, Math.min(60, (canvas.parentElement!.clientWidth - MARGIN_LEFT - MARGIN_RIGHT) / cols));
    const cellH = Math.max(20, Math.min(40, 300 / rows));
    const width = MARGIN_LEFT + cols * cellW + MARGIN_RIGHT;
    const height = MARGIN_TOP + rows * cellH + MARGIN_BOTTOM;

    canvas.width = width * window.devicePixelRatio;
    canvas.height = height * window.devicePixelRatio;
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
    ctx.scale(window.devicePixelRatio, window.devicePixelRatio);

    ctx.fillStyle = '#131722';
    ctx.fillRect(0, 0, width, height);

    const rawRange = maxVal - minVal;
    const range = isFinite(rawRange) && rawRange > 0 ? rawRange : 1;

    for (let yi = 0; yi < rows; yi++) {
      for (let xi = 0; xi < cols; xi++) {
        const val = grid[yi][xi];
        if (val === null) {
          ctx.fillStyle = '#1e222d';
        } else {
          let t = (val - minVal) / range;
          if (invert) t = 1 - t;
          ctx.fillStyle = heatmapColor(t);
        }

        const x = MARGIN_LEFT + xi * cellW;
        const y = MARGIN_TOP + (rows - 1 - yi) * cellH; // Flip Y so higher values are on top
        ctx.fillRect(x, y, cellW - 1, cellH - 1);

        // Draw value text in cell if cells are large enough
        if (cellW >= 40 && cellH >= 24 && val !== null) {
          ctx.fillStyle = '#e0e0e0';
          ctx.font = '9px monospace';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          const display = metric === 'total_trades' ? val.toFixed(0) : val.toFixed(1);
          ctx.fillText(display, x + cellW / 2, y + cellH / 2);
        }
      }
    }

    // X-axis labels
    ctx.fillStyle = '#787b86';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    for (let xi = 0; xi < cols; xi++) {
      const x = MARGIN_LEFT + xi * cellW + cellW / 2;
      const xv = xVals[xi];
      const label = xv != null ? Number(xv).toFixed(xv % 1 === 0 ? 0 : 1) : '-';
      ctx.fillText(label, x, MARGIN_TOP + rows * cellH + 4);
    }

    // Y-axis labels
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let yi = 0; yi < rows; yi++) {
      const y = MARGIN_TOP + (rows - 1 - yi) * cellH + cellH / 2;
      const yv = yVals[yi];
      const label = yv != null ? Number(yv).toFixed(yv % 1 === 0 ? 0 : 1) : '-';
      ctx.fillText(label, MARGIN_LEFT - 6, y);
    }

    // Axis labels
    ctx.fillStyle = '#787b86';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    ctx.fillText(xParam.replace(/_/g, ' '), MARGIN_LEFT + (cols * cellW) / 2, MARGIN_TOP + rows * cellH + 22);

    ctx.save();
    ctx.translate(12, MARGIN_TOP + (rows * cellH) / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText(yParam.replace(/_/g, ' '), 0, 0);
    ctx.restore();

  }, [heatmapData]);

  // Tooltip on hover
  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    if (!heatmapData || !canvasRef.current || !tooltipRef.current) return;

    const rect = canvasRef.current.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    const { xVals, yVals, grid, entriesGrid } = heatmapData;

    const MARGIN_LEFT = 60;
    const MARGIN_TOP = 10;
    const cols = xVals.length;
    const rows = yVals.length;
    const cellW = Math.max(30, Math.min(60, (canvasRef.current.parentElement!.clientWidth - MARGIN_LEFT - 10) / cols));
    const cellH = Math.max(20, Math.min(40, 300 / rows));

    const xi = Math.floor((mx - MARGIN_LEFT) / cellW);
    const rawYi = Math.floor((my - MARGIN_TOP) / cellH);
    const yi = rows - 1 - rawYi; // Flip back

    if (xi >= 0 && xi < cols && yi >= 0 && yi < rows) {
      const val = grid[yi][xi];
      const entry = entriesGrid[yi][xi];
      const tooltip = tooltipRef.current;
      tooltip.style.display = 'block';
      tooltip.style.left = `${e.clientX - rect.left + 12}px`;
      tooltip.style.top = `${e.clientY - rect.top - 10}px`;

      if (entry) {
        const metricLabel = METRIC_OPTIONS.find((m) => m.key === metric)?.label || metric;
        const paramsStr = Object.entries(entry.parameters)
          .map(([k, v]) => `${k}: ${typeof v === 'number' ? v.toFixed(2) : v}`)
          .join(', ');
        tooltip.innerHTML = `
          <div class="font-semibold">${metricLabel}: ${val !== null ? (typeof val === 'number' ? val.toFixed(2) : val) : 'N/A'}</div>
          <div class="text-[10px] opacity-80 mt-0.5">${paramsStr}</div>
          <div class="text-[10px] opacity-60 mt-0.5">Return: ${(entry.return_percent ?? 0).toFixed(1)}% | DD: ${(entry.max_drawdown ?? 0).toFixed(1)}% | Sharpe: ${(entry.sharpe_ratio ?? 0).toFixed(2)}</div>
        `;
      } else {
        tooltip.innerHTML = '<div class="opacity-60">No data</div>';
      }
    } else {
      tooltipRef.current.style.display = 'none';
    }
  };

  const handleMouseLeave = () => {
    if (tooltipRef.current) tooltipRef.current.style.display = 'none';
  };

  if (results.length === 0 || paramKeys.length < 1) return null;

  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
      {/* Controls */}
      <div className="flex items-center gap-4 px-4 py-2 border-b border-[var(--color-border)]">
        <span className="text-xs font-medium text-[var(--color-text-primary)]">Heatmap</span>

        <div className="flex items-center gap-1.5">
          <label className="text-[10px] text-[var(--color-text-muted)]">X:</label>
          <select
            value={xParam}
            onChange={(e) => setXParam(e.target.value)}
            className="px-2 py-0.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-[10px] text-[var(--color-text-primary)] outline-none"
          >
            {paramKeys.map((k) => (
              <option key={k} value={k}>{k.replace(/_/g, ' ')}</option>
            ))}
          </select>
        </div>

        <div className="flex items-center gap-1.5">
          <label className="text-[10px] text-[var(--color-text-muted)]">Y:</label>
          <select
            value={yParam}
            onChange={(e) => setYParam(e.target.value)}
            className="px-2 py-0.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-[10px] text-[var(--color-text-primary)] outline-none"
          >
            {paramKeys.map((k) => (
              <option key={k} value={k}>{k.replace(/_/g, ' ')}</option>
            ))}
          </select>
        </div>

        <div className="flex items-center gap-1.5">
          <label className="text-[10px] text-[var(--color-text-muted)]">Color:</label>
          <select
            value={metric}
            onChange={(e) => setMetric(e.target.value)}
            className="px-2 py-0.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-[10px] text-[var(--color-text-primary)] outline-none"
          >
            {METRIC_OPTIONS.map((m) => (
              <option key={m.key} value={m.key}>{m.label}</option>
            ))}
          </select>
        </div>

        {/* Color legend */}
        <div className="flex items-center gap-1 ml-auto">
          <span className="text-[10px] text-[var(--color-text-muted)]">Worst</span>
          <div className="w-24 h-3 rounded" style={{
            background: 'linear-gradient(to right, #ef5350, #ef5350, #ffa726, #66bb6a, #26a69a)'
          }} />
          <span className="text-[10px] text-[var(--color-text-muted)]">Best</span>
        </div>
      </div>

      {/* Canvas */}
      <div className="relative p-4">
        <canvas
          ref={canvasRef}
          onMouseMove={handleMouseMove}
          onMouseLeave={handleMouseLeave}
          className="cursor-crosshair"
        />
        <div
          ref={tooltipRef}
          className="absolute z-10 pointer-events-none hidden px-2.5 py-1.5 bg-[#1e222d] border border-[var(--color-border)] rounded-lg shadow-lg text-xs text-[var(--color-text-primary)] max-w-xs"
          style={{ display: 'none' }}
        />
      </div>
    </div>
  );
}

/** Map 0..1 to red→orange→yellow→green→teal color */
function heatmapColor(t: number): string {
  t = Math.max(0, Math.min(1, t));

  // Color stops: red(0) → orange(0.25) → yellow(0.5) → green(0.75) → teal(1.0)
  const stops = [
    [0.0, 239, 83, 80],   // #ef5350 red
    [0.25, 255, 167, 38],  // #ffa726 orange
    [0.5, 255, 238, 88],   // #ffee58 yellow
    [0.75, 102, 187, 106], // #66bb6a green
    [1.0, 38, 166, 154],   // #26a69a teal
  ];

  for (let i = 0; i < stops.length - 1; i++) {
    const [t0, r0, g0, b0] = stops[i];
    const [t1, r1, g1, b1] = stops[i + 1];
    if (t >= t0 && t <= t1) {
      const f = (t - t0) / (t1 - t0);
      const r = Math.round(r0 + f * (r1 - r0));
      const g = Math.round(g0 + f * (g1 - g0));
      const b = Math.round(b0 + f * (b1 - b0));
      return `rgb(${r},${g},${b})`;
    }
  }

  return `rgb(38,166,154)`;
}
