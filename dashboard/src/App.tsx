import { useState, useEffect } from 'react';
import { MainLayout } from './components/layout/MainLayout';
import { Dashboard } from './pages/Dashboard';
import { Backtest } from './pages/Backtest';
import { Sweep } from './pages/Sweep';
import { History } from './pages/History';
import { Compare } from './pages/Compare';
import { Settings } from './pages/Settings';
import { useBroker } from './hooks/useBroker';

function App() {
  const [currentPage, setCurrentPage] = useState('dashboard');
  const checkStatus = useBroker((s) => s.checkStatus);

  // Check broker status on mount
  useEffect(() => {
    checkStatus();
  }, [checkStatus]);

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
