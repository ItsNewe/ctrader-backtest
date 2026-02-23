import { create } from 'zustand';
import type { BacktestConfig, BacktestResult } from '../types/backtest';
import type { Strategy } from '../types/strategy';
import { apiPost, apiGet } from '../api/client';

interface BacktestState {
  strategies: Strategy[];
  selectedStrategy: string | null;
  result: BacktestResult | null;
  running: boolean;
  error: string | null;

  fetchStrategies: () => Promise<void>;
  setSelectedStrategy: (id: string) => void;
  runBacktest: (config: BacktestConfig) => Promise<void>;
  clearResult: () => void;
}

export const useBacktest = create<BacktestState>((set) => ({
  strategies: [],
  selectedStrategy: null,
  result: null,
  running: false,
  error: null,

  fetchStrategies: async () => {
    try {
      const res = await apiGet<{ status: string; strategies: Strategy[] }>('/api/strategies');
      if (res.status === 'success') {
        set({ strategies: res.strategies });
        // Auto-select first strategy
        if (res.strategies.length > 0) {
          set({ selectedStrategy: res.strategies[0].id });
        }
      }
    } catch (e) {
      console.error('Failed to fetch strategies:', e);
    }
  },

  setSelectedStrategy: (id) => set({ selectedStrategy: id }),

  runBacktest: async (config) => {
    set({ running: true, error: null, result: null });
    try {
      const result = await apiPost<BacktestResult>('/api/backtest/run', config);
      if (result.status === 'error') {
        set({ error: result.message || 'Backtest failed', running: false });
      } else {
        set({ result, running: false });
      }
    } catch (e) {
      set({ error: String(e), running: false });
    }
  },

  clearResult: () => set({ result: null, error: null }),
}));
