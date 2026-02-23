import { useEffect, useState } from 'react';
import { Save, FolderOpen, Tag, Loader2 } from 'lucide-react';
import { apiGet, apiPost } from '../api/client';

interface StrategyVersion {
  id: number;
  name: string;
  notes: string;
  timestamp: number;
  params: Record<string, number | string | boolean>;
}

interface VersionManagerProps {
  strategyId: string;
  currentParams: Record<string, number | string | boolean>;
  onLoadVersion: (params: Record<string, number | string | boolean>) => void;
}

export function VersionManager({ strategyId, currentParams, onLoadVersion }: VersionManagerProps) {
  const [versions, setVersions] = useState<StrategyVersion[]>([]);
  const [showSave, setShowSave] = useState(false);
  const [saveName, setSaveName] = useState('');
  const [saveNotes, setSaveNotes] = useState('');
  const [saving, setSaving] = useState(false);
  const [loading, setLoading] = useState(false);

  // Fetch versions
  const fetchVersions = async () => {
    if (!strategyId) return;
    try {
      const res = await apiGet<{ status: string; versions: StrategyVersion[] }>(
        `/api/versions/${strategyId}`
      );
      if (res.versions) {
        setVersions(res.versions);
      }
    } catch (e) {
      console.error('Failed to fetch versions:', e);
    }
  };

  useEffect(() => {
    fetchVersions();
  }, [strategyId]);

  const handleSave = async () => {
    if (!saveName.trim()) return;
    setSaving(true);
    try {
      await apiPost(`/api/versions/${strategyId}`, {
        name: saveName,
        notes: saveNotes,
        params: currentParams,
      });
      await fetchVersions();
      setShowSave(false);
      setSaveName('');
      setSaveNotes('');
    } catch (e) {
      console.error('Failed to save version:', e);
    } finally {
      setSaving(false);
    }
  };

  const handleLoad = async (version: StrategyVersion) => {
    setLoading(true);
    onLoadVersion(version.params);
    setLoading(false);
  };

  const formatDate = (ts: number) => {
    const d = new Date(ts * 1000);
    return d.toLocaleDateString();
  };

  return (
    <div className="space-y-2">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-1.5 text-[10px] uppercase tracking-wider text-[var(--color-text-muted)]">
          <Tag size={10} />
          Versions
          {versions.length > 0 && (
            <span className="px-1 py-0.5 bg-[var(--color-accent)] text-white rounded text-[9px] font-mono">
              {versions.length}
            </span>
          )}
        </div>
        <button
          onClick={() => setShowSave(!showSave)}
          className="flex items-center gap-1 px-2 py-0.5 rounded text-[10px] text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)] hover:bg-[var(--color-bg-tertiary)] transition-colors"
        >
          <Save size={10} />
          Save
        </button>
      </div>

      {/* Save dialog */}
      {showSave && (
        <div className="bg-[var(--color-bg-tertiary)] rounded border border-[var(--color-border)] p-2 space-y-2">
          <input
            type="text"
            value={saveName}
            onChange={(e) => setSaveName(e.target.value)}
            placeholder="Version name..."
            className="w-full px-2 py-1 bg-[var(--color-bg-primary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
          />
          <input
            type="text"
            value={saveNotes}
            onChange={(e) => setSaveNotes(e.target.value)}
            placeholder="Notes (optional)..."
            className="w-full px-2 py-1 bg-[var(--color-bg-primary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-muted)] outline-none focus:border-[var(--color-accent)]"
          />
          <div className="flex gap-1">
            <button
              onClick={handleSave}
              disabled={!saveName.trim() || saving}
              className="flex items-center gap-1 px-2 py-1 rounded text-[10px] font-medium bg-[var(--color-accent)] text-white disabled:opacity-50 hover:bg-[var(--color-accent-hover)] transition-colors"
            >
              {saving ? <Loader2 size={10} className="animate-spin" /> : <Save size={10} />}
              Save
            </button>
            <button
              onClick={() => setShowSave(false)}
              className="px-2 py-1 rounded text-[10px] text-[var(--color-text-muted)] hover:text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-secondary)] transition-colors"
            >
              Cancel
            </button>
          </div>
        </div>
      )}

      {/* Load dropdown */}
      {versions.length > 0 && (
        <div className="space-y-1">
          {versions.map((v) => (
            <button
              key={v.id}
              onClick={() => handleLoad(v)}
              disabled={loading}
              className="w-full flex items-center justify-between px-2 py-1 rounded text-xs hover:bg-[var(--color-bg-tertiary)] transition-colors group"
            >
              <div className="flex items-center gap-1.5 text-left">
                <FolderOpen size={10} className="text-[var(--color-text-muted)] group-hover:text-[var(--color-accent)]" />
                <span className="text-[var(--color-text-primary)] font-medium">{v.name}</span>
                {v.notes && (
                  <span className="text-[10px] text-[var(--color-text-muted)] truncate max-w-[100px]">{v.notes}</span>
                )}
              </div>
              <span className="text-[9px] text-[var(--color-text-muted)]">{formatDate(v.timestamp)}</span>
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
