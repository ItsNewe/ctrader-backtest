import { create } from 'zustand';
import type { InstrumentSpec, AccountInfo, BrokerConnectRequest } from '../types/broker';
import { apiPost, apiGet } from '../api/client';

interface BrokerState {
  connected: boolean;
  brokerKey: string | null;
  accountInfo: AccountInfo | null;
  symbols: string[];
  specs: Record<string, InstrumentSpec>;
  activeSymbol: string | null;
  loading: boolean;
  error: string | null;

  connect: (config: BrokerConnectRequest) => Promise<boolean>;
  fetchSymbols: () => Promise<void>;
  fetchSpec: (symbol: string) => Promise<InstrumentSpec | null>;
  setActiveSymbol: (symbol: string) => void;
  checkStatus: () => Promise<void>;
}

export const useBroker = create<BrokerState>((set, get) => ({
  connected: false,
  brokerKey: null,
  accountInfo: null,
  symbols: [],
  specs: {},
  activeSymbol: null,
  loading: false,
  error: null,

  connect: async (config) => {
    set({ loading: true, error: null });
    try {
      const res = await apiPost<{ status: string; message: string; broker_key?: string }>(
        '/api/broker/connect',
        config
      );
      if (res.status === 'success') {
        set({ connected: true, brokerKey: res.broker_key, loading: false });

        // Auto-fetch account info
        try {
          const acctRes = await apiGet<{ status: string; account: AccountInfo }>('/api/broker/account');
          if (acctRes.status === 'success') {
            set({ accountInfo: acctRes.account });
          }
        } catch { /* non-critical */ }

        return true;
      } else {
        set({ error: res.message, loading: false });
        return false;
      }
    } catch (e) {
      set({ error: String(e), loading: false });
      return false;
    }
  },

  fetchSymbols: async () => {
    try {
      const res = await apiGet<{ status: string; symbols: string[] }>('/api/broker/symbols');
      if (res.status === 'success') {
        set({ symbols: res.symbols });
      }
    } catch (e) {
      console.error('Failed to fetch symbols:', e);
    }
  },

  fetchSpec: async (symbol) => {
    const cached = get().specs[symbol];
    if (cached) return cached;

    try {
      const res = await apiGet<{ status: string; spec: InstrumentSpec }>(`/api/broker/specs/${symbol}`);
      if (res.status === 'success') {
        set((state) => ({ specs: { ...state.specs, [symbol]: res.spec } }));
        return res.spec;
      }
    } catch (e) {
      console.error(`Failed to fetch spec for ${symbol}:`, e);
    }
    return null;
  },

  setActiveSymbol: (symbol) => {
    set({ activeSymbol: symbol });
    // Auto-fetch spec when symbol changes
    get().fetchSpec(symbol);
  },

  checkStatus: async () => {
    try {
      const res = await apiGet<{ connected: boolean; active_broker: string | null }>('/api/broker/status');
      set({ connected: res.connected, brokerKey: res.active_broker });
    } catch { /* API not running yet */ }
  },
}));
