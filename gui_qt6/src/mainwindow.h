/**
 * BacktestPro - Main Window
 * Professional backtesting GUI with MT5 integration
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSettings>
#include <QThread>
#include <memory>

class QDockWidget;
class QProgressBar;
class QLabel;
class QAction;
class QMenu;
class QToolBar;
class QSplitter;
class QTabWidget;

// Forward declarations for custom widgets
class InstrumentSelector;
class EquityChart;
class ParameterGrid;
class ResultsTable;
class MT5Bridge;
class BacktestWorker;

/**
 * Main application window
 * Layout:
 * ┌─────────────────────────────────────────────────────────┐
 * │ Menu Bar                                                │
 * ├─────────────────────────────────────────────────────────┤
 * │ Tool Bar                                                │
 * ├──────────────┬──────────────────────────┬───────────────┤
 * │              │                          │               │
 * │  Left Panel  │     Center Panel         │  Right Panel  │
 * │  - Symbols   │     - Equity Chart       │  - Statistics │
 * │  - Date      │     - Trade Markers      │  - Trades     │
 * │  - Params    │                          │               │
 * │              │                          │               │
 * ├──────────────┴──────────────────────────┴───────────────┤
 * │ Status Bar                                              │
 * └─────────────────────────────────────────────────────────┘
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // File menu
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void exportResults();

    // Tools menu
    void downloadTickData();
    void connectToMT5();
    void openSettings();

    // Backtest control
    void startBacktest();
    void stopBacktest();
    void pauseBacktest();

    // Progress updates
    void onBacktestProgress(int current, int total, double bestReturn);
    void onBacktestComplete();
    void onBacktestError(const QString &error);

    // MT5 connection
    void onMT5Connected(bool success);
    void onInstrumentsLoaded(const QStringList &instruments);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    void createDockWidgets();
    void createCentralWidget();
    void loadSettings();
    void saveSettings();

    // Menus
    QMenu *m_fileMenu;
    QMenu *m_editMenu;
    QMenu *m_toolsMenu;
    QMenu *m_helpMenu;

    // Actions
    QAction *m_newAction;
    QAction *m_openAction;
    QAction *m_saveAction;
    QAction *m_saveAsAction;
    QAction *m_exportAction;
    QAction *m_exitAction;
    QAction *m_downloadDataAction;
    QAction *m_connectMT5Action;
    QAction *m_settingsAction;
    QAction *m_startAction;
    QAction *m_stopAction;
    QAction *m_pauseAction;
    QAction *m_aboutAction;

    // Tool bar
    QToolBar *m_mainToolBar;

    // Status bar widgets
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QLabel *m_connectionLabel;

    // Dock widgets
    QDockWidget *m_leftDock;
    QDockWidget *m_rightDock;
    QDockWidget *m_bottomDock;

    // Custom widgets
    InstrumentSelector *m_instrumentSelector;
    EquityChart *m_equityChart;
    ParameterGrid *m_parameterGrid;
    ResultsTable *m_resultsTable;

    // MT5 Bridge (Python subprocess)
    std::unique_ptr<MT5Bridge> m_mt5Bridge;

    // Backtest worker thread
    QThread *m_workerThread;
    BacktestWorker *m_worker;
    bool m_backtestRunning;

    // Settings
    QSettings m_settings;
    QString m_currentProjectPath;
};

#endif // MAINWINDOW_H
