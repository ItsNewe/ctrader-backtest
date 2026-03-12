import { useState, useEffect } from 'react';
import { MainLayout } from './components/layout/MainLayout';
import { Dashboard } from './pages/Dashboard';
import { Backtest } from './pages/Backtest';
import { Sweep } from './pages/Sweep';
import { History } from './pages/History';
import { Compare } from './pages/Compare';
import { Settings } from './pages/Settings';
import { WalkForward } from './pages/WalkForward';
import { MonteCarlo } from './pages/MonteCarlo';
import { RiskDashboard } from './pages/RiskDashboard';
import { useBroker } from './hooks/useBroker';

function App() {
  const [currentPage, setCurrentPage] = useState('dashboard');
  const checkStatus = useBroker((s) => s.checkStatus);
  const fetchDataSources = useBroker((s) => s.fetchDataSources);

  // Check broker status and data sources on mount
  useEffect(() => {
    checkStatus();
    fetchDataSources();
  }, [checkStatus, fetchDataSources]);

  // Listen for navigate events from child components
  useEffect(() => {
    const handler = (e: Event) => {
      const page = (e as CustomEvent).detail;
      if (page) setCurrentPage(page);
    };
    window.addEventListener('navigate', handler);
    return () => window.removeEventListener('navigate', handler);
  }, []);

  const renderPage = () => {
    switch (currentPage) {
      case 'backtest':
        return <Backtest />;
      case 'sweep':
        return <Sweep />;
      case 'history':
        return <History />;
      case 'compare':
        return <Compare />;
      case 'walkforward':
        return <WalkForward />;
      case 'montecarlo':
        return <MonteCarlo />;
      case 'risk':
        return <RiskDashboard />;
      case 'settings':
        return <Settings />;
      default:
        return <Dashboard />;
    }
  };

  return (
    <MainLayout currentPage={currentPage} onNavigate={setCurrentPage}>
      {renderPage()}
    </MainLayout>
  );
}

export default App;
