import { create } from 'zustand';
import { apiPost, apiGet } from '../api/client';
import type { SweepConfig, SweepProgress, SweepResultEntry } from '../types/sweep';

interface SweepState {
  // State
  sweepId: string | null;
  progress: SweepProgress | null;
  results: SweepResultEntry[];
  running: boolean;
  error: string | null;
  ws: WebSocket | null;

  // Actions
  startSweep: (config: SweepConfig) => Promise<void>;
  cancelSweep: () => Promise<void>;
  fetchResults: (sortBy?: string) => Promise<void>;
  reset: () => void;
  connectWs: (sweepId: string) => void;
  disconnectWs: () => void;
}

/** Start HTTP polling fallback when WebSocket is unavailable */
function startPolling(sweepId: string, get: () => SweepState, set: (partial: Partial<SweepState>) => void) {
  const poll = async () => {
    try {
      const res = await apiGet<{ status: string; progress: SweepProgress }>(
        `/api/sweep/${sweepId}/status`
      );
      if (res.status === 'ok') {
        set({ progress: res.progress });
        if (res.progress.status === 'completed' || res.progress.status === 'cancelled' || res.progress.status === 'error') {
          set({ running: false });
          get().fetchResults();
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

export const useSweep = create<SweepState>((set, get) => ({
  sweepId: null,
  progress: null,
  results: [],
  running: false,
  error: null,
  ws: null,

  startSweep: async (config) => {
    set({ running: true, error: null, results: [], progress: null });
    try {
      const res = await apiPost<{ status: string; sweep_id: string; progress: SweepProgress }>('/api/sweep/start', config);
      if (res.status === 'ok') {
        set({ sweepId: res.sweep_id, progress: res.progress });
        get().connectWs(res.sweep_id);
      } else {
        set({ error: 'Failed to start sweep', running: false });
      }
    } catch (e) {
      set({ error: String(e), running: false });
    }
  },

  cancelSweep: async () => {
    const { sweepId } = get();
    if (!sweepId) return;
    try {
      await fetch(`/api/sweep/${sweepId}`, { method: 'DELETE' });
    } catch {
      // Ignore cancel errors
    }
  },

  fetchResults: async (sortBy = 'return_percent') => {
    const { sweepId } = get();
    if (!sweepId) return;
    try {
      const res = await apiGet<{ status: string; results: SweepResultEntry[]; count: number }>(
        `/api/sweep/${sweepId}/results?sort_by=${sortBy}&limit=500`
      );
      if (res.status === 'ok') {
        set({ results: res.results });
      }
    } catch {
      // Retry later
    }
  },

  reset: () => {
    get().disconnectWs();
    set({ sweepId: null, progress: null, results: [], running: false, error: null });
  },

  connectWs: (sweepId) => {
    const { ws: existingWs } = get();
    if (existingWs) existingWs.close();

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/api/sweep/ws/${sweepId}`;
    const ws = new WebSocket(wsUrl);

    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data) as SweepProgress;
        set({ progress: data });

        if (data.status === 'completed' || data.status === 'cancelled' || data.status === 'error') {
          set({ running: false });
          get().fetchResults();
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
      startPolling(sweepId, get, set);
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
