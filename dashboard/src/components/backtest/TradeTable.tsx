import { useState, useMemo } from 'react';
import { ChevronUp, ChevronDown, Download } from 'lucide-react';

interface Trade {
  id: number;
  direction: string;
  entry_price: number;
  exit_price: number;
  entry_time: string;
  exit_time: string;
  lot_size: number;
  profit_loss: number;
  commission: number;
  exit_reason: string;
}

type SortKey = 'id' | 'entry_time' | 'profit_loss' | 'lot_size' | 'direction';
type SortDir = 'asc' | 'desc';

export function TradeTable({
  trades,
  tradesTotal,
  truncated,
  historyId,
}: {
  trades: Trade[];
  tradesTotal: number;
  truncated: boolean;
  historyId?: number;
}) {
  const [sortKey, setSortKey] = useState<SortKey>('id');
  const [sortDir, setSortDir] = useState<SortDir>('asc');
  const [page, setPage] = useState(0);
  const pageSize = 50;

  const sorted = useMemo(() => {
    const arr = [...trades];
    arr.sort((a, b) => {
      let cmp = 0;
      switch (sortKey) {
        case 'id': cmp = a.id - b.id; break;
        case 'entry_time': cmp = a.entry_time.localeCompare(b.entry_time); break;
        case 'profit_loss': cmp = a.profit_loss - b.profit_loss; break;
        case 'lot_size': cmp = a.lot_size - b.lot_size; break;
        case 'direction': cmp = a.direction.localeCompare(b.direction); break;
      }
      return sortDir === 'asc' ? cmp : -cmp;
    });
    return arr;
  }, [trades, sortKey, sortDir]);

  const paged = useMemo(() => {
    return sorted.slice(page * pageSize, (page + 1) * pageSize);
  }, [sorted, page]);

  const totalPages = Math.ceil(trades.length / pageSize);

  const toggleSort = (key: SortKey) => {
    if (sortKey === key) {
      setSortDir(sortDir === 'asc' ? 'desc' : 'asc');
    } else {
      setSortKey(key);
      setSortDir('asc');
    }
  };

  const SortIcon = ({ col }: { col: SortKey }) => {
    if (sortKey !== col) return null;
    return sortDir === 'asc' ? <ChevronUp size={10} /> : <ChevronDown size={10} />;
  };

  return (
    <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] overflow-hidden">
      <div className="px-3 py-2 border-b border-[var(--color-border)] flex items-center justify-between">
        <span className="text-xs font-semibold text-[var(--color-text-primary)]">
          Trade History
        </span>
        <div className="flex items-center gap-2">
          {historyId && (
            <button
              onClick={() => window.open(`http://localhost:8000/api/analysis/export/trades/${historyId}`, '_blank')}
              className="flex items-center gap-1 px-2 py-1 rounded text-[10px] font-medium text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)] bg-[var(--color-bg-tertiary)] hover:bg-[var(--color-border)] transition-colors"
            >
              <Download size={10} />
              Export CSV
            </button>
          )}
          <span className="text-[10px] text-[var(--color-text-muted)]">
            {trades.length} of {tradesTotal} trades
            {truncated && ' (truncated)'}
          </span>
        </div>
      </div>

      <div className="overflow-x-auto">
        <table className="w-full text-xs">
          <thead>
            <tr className="border-b border-[var(--color-border)]">
              <Th col="id" label="#" sortKey={sortKey} sortDir={sortDir} onClick={toggleSort} />
              <Th col="direction" label="Dir" sortKey={sortKey} sortDir={sortDir} onClick={toggleSort} />
              <Th col="entry_time" label="Entry Time" sortKey={sortKey} sortDir={sortDir} onClick={toggleSort} />
              <th className="px-2 py-1.5 text-left text-[var(--color-text-muted)] font-medium">Entry</th>
              <th className="px-2 py-1.5 text-left text-[var(--color-text-muted)] font-medium">Exit</th>
              <Th col="lot_size" label="Lots" sortKey={sortKey} sortDir={sortDir} onClick={toggleSort} />
              <Th col="profit_loss" label="P/L" sortKey={sortKey} sortDir={sortDir} onClick={toggleSort} />
              <th className="px-2 py-1.5 text-left text-[var(--color-text-muted)] font-medium">Reason</th>
            </tr>
          </thead>
          <tbody>
            {paged.map((t) => (
              <tr
                key={t.id}
                className="border-b border-[var(--color-border)]/50 hover:bg-[var(--color-bg-tertiary)] transition-colors"
              >
                <td className="px-2 py-1 text-[var(--color-text-muted)] font-mono">{t.id}</td>
                <td className="px-2 py-1">
                  <span
                    className={`font-medium ${
                      t.direction === 'BUY' ? 'text-[var(--color-success)]' : 'text-[var(--color-danger)]'
                    }`}
                  >
                    {t.direction}
                  </span>
                </td>
                <td className="px-2 py-1 text-[var(--color-text-secondary)] font-mono whitespace-nowrap">
                  {t.entry_time}
                </td>
                <td className="px-2 py-1 text-[var(--color-text-primary)] font-mono">
                  {(t.entry_price ?? 0).toFixed(5)}
                </td>
                <td className="px-2 py-1 text-[var(--color-text-primary)] font-mono">
                  {(t.exit_price ?? 0).toFixed(5)}
                </td>
                <td className="px-2 py-1 text-[var(--color-text-secondary)] font-mono">
                  {(t.lot_size ?? 0).toFixed(2)}
                </td>
                <td className="px-2 py-1 font-mono font-medium">
                  <span
                    className={(t.profit_loss ?? 0) >= 0 ? 'text-[var(--color-success)]' : 'text-[var(--color-danger)]'}
                  >
                    {(t.profit_loss ?? 0) >= 0 ? '+' : ''}${(t.profit_loss ?? 0).toFixed(2)}
                  </span>
                </td>
                <td className="px-2 py-1 text-[var(--color-text-muted)]">{t.exit_reason}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {/* Pagination */}
      {totalPages > 1 && (
        <div className="px-3 py-2 border-t border-[var(--color-border)] flex items-center justify-between">
          <button
            onClick={() => setPage(Math.max(0, page - 1))}
            disabled={page === 0}
            className="px-2 py-1 rounded text-[10px] bg-[var(--color-bg-tertiary)] text-[var(--color-text-secondary)] disabled:opacity-30 hover:bg-[var(--color-border)]"
          >
            Previous
          </button>
          <span className="text-[10px] text-[var(--color-text-muted)]">
            Page {page + 1} of {totalPages}
          </span>
          <button
            onClick={() => setPage(Math.min(totalPages - 1, page + 1))}
            disabled={page >= totalPages - 1}
            className="px-2 py-1 rounded text-[10px] bg-[var(--color-bg-tertiary)] text-[var(--color-text-secondary)] disabled:opacity-30 hover:bg-[var(--color-border)]"
          >
            Next
          </button>
        </div>
      )}
    </div>
  );
}

function Th({
  col,
  label,
  sortKey,
  sortDir,
  onClick,
}: {
  col: SortKey;
  label: string;
  sortKey: SortKey;
  sortDir: SortDir;
  onClick: (col: SortKey) => void;
}) {
  return (
    <th
      className="px-2 py-1.5 text-left text-[var(--color-text-muted)] font-medium cursor-pointer hover:text-[var(--color-text-secondary)] select-none"
      onClick={() => onClick(col)}
    >
      <span className="flex items-center gap-0.5">
        {label}
        {sortKey === col && (
          sortDir === 'asc' ? <ChevronUp size={10} /> : <ChevronDown size={10} />
        )}
      </span>
    </th>
  );
}
