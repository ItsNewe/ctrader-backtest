import { useState, useEffect, useCallback } from 'react';
import { Folder, FileText, ChevronLeft, HardDrive, X, Search } from 'lucide-react';
import { apiGet } from '../api/client';

interface BrowseEntry {
  name: string;
  path: string;
  is_dir: boolean;
}

interface BrowseResponse {
  status: string;
  path: string;
  parent: string;
  entries: BrowseEntry[];
  message?: string;
}

interface FileBrowserProps {
  onSelect: (path: string) => void;
  onCancel: () => void;
  filterExt?: string; // e.g. ".exe"
  title?: string;
}

export function FileBrowser({ onSelect, onCancel, filterExt = '.exe', title = 'Select File' }: FileBrowserProps) {
  const [currentPath, setCurrentPath] = useState('');
  const [entries, setEntries] = useState<BrowseEntry[]>([]);
  const [parentPath, setParentPath] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [search, setSearch] = useState('');

  const browse = useCallback(async (path: string) => {
    setLoading(true);
    setError('');
    setSearch('');
    try {
      const params = new URLSearchParams();
      if (path) params.set('path', path);
      if (filterExt) params.set('filter_ext', filterExt);
      const res = await apiGet<BrowseResponse>(`/api/browse?${params.toString()}`);
      if (res.status === 'success') {
        setCurrentPath(res.path);
        setParentPath(res.parent);
        setEntries(res.entries);
      } else {
        setError(res.message || 'Failed to browse');
      }
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  }, [filterExt]);

  useEffect(() => {
    // Start at common MT5 install locations
    browse('C:\\Program Files');
  }, [browse]);

  const filteredEntries = search
    ? entries.filter((e) => e.name.toLowerCase().includes(search.toLowerCase()))
    : entries;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50" onClick={onCancel}>
      <div
        className="bg-[var(--color-bg-secondary)] border border-[var(--color-border)] rounded-lg shadow-2xl w-[560px] max-h-[480px] flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between px-4 py-3 border-b border-[var(--color-border)]">
          <h3 className="text-sm font-semibold text-[var(--color-text-primary)]">{title}</h3>
          <button
            onClick={onCancel}
            className="p-1 rounded hover:bg-[var(--color-bg-tertiary)] text-[var(--color-text-muted)]"
          >
            <X size={14} />
          </button>
        </div>

        {/* Navigation bar */}
        <div className="flex items-center gap-1 px-3 py-2 border-b border-[var(--color-border)] bg-[var(--color-bg-primary)]">
          <button
            onClick={() => browse('')}
            className="p-1.5 rounded hover:bg-[var(--color-bg-tertiary)] text-[var(--color-text-muted)] shrink-0"
            title="Drives"
          >
            <HardDrive size={14} />
          </button>
          <button
            onClick={() => parentPath && browse(parentPath)}
            disabled={!parentPath}
            className="p-1.5 rounded hover:bg-[var(--color-bg-tertiary)] text-[var(--color-text-muted)] disabled:opacity-30 shrink-0"
            title="Up"
          >
            <ChevronLeft size={14} />
          </button>
          <div className="flex-1 px-2 py-1 bg-[var(--color-bg-tertiary)] rounded text-[10px] font-mono text-[var(--color-text-secondary)] truncate">
            {currentPath || 'My Computer'}
          </div>
        </div>

        {/* Search */}
        <div className="px-3 py-1.5 border-b border-[var(--color-border)]">
          <div className="relative">
            <Search size={12} className="absolute left-2 top-1/2 -translate-y-1/2 text-[var(--color-text-muted)]" />
            <input
              type="text"
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              placeholder="Filter..."
              className="w-full pl-7 pr-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] placeholder-[var(--color-text-muted)] outline-none focus:border-[var(--color-accent)]"
            />
          </div>
        </div>

        {/* File list */}
        <div className="flex-1 overflow-y-auto min-h-0">
          {loading ? (
            <div className="flex items-center justify-center py-8 text-xs text-[var(--color-text-muted)]">
              Loading...
            </div>
          ) : error ? (
            <div className="p-4 text-xs text-[var(--color-danger)]">{error}</div>
          ) : filteredEntries.length === 0 ? (
            <div className="flex items-center justify-center py-8 text-xs text-[var(--color-text-muted)]">
              {search ? 'No matches' : 'Empty directory'}
            </div>
          ) : (
            <div className="py-1">
              {filteredEntries.map((entry) => (
                <button
                  key={entry.path}
                  onClick={() => {
                    if (entry.is_dir) {
                      browse(entry.path);
                    } else {
                      onSelect(entry.path);
                    }
                  }}
                  className="flex items-center gap-2 w-full px-4 py-1.5 text-left hover:bg-[var(--color-bg-tertiary)] transition-colors"
                >
                  {entry.is_dir ? (
                    <Folder size={14} className="shrink-0 text-[var(--color-accent)]" />
                  ) : (
                    <FileText size={14} className="shrink-0 text-[var(--color-text-muted)]" />
                  )}
                  <span className="text-xs text-[var(--color-text-primary)] truncate">{entry.name}</span>
                </button>
              ))}
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="flex items-center justify-between px-4 py-2.5 border-t border-[var(--color-border)]">
          <span className="text-[10px] text-[var(--color-text-muted)]">
            {filteredEntries.length} items {filterExt && `(${filterExt})`}
          </span>
          <button
            onClick={onCancel}
            className="px-3 py-1 rounded text-xs text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-tertiary)]"
          >
            Cancel
          </button>
        </div>
      </div>
    </div>
  );
}
