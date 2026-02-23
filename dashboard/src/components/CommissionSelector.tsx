import { useEffect, useState } from 'react';
import { DollarSign } from 'lucide-react';
import { apiGet } from '../api/client';

interface CommissionPreset {
  id: string;
  name: string;
  commission_per_lot: number;
  description?: string;
}

interface CommissionSelectorProps {
  value: number;
  onChange: (value: number) => void;
}

export function CommissionSelector({ value, onChange }: CommissionSelectorProps) {
  const [presets, setPresets] = useState<CommissionPreset[]>([]);
  const [selectedPreset, setSelectedPreset] = useState<string>('');

  useEffect(() => {
    apiGet<{ status: string; presets: CommissionPreset[] }>('/api/commissions')
      .then((res) => {
        if (res.status === 'ok' || res.presets) {
          setPresets(res.presets);
        }
      })
      .catch(console.error);
  }, []);

  const handlePresetChange = (presetId: string) => {
    setSelectedPreset(presetId);
    const preset = presets.find((p) => p.id === presetId);
    if (preset) {
      onChange(preset.commission_per_lot);
    }
  };

  return (
    <div className="space-y-1">
      <label className="block text-[10px] text-[var(--color-text-muted)] flex items-center gap-1">
        <DollarSign size={10} />
        Commission
      </label>
      {presets.length > 0 && (
        <select
          value={selectedPreset}
          onChange={(e) => handlePresetChange(e.target.value)}
          className="w-full px-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] outline-none focus:border-[var(--color-accent)]"
        >
          <option value="">Custom</option>
          {presets.map((p) => (
            <option key={p.id} value={p.id}>
              {p.name} (${p.commission_per_lot}/lot)
            </option>
          ))}
        </select>
      )}
      <div className="flex items-center gap-1">
        <input
          type="number"
          value={value}
          onChange={(e) => {
            onChange(parseFloat(e.target.value) || 0);
            setSelectedPreset('');
          }}
          step={0.1}
          min={0}
          className="w-full px-2 py-1 bg-[var(--color-bg-tertiary)] border border-[var(--color-border)] rounded text-xs text-[var(--color-text-primary)] font-mono outline-none focus:border-[var(--color-accent)]"
          placeholder="Per lot"
        />
        <span className="text-[10px] text-[var(--color-text-muted)] whitespace-nowrap">/lot</span>
      </div>
    </div>
  );
}
