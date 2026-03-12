import { useState, useMemo } from 'react';
import { Search, ChevronDown, ChevronRight, Database } from 'lucide-react';
import { useBroker } from '../../hooks/useBroker';
import { categorizeSymbol } from '../SymbolPicker';

export function Sidebar() {
  const { symbols: brokerSymbols, activeSymbol, setActiveSymbol, connected, ctraderConfigured, ctraderSymbols, dataSymbols } = useBroker();
  const [search, setSearch] = useState('');
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({});

  // Merge broker symbols + data symbols + cTrader symbols (deduplicated)
  const allSymbols = useMemo(() => {
    const set = new Set([...brokerSymbols, ...dataSymbols, ...ctraderSymbols]);
    return Array.from(set).sort();
  }, [brokerSymbols, dataSymbols, ctraderSymbols]);

  const grouped = useMemo(() => {
    const filtered = allSymbols.filter((s) =>
      s.toUpperCase().includes(search.toUpperCase())
    );
    const groups: Record<string, string[]> = {};
    for (const sym of filtered) {
      const cat = categorizeSymbol(sym);
      if (!groups[cat]) groups[cat] = [];
      groups[cat].push(sym);
    }
    const order = ['Metals', 'Forex', 'Indices', 'Commodities', 'Crypto', 'Other'];
    const sorted: [string, string[]][] = [];
    for (const cat of order) {
      if (groups[cat]) sorted.push([cat, groups[cat].sort()]);
    }
    return sorted;
  }, [allSymbols, search]);

  const toggleGroup = (cat: string) => {
    setCollapsed((prev) => ({ ...prev, [cat]: !prev[cat] }));
  };

  const hasDataFile = (sym: string) => dataSymbols.includes(sym);

  return (
    <aside className="w-52 bg-[var(--color-bg-secondary)] border-r border-[var(--color-border)] flex flex-col overflow-hidden">
      {/* Header */}
      <div className="px-3 py-2 border-b border-[var(--color-border)]">
        <div className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)] mb-1">
          Symbols
        </div>
        <div className="flex items-center gap-1.5 px-2 py-1 bg-[var(--color-bg-tertiary)] rounded">
          <Search size={12} className="text-[var(--color-text-muted)]" />
          <input
            type="text"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            placeholder="Search..."
            className="flex-1 bg-transparent border-none outline-none text-xs text-[var(--color-text-primary)] placeholder-[var(--color-text-muted)]"
          />
        </div>
      </div>

      {/* Symbol list */}
      <div className="flex-1 overflow-y-auto">
        {grouped.map(([category, syms]) => (
          <div key={category}>
            <button
              onClick={() => toggleGroup(category)}
              className="w-full flex items-center gap-1 px-3 py-1.5 text-[10px] font-semibold uppercase tracking-wider text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)]"
            >
              {collapsed[category] ? <ChevronRight size={10} /> : <ChevronDown size={10} />}
              {category} ({syms.length})
            </button>

            {!collapsed[category] &&
              syms.map((sym) => (
                <button
                  key={sym}
                  onClick={() => setActiveSymbol(sym)}
                  className={`w-full text-left px-4 py-1 text-xs flex items-center justify-between transition-colors ${
                    activeSymbol === sym
                      ? 'bg-[var(--color-accent)]/10 text-[var(--color-accent)] font-medium'
                      : 'text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)] hover:text-[var(--color-text-primary)]'
                  }`}
                >
                  <span>{sym}</span>
                  {hasDataFile(sym) && (
                    <Database size={10} className="text-[var(--color-success)] opacity-60" />
                  )}
                </button>
              ))}
          </div>
        ))}

        {grouped.length === 0 && (
          <div className="p-3 text-xs text-[var(--color-text-muted)] text-center">
            {allSymbols.length === 0
              ? connected
                ? 'Loading symbols...'
                : 'No tick data found. Connect to MT5 or add CSV files to validation/'
              : 'No symbols match your search'}
          </div>
        )}
      </div>

      {/* Status bar */}
      <div className="px-3 py-1.5 border-t border-[var(--color-border)] text-[10px] text-[var(--color-text-muted)]">
        {connected ? (
          <span className="text-[var(--color-success)]">&#9679; MT5</span>
        ) : ctraderConfigured ? (
          <span className="text-[var(--color-success)]">&#9679; cTrader</span>
        ) : (
          <span>&#9679; Offline</span>
        )}
        <span className="ml-2">{allSymbols.length} symbols</span>
      </div>
    </aside>
  );
}
