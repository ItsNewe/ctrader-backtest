import { create } from 'zustand';
import type { BacktestConfig, BacktestResult, BacktestProgress } from '../types/backtest';
import type { Strategy } from '../types/strategy';
import { apiPost, apiGet } from '../api/client';

interface BacktestState {
  strategies: Strategy[];
  selectedStrategy: string | null;
  result: BacktestResult | null;
  running: boolean;
  error: string | null;
  backtestId: string | null;
  progress: BacktestProgress | null;
  ws: WebSocket | null;

  fetchStrategies: () => Promise<void>;
  setSelectedStrategy: (id: string) => void;
  runBacktest: (config: BacktestConfig) => Promise<void>;
  clearResult: () => void;
  connectWs: (backtestId: string) => void;
  disconnectWs: () => void;
}

/** HTTP polling fallback when WebSocket is unavailable */
function startPolling(backtestId: string, get: () => BacktestState, set: (partial: Partial<BacktestState>) => void) {
  const poll = async () => {
    try {
      const res = await apiGet<{ status: string; progress: BacktestProgress }>(
        `/api/backtest/${backtestId}/status`
      );
      if (res.status === 'ok' && res.progress) {
        set({ progress: res.progress });
        if (res.progress.status === 'completed' || res.progress.status === 'error') {
          if (res.progress.status === 'completed' && res.progress.result) {
            set({ result: res.progress.result as unknown as BacktestResult, running: false });
          } else if (res.progress.status === 'error') {
            set({ error: res.progress.message || 'Backtest failed', running: false });
          }
          return; // Stop polling
        }
      }
    } catch {
      // Retry
    }
    if (get().running) {
      setTimeout(poll, 2000);
    }
  };
  setTimeout(poll, 2000);
}

export const useBacktest = create<BacktestState>((set, get) => ({
  strategies: [],
  selectedStrategy: null,
  result: null,
  running: false,
  error: null,
  backtestId: null,
  progress: null,
  ws: null,

  fetchStrategies: async () => {
    try {
      const res = await apiGet<{ status: string; strategies: Strategy[] }>('/api/strategies');
      if (res.status === 'success') {
        set({ strategies: res.strategies });
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
    set({ running: true, error: null, result: null, progress: null, backtestId: null });
    try {
      const res = await apiPost<{ status: string; backtest_id: string; progress: BacktestProgress }>(
        '/api/backtest/start',
        config
      );
      if (res.status === 'ok') {
        set({ backtestId: res.backtest_id, progress: res.progress });
        get().connectWs(res.backtest_id);
      } else {
        set({ error: 'Failed to start backtest', running: false });
      }
    } catch (e) {
      set({ error: String(e), running: false });
    }
  },

  clearResult: () => {
    get().disconnectWs();
    set({ result: null, error: null, progress: null, backtestId: null });
  },

  connectWs: (backtestId) => {
    const { ws: existingWs } = get();
    if (existingWs) existingWs.close();

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/api/backtest/ws/${backtestId}`;
    const ws = new WebSocket(wsUrl);

    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data) as BacktestProgress;
        set({ progress: data });

        if (data.status === 'completed') {
          if (data.result) {
            set({ result: data.result as unknown as BacktestResult, running: false });
          } else {
            set({ running: false });
          }
          setTimeout(() => {
            ws.close();
            set({ ws: null });
          }, 1000);
        } else if (data.status === 'error') {
          set({ error: data.message || 'Backtest failed', running: false });
          setTimeout(() => {
            ws.close();
            set({ ws: null });
          }, 1000);
        }
      } catch {
        // Ignore parse errors
      }
    };

    ws.onerror = () => {
      set({ ws: null });
      startPolling(backtestId, get, set);
    };

    ws.onclose = () => {
      const current = get().ws;
      if (current === ws) set({ ws: null });
    };

    set({ ws });
  },

  disconnectWs: () => {
    const { ws } = get();
    if (ws) {
      ws.close();
      set({ ws: null });
    }
  },
}));
