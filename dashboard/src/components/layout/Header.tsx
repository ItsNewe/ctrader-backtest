import { Activity, Settings, TrendingUp, Grid3x3, Clock, GitCompare, BarChart3, Dice5, Shield } from 'lucide-react';
import { useBroker } from '../../hooks/useBroker';

interface HeaderProps {
  currentPage: string;
  onNavigate: (page: string) => void;
}

export function Header({ currentPage, onNavigate }: HeaderProps) {
  const { connected, brokerKey, accountInfo, ctraderConfigured } = useBroker();

  const navItems = [
    { id: 'dashboard', label: 'Dashboard', icon: TrendingUp },
    { id: 'backtest', label: 'Backtest', icon: Activity },
    { id: 'sweep', label: 'Sweep', icon: Grid3x3 },
    { id: 'history', label: 'History', icon: Clock },
    { id: 'compare', label: 'Compare', icon: GitCompare },
    { id: 'walkforward', label: 'WF Analysis', icon: BarChart3 },
    { id: 'montecarlo', label: 'Monte Carlo', icon: Dice5 },
    { id: 'risk', label: 'Risk', icon: Shield },
    { id: 'settings', label: 'Settings', icon: Settings },
  ];

  return (
    <header className="flex items-center justify-between h-12 px-4 bg-[var(--color-bg-secondary)] border-b border-[var(--color-border)]">
      {/* Logo + Nav */}
      <div className="flex items-center gap-6">
        <span className="text-sm font-bold tracking-wide text-[var(--color-accent)]">
          BACKTEST STUDIO
        </span>

        <nav className="flex gap-1">
          {navItems.map((item) => (
            <button
              key={item.id}
              onClick={() => onNavigate(item.id)}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded text-xs font-medium transition-colors ${
                currentPage === item.id
                  ? 'bg-[var(--color-bg-tertiary)] text-[var(--color-text-primary)]'
                  : 'text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)] hover:bg-[var(--color-bg-tertiary)]/50'
              }`}
            >
              <item.icon size={14} />
              {item.label}
            </button>
          ))}
        </nav>
      </div>

      {/* Broker status */}
      <div className="flex items-center gap-3 text-xs">
        {accountInfo && (
          <span className="text-[var(--color-text-secondary)]">
            Balance: ${(accountInfo.balance ?? 0).toLocaleString()}
          </span>
        )}
        <div className="flex items-center gap-1.5">
          <div
            className={`w-2 h-2 rounded-full ${
              connected || ctraderConfigured ? 'bg-[var(--color-success)]' : 'bg-[var(--color-danger)]'
            }`}
          />
          <span className="text-[var(--color-text-secondary)]">
            {connected ? brokerKey : ctraderConfigured ? 'cTrader' : 'Disconnected'}
          </span>
        </div>
      </div>
    </header>
  );
}
