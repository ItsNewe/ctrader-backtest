import { useState, useEffect } from 'react';
import { Download, CheckCircle, AlertCircle, Loader2, FolderOpen } from 'lucide-react';
import { useBroker } from '../hooks/useBroker';
import { apiGet, apiPost } from '../api/client';
import { FileBrowser } from '../components/FileBrowser';
import type { BrokerConnectRequest } from '../types/broker';
import type { DataFile } from '../types/backtest';

export function Settings() {
  const { connected, brokerKey, accountInfo, connect, loading, error } = useBroker();

  const [form, setForm] = useState<BrokerConnectRequest>({
    broker: 'metatrader5',
    account_type: 'demo',
    account_id: '',
    leverage: 500,
    account_currency: 'USD',
    mt5_path: '',
  });

  // File browser state
  const [showFileBrowser, setShowFileBrowser] = useState(false);

  // Data Manager state
  const [dataFiles, setDataFiles] = useState<DataFile[]>([]);
  const [downloadSymbol, setDownloadSymbol] = useState('XAUUSD');
  const [downloadStart, setDownloadStart] = useState('2025-01-01');
  const [downloadEnd, setDownloadEnd] = useState('2025-12-31');
  const [downloading, setDownloading] = useState(false);
  const [downloadResult, setDownloadResult] = useState<{ status: string; message?: string; tick_count?: number; file_size_mb?: number } | null>(null);

  // Fetch data files on mount
  useEffect(() => {
    refreshDataFiles();
  }, []);

  const refreshDataFiles = () => {
    apiGet<{ status: string; files: DataFile[] }>('/api/data/files')
      .then((res) => {
        if (res.status === 'success') setDataFiles(res.files);
      })
      .catch(() => {});
  };

  const handleConnect = async () => {
    const success = await connect(form);
    if (success) {
      useBroker.getState().fetchSymbols();
    }
  };

  const handleDownload = async () => {
    setDownloading(true);
    setDownloadResult(null);
    try {
      const res = await apiPost<{
        status: string;
        message?: string;
        tick_count?: number;
        file_size_mb?: number;
      }>('/api/data/download-ticks', {
        symbol: downloadSymbol,
        start_date: downloadStart,
        end_date: downloadEnd,
      });
      setDownloadResult(res);
      if (res.status === 'success') {
        refreshDataFiles();
      }
    } catch (e) {
      setDownloadResult({ status: 'error', message: String(e) });
    } finally {
      setDownloading(false);
    }
  };

  const updateField = (field: string, value: string | number) => {
    setForm((prev) => ({ ...prev, [field]: value }));
  };

  return (
    <div className="max-w-2xl space-y-6">
      <h1 className="text-lg font-semibold text-[var(--color-text-primary)]">Settings</h1>

      {/* MT5 Connection */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
        <h2 className="text-sm font-semibold mb-4 text-[var(--color-text-primary)]">
          MetaTrader 5 Connection
        </h2>

        <div className="space-y-3">
          <div>
            <label className="block text-xs text-[var(--color-text-secondary)] mb-1">MT5 Terminal Path</label>
            <div className="flex gap-1.5">
              <input
                type="text"
                value={form.mt5_path || ''}
                onChange={(e) => updateField('mt5_path', e.target.value)}
                placeholder="C:\Program Files\MetaTrader 5\terminal64.exe"
                className="flex-1 px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] placeholder-[var(--color-text-muted)] outline-none focus:border-[var(--color-accent)]"
              />
              <button
                onClick={() => setShowFileBrowser(true)}
                className="px-2.5 py-1.5 rounded bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] hover:border-[var(--color-accent)] text-[var(--color-text-secondary)] hover:text-[var(--color-accent)] transition-colors"
                title="Browse for MT5 terminal"
              >
                <FolderOpen size={14} />
              </button>
            </div>
            <p className="mt-0.5 text-[10px] text-[var(--color-text-muted)]">Leave empty for default installation path</p>
          </div>

          <FormField
            label="Account ID"
            value={form.account_id}
            onChange={(v) => updateField('account_id', v)}
            placeholder="12345678"
          />

          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="block text-xs text-[var(--color-text-secondary)] mb-1">
                Account Type
              </label>
              <select
                value={form.account_type}
                onChange={(e) => updateField('account_type', e.target.value)}
                className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
              >
                <option value="demo">Demo</option>
                <option value="live">Live</option>
              </select>
            </div>

            <FormField
              label="Leverage"
              value={String(form.leverage)}
              onChange={(v) => updateField('leverage', parseInt(v) || 500)}
              placeholder="500"
              type="number"
            />
          </div>

          <FormField
            label="Account Currency"
            value={form.account_currency}
            onChange={(v) => updateField('account_currency', v)}
            placeholder="USD"
          />

          <button
            onClick={handleConnect}
            disabled={loading || !form.account_id}
            className="w-full py-2 rounded text-xs font-medium transition-colors bg-[var(--color-accent)] hover:bg-[var(--color-accent-hover)] text-white disabled:opacity-50 disabled:cursor-not-allowed"
          >
            {loading ? 'Connecting...' : connected ? 'Reconnect' : 'Connect to MT5'}
          </button>

          {error && (
            <div className="p-2 rounded bg-[var(--color-danger)]/10 border border-[var(--color-danger)]/30 text-xs text-[var(--color-danger)]">
              {error}
            </div>
          )}
        </div>
      </div>

      {/* Connection Status */}
      {connected && (
        <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
          <h2 className="text-sm font-semibold mb-3 text-[var(--color-text-primary)]">
            Connection Status
          </h2>
          <div className="space-y-2 text-xs">
            <StatusRow label="Status" value="Connected" color="var(--color-success)" />
            <StatusRow label="Broker Key" value={brokerKey || ''} mono />
            {accountInfo && (
              <>
                <StatusRow label="Balance" value={`$${accountInfo.balance.toLocaleString()}`} />
                <StatusRow label="Equity" value={`$${accountInfo.equity.toLocaleString()}`} />
                <StatusRow label="Leverage" value={`1:${accountInfo.leverage}`} />
              </>
            )}
          </div>
        </div>
      )}

      {/* Data Manager */}
      <div className="bg-[var(--color-bg-secondary)] rounded-lg border border-[var(--color-border)] p-4">
        <h2 className="text-sm font-semibold mb-4 text-[var(--color-text-primary)]">
          Tick Data Manager
        </h2>

        {/* Download form */}
        {connected && (
          <div className="space-y-3 mb-4">
            <div className="grid grid-cols-3 gap-3">
              <FormField
                label="Symbol"
                value={downloadSymbol}
                onChange={setDownloadSymbol}
                placeholder="XAUUSD"
              />
              <FormField
                label="Start Date"
                value={downloadStart}
                onChange={setDownloadStart}
                placeholder="2025-01-01"
                type="date"
              />
              <FormField
                label="End Date"
                value={downloadEnd}
                onChange={setDownloadEnd}
                placeholder="2025-12-31"
                type="date"
              />
            </div>

            <button
              onClick={handleDownload}
              disabled={downloading || !downloadSymbol}
              className="flex items-center justify-center gap-2 w-full py-2 rounded text-xs font-medium transition-colors bg-[var(--color-success)] hover:bg-[var(--color-success)]/80 text-white disabled:opacity-50 disabled:cursor-not-allowed"
            >
              {downloading ? (
                <>
                  <Loader2 size={12} className="animate-spin" />
                  Downloading from MT5...
                </>
              ) : (
                <>
                  <Download size={12} />
                  Download Tick Data
                </>
              )}
            </button>

            {downloadResult && (
              <div
                className={`p-2 rounded text-xs flex items-start gap-2 ${
                  downloadResult.status === 'success'
                    ? 'bg-[var(--color-success)]/10 border border-[var(--color-success)]/30 text-[var(--color-success)]'
                    : 'bg-[var(--color-danger)]/10 border border-[var(--color-danger)]/30 text-[var(--color-danger)]'
                }`}
              >
                {downloadResult.status === 'success' ? (
                  <CheckCircle size={14} className="mt-0.5 shrink-0" />
                ) : (
                  <AlertCircle size={14} className="mt-0.5 shrink-0" />
                )}
                <div>
                  {downloadResult.status === 'success' ? (
                    <>
                      Downloaded {downloadResult.tick_count?.toLocaleString()} ticks ({downloadResult.file_size_mb} MB)
                    </>
                  ) : (
                    downloadResult.message
                  )}
                </div>
              </div>
            )}
          </div>
        )}

        {!connected && (
          <p className="text-xs text-[var(--color-text-muted)] mb-4">
            Connect to MT5 above to download tick data, or manually place CSV files in the validation/ directory.
          </p>
        )}

        {/* Available data files */}
        <div className="space-y-1">
          <div className="text-[10px] uppercase tracking-wider text-[var(--color-text-muted)] mb-2">
            Available Tick Data ({dataFiles.length} files)
          </div>
          {dataFiles.length > 0 ? (
            dataFiles.map((f) => (
              <div
                key={f.name}
                className="flex items-center justify-between py-1.5 px-2 rounded text-xs hover:bg-[var(--color-bg-tertiary)]"
              >
                <div className="flex items-center gap-2">
                  <span className="font-medium text-[var(--color-text-primary)]">{f.symbol}</span>
                  <span className="text-[var(--color-text-muted)] font-mono text-[10px]">{f.name}</span>
                </div>
                <span className="text-[var(--color-text-secondary)]">{f.size_mb} MB</span>
              </div>
            ))
          ) : (
            <p className="text-xs text-[var(--color-text-muted)]">No tick data files found</p>
          )}
        </div>
      </div>

      {/* File Browser Modal */}
      {showFileBrowser && (
        <FileBrowser
          title="Select MT5 Terminal"
          filterExt=".exe"
          onSelect={(path) => {
            updateField('mt5_path', path);
            setShowFileBrowser(false);
          }}
          onCancel={() => setShowFileBrowser(false)}
        />
      )}
    </div>
  );
}

function StatusRow({
  label,
  value,
  color,
  mono,
}: {
  label: string;
  value: string;
  color?: string;
  mono?: boolean;
}) {
  return (
    <div className="flex justify-between">
      <span className="text-[var(--color-text-secondary)]">{label}</span>
      <span
        className={mono ? 'font-mono' : 'font-medium'}
        style={color ? { color } : undefined}
      >
        {value}
      </span>
    </div>
  );
}

function FormField({
  label,
  value,
  onChange,
  placeholder,
  hint,
  type = 'text',
}: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  hint?: string;
  type?: string;
}) {
  return (
    <div>
      <label className="block text-xs text-[var(--color-text-secondary)] mb-1">{label}</label>
      <input
        type={type}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
        className="w-full px-3 py-1.5 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] placeholder-[var(--color-text-muted)] outline-none focus:border-[var(--color-accent)]"
      />
      {hint && (
        <p className="mt-0.5 text-[10px] text-[var(--color-text-muted)]">{hint}</p>
      )}
    </div>
  );
}
