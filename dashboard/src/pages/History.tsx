import { useEffect, useState, useCallback } from 'react';
import { Clock, Trash2, ArrowUpDown, ChevronLeft, ChevronRight, ExternalLink, Eye } from 'lucide-react';
import { apiGet, apiFetch } from '../api/client';
import { EquityChart } from '../components/charts/EquityChart';

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
  notes: string;
}

interface HistoryDetail {
  id: number;
  equity_curve: number[];
  equity_timestamps: string[];
  full_result: Record<string, unknown>;
  strategy_params: Record<string, unknown> | null;
  broker_settings: Record<string, unknown> | null;
  initial_balance: number;
}

export function History() {
  const [entries, setEntries] = useState<HistoryEntry[]>([]);
  const [total, setTotal] = useState(0);
  const [offset, setOffset] = useState(0);
  const [sortBy, setSortBy] = useState('timestamp');
  const [ascending, setAscending] = useState(false);
  const [filterStrategy, setFilterStrategy] = useState('');
  const [filterSymbol, setFilterSymbol] = useState('');
  const [expandedId, setExpandedId] = useState<number | null>(null);
  const [detail, setDetail] = useState<HistoryDetail | null>(null);
  const [loading, setLoading] = useState(false);
  const limit = 20;

  const fetchHistory = useCallback(async () => {
    setLoading(true);
    try {
      const params = new URLSearchParams({
        limit: String(limit),
        offset: String(offset),
        sort_by: sortBy,
        ascending: String(ascending),
      });
      if (filterStrategy) params.set('strategy', filterStrategy);
      if (filterSymbol) params.set('symbol', filterSymbol);

      const res = await apiGet<{
        status: string;
        entries: HistoryEntry[];
        total: number;
      }>(`/api/backtest/history?${params}`);

      if (res.status === 'ok') {
        setEntries(res.entries);
        setTotal(res.total);
      }
    } catch (e) {
      console.error('Failed to fetch history:', e);
    } finally {
      setLoading(false);
    }
  }, [offset, sortBy, ascending, filterStrategy, filterSymbol]);

  useEffect(() => {
    fetchHistory();
  }, [fetchHistory]);

  const handleSort = (col: string) => {
    if (sortBy === col) {
      setAscending(!ascending);
    } else {
      setSortBy(col);
      setAscending(false);
    }
    setOffset(0);
  };

  const handleDelete = async (id: number) => {
    try {
      await apiFetch(`/api/backtest/history/${id}`, { method: 'DELETE' });
      fetchHistory();
      if (expandedId === id) {
        setExpandedId(null);
        setDetail(null);
      }
    } catch (e) {
      console.error('Delete failed:', e);
    }
  };

  const handleExpand = async (id: number) => {
    if (expandedId === id) {
      setExpandedId(null);
      setDetail(null);
      return;
    }
    setExpandedId(id);
    setDetail(null);
    try {
      const res = await apiGet<{ status: string; entry: HistoryDetail }>(`/api/backtest/history/${id}`);
      if (res.status === 'ok') {
        setDetail(res.entry);
      }
    } catch (e) {
      console.error('Failed to fetch detail:', e);
    }
  };

  const formatDate = (ts: number) => {
    const d = new Date(ts * 1000);
    return d.toLocaleDateString() + ' ' + d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  };

  const totalPages = Math.ceil(total / limit);
  const currentPage = Math.floor(offset / limit) + 1;

  return (
    <div className="space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h1 className="text-lg font-semibold text-[var(--color-text-primary)] flex items-center gap-2">
          <Clock size={18} /> Backtest History
        </h1>
        <span className="text-xs text-[var(--color-text-muted)]">{total} entries</span>
      </div>

      {/* Filters */}
      <div className="flex gap-2">
        <input
          type="text"
          value={filterStrategy}
          onChange={(e) => { setFilterStrategy(e.target.value); setOffset(0); }}
          placeholder="Filter strategy..."
          className="px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] w-40"
        />
        <input
          type="text"
          value={filterSymbol}
          onChange={(e) => { setFilterSymbol(e.target.value); setOffset(0); }}
          placeholder="Filter symbol..."
          className="px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)] w-40"
        />
      </div>

      {/* Table */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
        {loading && entries.length === 0 ? (
          <div className="p-8 text-center text-xs text-[var(--color-text-muted)]">Loading...</div>
        ) : entries.length === 0 ? (
          <div className="p-8 text-center text-xs text-[var(--color-text-muted)]">
            No backtest history yet. Run a backtest to see results here.
          </div>
        ) : (
          <>
            <div className="overflow-auto">
              <table className="w-full text-xs">
                <thead className="sticky top-0 bg-[var(--color-bg-secondary)]">
                  <tr className="border-b border-[var(--color-border)]">
                    <SortHeader col="timestamp" label="Date" sortBy={sortBy} ascending={ascending} onSort={handleSort} />
                    <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">Strategy</th>
                    <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">Symbol</th>
                    <th className="px-2 py-1.5 text-left text-[10px] text-[var(--color-text-muted)] font-medium">Period</th>
                    <SortHeader col="return_percent" label="Return %" sortBy={sortBy} ascending={ascending} onSort={handleSort} align="right" />
                    <SortHeader col="sharpe_ratio" label="Sharpe" sortBy={sortBy} ascending={ascending} onSort={handleSort} align="right" />
                    <SortHeader col="max_drawdown_pct" label="Max DD%" sortBy={sortBy} ascending={ascending} onSort={handleSort} align="right" />
                    <SortHeader col="profit_factor" label="PF" sortBy={sortBy} ascending={ascending} onSort={handleSort} align="right" />
                    <SortHeader col="total_trades" label="Trades" sortBy={sortBy} ascending={ascending} onSort={handleSort} align="right" />
                    <SortHeader col="win_rate" label="Win%" sortBy={sortBy} ascending={ascending} onSort={handleSort} align="right" />
                    <th className="px-2 py-1.5 text-right text-[10px] text-[var(--color-text-muted)] font-medium">Actions</th>
                  </tr>
                </thead>
                <tbody>
                  {entries.map((entry) => {
                    const isProfit = entry.return_percent >= 0;
                    const isExpanded = expandedId === entry.id;
                    return (
                      <>
                        <tr
                          key={entry.id}
                          className={`border-b border-[var(--color-border)]/50 hover:bg-[var(--color-bg-tertiary)]/50 cursor-pointer ${isExpanded ? 'bg-[var(--color-accent)]/5' : ''}`}
                          onClick={() => handleExpand(entry.id)}
                        >
                          <td className="px-2 py-1.5 text-[var(--color-text-muted)] whitespace-nowrap">{formatDate(entry.timestamp)}</td>
                          <td className="px-2 py-1.5 text-[var(--color-text-primary)] font-medium">{entry.strategy}</td>
                          <td className="px-2 py-1.5 text-[var(--color-text-primary)] font-mono">{entry.symbol}</td>
                          <td className="px-2 py-1.5 text-[var(--color-text-muted)] whitespace-nowrap">{entry.start_date} - {entry.end_date}</td>
                          <td className="px-2 py-1.5 text-right font-mono" style={{ color: isProfit ? 'var(--color-success)' : 'var(--color-danger)' }}>
                            {isProfit ? '+' : ''}{entry.return_percent.toFixed(1)}%
                          </td>
                          <td className="px-2 py-1.5 text-right font-mono text-[var(--color-text-primary)]">{entry.sharpe_ratio.toFixed(2)}</td>
                          <td className="px-2 py-1.5 text-right font-mono" style={{ color: entry.max_drawdown_pct > 60 ? 'var(--color-danger)' : 'var(--color-text-primary)' }}>
                            {entry.max_drawdown_pct.toFixed(1)}%
                          </td>
                          <td className="px-2 py-1.5 text-right font-mono text-[var(--color-text-primary)]">{entry.profit_factor.toFixed(2)}</td>
                          <td className="px-2 py-1.5 text-right font-mono text-[var(--color-text-primary)]">{entry.total_trades.toLocaleString()}</td>
                          <td className="px-2 py-1.5 text-right font-mono text-[var(--color-text-primary)]">{entry.win_rate.toFixed(1)}%</td>
                          <td className="px-2 py-1.5 text-right">
                            <div className="flex items-center justify-end gap-1">
                              <button
                                onClick={(e) => { e.stopPropagation(); handleExpand(entry.id); }}
                                className="p-1 rounded hover:bg-[var(--color-bg-tertiary)] text-[var(--color-text-muted)] hover:text-[var(--color-text-primary)]"
                                title="View details"
                              >
                                <Eye size={12} />
                              </button>
                              <button
                                onClick={(e) => { e.stopPropagation(); handleDelete(entry.id); }}
                                className="p-1 rounded hover:bg-[var(--color-danger)]/20 text-[var(--color-text-muted)] hover:text-[var(--color-danger)]"
                                title="Delete"
                              >
                                <Trash2 size={12} />
                              </button>
                            </div>
                          </td>
                        </tr>

                        {/* Expanded detail row */}
                        {isExpanded && (
                          <tr key={`${entry.id}-detail`}>
                            <td colSpan={11} className="p-0">
                              <HistoryDetailPanel entry={entry} detail={detail} />
                            </td>
                          </tr>
                        )}
                      </>
                    );
                  })}
                </tbody>
              </table>
            </div>

            {/* Pagination */}
            {totalPages > 1 && (
              <div className="flex items-center justify-between px-4 py-2 border-t border-[var(--color-border)]">
                <span className="text-[10px] text-[var(--color-text-muted)]">
                  Page {currentPage} of {totalPages}
                </span>
                <div className="flex items-center gap-1">
                  <button
                    onClick={() => setOffset(Math.max(0, offset - limit))}
                    disabled={offset === 0}
                    className="p-1 rounded hover:bg-[var(--color-bg-tertiary)] text-[var(--color-text-muted)] disabled:opacity-30"
                  >
                    <ChevronLeft size={14} />
                  </button>
                  <button
                    onClick={() => setOffset(offset + limit)}
                    disabled={offset + limit >= total}
                    className="p-1 rounded hover:bg-[var(--color-bg-tertiary)] text-[var(--color-text-muted)] disabled:opacity-30"
                  >
                    <ChevronRight size={14} />
                  </button>
                </div>
              </div>
            )}
          </>
        )}
      </div>
    </div>
  );
}

// ── Sortable header component ────────────────────────────

function SortHeader({
  col,
  label,
  sortBy,
  ascending,
  onSort,
  align = 'left',
}: {
  col: string;
  label: string;
  sortBy: string;
  ascending: boolean;
  onSort: (col: string) => void;
  align?: 'left' | 'right';
}) {
  return (
    <th
      onClick={() => onSort(col)}
      className={`px-2 py-1.5 text-[10px] text-[var(--color-text-muted)] font-medium cursor-pointer hover:text-[var(--color-text-primary)] select-none whitespace-nowrap text-${align}`}
    >
      <span className={`flex items-center gap-0.5 ${align === 'right' ? 'justify-end' : ''}`}>
        {label}
        {sortBy === col && (
          <ArrowUpDown size={10} className="text-[var(--color-accent)]" />
        )}
      </span>
    </th>
  );
}

// ── Expanded detail panel ────────────────────────────────

function HistoryDetailPanel({
  entry,
  detail,
}: {
  entry: HistoryEntry;
  detail: HistoryDetail | null;
}) {
  return (
    <div className="bg-[var(--color-bg-tertiary)]/30 border-t border-[var(--color-border)] p-4 space-y-3">
      {/* Strategy params */}
      {entry.strategy_params && Object.keys(entry.strategy_params).length > 0 && (
        <div>
          <div className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)] mb-1">
            Strategy Parameters
          </div>
          <div className="flex flex-wrap gap-2">
            {Object.entries(entry.strategy_params).map(([k, v]) => (
              <span key={k} className="px-2 py-0.5 bg-[var(--color-bg-tertiary)] rounded text-[10px] font-mono text-[var(--color-text-secondary)]">
                {k}: {typeof v === 'number' ? (v as number).toFixed(2) : String(v)}
              </span>
            ))}
          </div>
        </div>
      )}

      {/* Equity curve */}
      {detail?.equity_curve && detail.equity_curve.length > 0 ? (
        <div>
          <div className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)] mb-1">
            Equity Curve
          </div>
          <div className="h-48">
            <EquityChart
              equityCurve={detail.equity_curve}
              timestamps={detail.equity_timestamps}
              initialBalance={detail.initial_balance}
            />
          </div>
        </div>
      ) : (
        <div className="text-xs text-[var(--color-text-muted)]">Loading equity curve...</div>
      )}

      {/* Additional metrics from full result */}
      {detail?.full_result && (
        <div className="grid grid-cols-6 gap-2 text-xs">
          <MetricPill label="Final Balance" value={`$${(detail.full_result.final_balance as number || 0).toLocaleString(undefined, { maximumFractionDigits: 0 })}`} />
          <MetricPill label="Sortino" value={String((detail.full_result.sortino_ratio as number || 0).toFixed(2))} />
          <MetricPill label="Recovery" value={String((detail.full_result.recovery_factor as number || 0).toFixed(2))} />
          <MetricPill label="Peak Equity" value={`$${(detail.full_result.peak_equity as number || 0).toLocaleString(undefined, { maximumFractionDigits: 0 })}`} />
          <MetricPill label="Max Positions" value={String(detail.full_result.max_open_positions || 0)} />
          <MetricPill
            label="Stop Out"
            value={detail.full_result.stop_out_occurred ? 'YES' : 'No'}
            color={detail.full_result.stop_out_occurred ? 'var(--color-danger)' : 'var(--color-success)'}
          />
        </div>
      )}
    </div>
  );
}

function MetricPill({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div className="bg-[var(--color-bg-secondary)] rounded px-2 py-1.5 border border-[var(--color-border)]">
      <div className="text-[10px] text-[var(--color-text-muted)]">{label}</div>
      <div className="text-xs font-mono font-medium" style={color ? { color } : { color: 'var(--color-text-primary)' }}>
        {value}
      </div>
    </div>
  );
}
