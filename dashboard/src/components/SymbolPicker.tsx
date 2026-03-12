import { useState, useMemo, useRef, useEffect } from 'react';
import { ChevronDown, Database } from 'lucide-react';
import { useBroker } from '../hooks/useBroker';

export function categorizeSymbol(symbol: string): string {
  const s = symbol.toUpperCase();
  if (s.startsWith('XAU') || s.startsWith('XAG') || s.startsWith('XPT') || s.startsWith('XPD'))
    return 'Metals';
  if (s.includes('NAS') || s.includes('SPX') || s.includes('US30') || s.includes('US500') || s.includes('DAX') || s.includes('FTSE') || s.includes('US100'))
    return 'Indices';
  if (s.includes('BTC') || s.includes('ETH') || s.includes('LTC'))
    return 'Crypto';
  if (s.includes('OIL') || s.includes('BRENT') || s.includes('GAS') || s.includes('COCOA') || s.includes('COFFEE'))
    return 'Commodities';
  if (s.length === 6 || s.length === 7)
    return 'Forex';
  return 'Other';
}

const CATEGORY_ORDER = ['Metals', 'Forex', 'Indices', 'Commodities', 'Crypto', 'Other'];

interface SymbolPickerProps {
  value: string;
  onChange: (symbol: string) => void;
  disabled?: boolean;
}

export function SymbolPicker({ value, onChange, disabled }: SymbolPickerProps) {
  const { symbols: brokerSymbols, dataSymbols, ctraderSymbols } = useBroker();
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const containerRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const [highlightIndex, setHighlightIndex] = useState(0);

  const allSymbols = useMemo(() => {
    const set = new Set([...brokerSymbols, ...dataSymbols, ...ctraderSymbols]);
    return Array.from(set).sort();
  }, [brokerSymbols, dataSymbols, ctraderSymbols]);

  const filtered = useMemo(() => {
    if (!query) return allSymbols;
    const q = query.toUpperCase();
    return allSymbols.filter((s) => s.toUpperCase().includes(q));
  }, [allSymbols, query]);

  const grouped = useMemo(() => {
    const groups: Record<string, string[]> = {};
    for (const sym of filtered) {
      const cat = categorizeSymbol(sym);
      if (!groups[cat]) groups[cat] = [];
      groups[cat].push(sym);
    }
    return CATEGORY_ORDER.filter((cat) => groups[cat]).map((cat) => [cat, groups[cat]] as const);
  }, [filtered]);

  // Flat list for keyboard navigation
  const flatList = useMemo(() => grouped.flatMap(([, syms]) => syms), [grouped]);

  // Reset highlight when filtered list changes
  useEffect(() => {
    setHighlightIndex(0);
  }, [filtered]);

  // Close on outside click
  useEffect(() => {
    const handler = (e: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
        setOpen(false);
        setQuery('');
      }
    };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, []);

  // Scroll highlighted item into view
  useEffect(() => {
    if (!open || !listRef.current) return;
    const el = listRef.current.querySelector(`[data-index="${highlightIndex}"]`);
    if (el) el.scrollIntoView({ block: 'nearest' });
  }, [highlightIndex, open]);

  const select = (sym: string) => {
    onChange(sym);
    setOpen(false);
    setQuery('');
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (!open) {
      if (e.key === 'ArrowDown' || e.key === 'Enter') {
        e.preventDefault();
        setOpen(true);
      }
      return;
    }

    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault();
        setHighlightIndex((i) => Math.min(i + 1, flatList.length - 1));
        break;
      case 'ArrowUp':
        e.preventDefault();
        setHighlightIndex((i) => Math.max(i - 1, 0));
        break;
      case 'Enter':
        e.preventDefault();
        if (flatList[highlightIndex]) select(flatList[highlightIndex]);
        break;
      case 'Escape':
        e.preventDefault();
        setOpen(false);
        setQuery('');
        break;
    }
  };

  const hasDataFile = (sym: string) => dataSymbols.includes(sym);

  let flatIndex = -1;

  return (
    <div ref={containerRef} className="relative min-w-0">
      <div
        className={`flex items-center w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border rounded text-xs font-mono cursor-text ${
          open ? 'border-[var(--color-accent)]' : 'border-[var(--color-border)]'
        } ${disabled ? 'opacity-50 pointer-events-none' : ''}`}
        onClick={() => {
          if (!disabled) {
            setOpen(true);
            setTimeout(() => inputRef.current?.focus(), 0);
          }
        }}
      >
        <input
          ref={inputRef}
          type="text"
          value={open ? query : value}
          placeholder={open ? value : undefined}
          onChange={(e) => setQuery(e.target.value)}
          onFocus={() => setOpen(true)}
          onKeyDown={handleKeyDown}
          className="flex-1 min-w-0 bg-transparent border-none outline-none text-xs text-[var(--color-text-primary)] font-mono placeholder-[var(--color-text-muted)]"
          readOnly={disabled}
        />
        <ChevronDown size={12} className="text-[var(--color-text-muted)] shrink-0" />
      </div>

      {open && (
        <div
          ref={listRef}
          className="absolute z-50 mt-1 min-w-[10rem] w-full max-h-60 overflow-y-auto bg-[var(--color-bg-secondary)] border border-[var(--color-border)] rounded shadow-lg"
        >
          {grouped.map(([category, syms]) => (
            <div key={category}>
              <div className="px-3 py-1 text-[10px] font-semibold uppercase tracking-wider text-[var(--color-text-muted)] bg-[var(--color-bg-tertiary)]">
                {category}
              </div>
              {syms.map((sym) => {
                flatIndex++;
                const idx = flatIndex;
                return (
                  <button
                    key={sym}
                    data-index={idx}
                    onMouseDown={(e) => {
                      e.preventDefault();
                      select(sym);
                    }}
                    onMouseEnter={() => setHighlightIndex(idx)}
                    className={`w-full text-left px-3 py-1 text-xs font-mono flex items-center justify-between transition-colors ${
                      idx === highlightIndex
                        ? 'bg-[var(--color-accent)]/10 text-[var(--color-accent)]'
                        : sym === value
                          ? 'text-[var(--color-accent)]'
                          : 'text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)]'
                    }`}
                  >
                    <span>{sym}</span>
                    {hasDataFile(sym) && (
                      <Database size={10} className="text-[var(--color-success)] opacity-60" />
                    )}
                  </button>
                );
              })}
            </div>
          ))}

          {flatList.length === 0 && (
            <div className="px-3 py-2 text-xs text-[var(--color-text-muted)] text-center">
              No symbols match "{query}"
            </div>
          )}
        </div>
      )}
    </div>
  );
}
