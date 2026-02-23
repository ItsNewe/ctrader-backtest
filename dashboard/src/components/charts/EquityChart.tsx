import { useEffect, useRef, memo } from 'react';
import { createChart, ColorType, AreaSeries } from 'lightweight-charts';
import type { IChartApi, ISeriesApi, Time } from 'lightweight-charts';

interface EquityChartProps {
  equityCurve: number[];
  timestamps: string[];
  height?: number;
  initialBalance?: number;
}

function parseTimestamp(ts: string): Time {
  // Format: "2025.01.02 01:32:51.955" -> seconds since epoch
  const clean = ts.replace(/\.\d{3}$/, ''); // Remove milliseconds
  const d = new Date(clean.replace(/\./g, '-').replace(' ', 'T') + 'Z');
  return Math.floor(d.getTime() / 1000) as Time;
}

export const EquityChart = memo(function EquityChart({
  equityCurve,
  timestamps,
  height = 300,
  initialBalance = 10000,
}: EquityChartProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<IChartApi | null>(null);
  const seriesRef = useRef<ISeriesApi<'Area'> | null>(null);

  // Create chart on mount
  useEffect(() => {
    if (!containerRef.current) return;

    const chart = createChart(containerRef.current, {
      width: containerRef.current.clientWidth,
      height,
      layout: {
        background: { type: ColorType.Solid, color: '#1e222d' },
        textColor: '#787b86',
        fontSize: 11,
      },
      grid: {
        vertLines: { color: '#2a2e39' },
        horzLines: { color: '#2a2e39' },
      },
      crosshair: {
        vertLine: { color: '#758696', width: 1, style: 3 },
        horzLine: { color: '#758696', width: 1, style: 3 },
      },
      rightPriceScale: {
        borderColor: '#2a2e39',
      },
      timeScale: {
        borderColor: '#2a2e39',
        timeVisible: true,
        secondsVisible: false,
      },
    });

    // v5 API: addSeries(AreaSeries, options)
    const series = chart.addSeries(AreaSeries, {
      lineColor: '#2962FF',
      topColor: 'rgba(41, 98, 255, 0.3)',
      bottomColor: 'rgba(41, 98, 255, 0.02)',
      lineWidth: 2,
      priceFormat: { type: 'price', precision: 2, minMove: 0.01 },
    });

    chartRef.current = chart;
    seriesRef.current = series;

    // Handle resize
    const resizeObserver = new ResizeObserver((entries) => {
      for (const entry of entries) {
        chart.applyOptions({ width: entry.contentRect.width });
      }
    });
    resizeObserver.observe(containerRef.current);

    return () => {
      resizeObserver.disconnect();
      chart.remove();
      chartRef.current = null;
      seriesRef.current = null;
    };
  }, [height]);

  // Update data
  useEffect(() => {
    if (!seriesRef.current || equityCurve.length === 0) return;

    const data: { time: Time; value: number }[] = [];

    if (timestamps && timestamps.length === equityCurve.length) {
      for (let i = 0; i < equityCurve.length; i++) {
        try {
          data.push({
            time: parseTimestamp(timestamps[i]),
            value: equityCurve[i],
          });
        } catch {
          // Skip invalid timestamps
        }
      }
    } else {
      // Fallback: use index-based time
      const now = Math.floor(Date.now() / 1000);
      const interval = 86400;
      for (let i = 0; i < equityCurve.length; i++) {
        data.push({
          time: (now - (equityCurve.length - i) * interval) as Time,
          value: equityCurve[i],
        });
      }
    }

    seriesRef.current.setData(data);
    chartRef.current?.timeScale().fitContent();

    // Color: green if above initial balance, red if below
    const lastValue = equityCurve[equityCurve.length - 1];
    const isProfit = lastValue >= initialBalance;
    seriesRef.current.applyOptions({
      lineColor: isProfit ? '#26a69a' : '#ef5350',
      topColor: isProfit ? 'rgba(38, 166, 154, 0.3)' : 'rgba(239, 83, 80, 0.3)',
      bottomColor: isProfit ? 'rgba(38, 166, 154, 0.02)' : 'rgba(239, 83, 80, 0.02)',
    });
  }, [equityCurve, timestamps, initialBalance]);

  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
      <div className="px-3 py-2 border-b border-[var(--color-border)] flex items-center justify-between">
        <span className="text-xs font-semibold text-[var(--color-text-primary)]">Equity Curve</span>
        {equityCurve.length > 0 && (
          <span className="text-[10px] text-[var(--color-text-muted)]">
            {equityCurve.length} data points
          </span>
        )}
      </div>
      <div ref={containerRef} style={{ height }} />
    </div>
  );
});
