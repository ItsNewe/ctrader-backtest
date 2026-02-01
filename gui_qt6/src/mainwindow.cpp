/**
 * BacktestPro - Main Window Implementation
 */

#include "mainwindow.h"
#include "widgets/instrumentselector.h"
#include "widgets/equitychart.h"
#include "widgets/parametergrid.h"
#include "widgets/resultstable.h"
#include "mt5bridge.h"
#include "backtestworker.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <QCloseEvent>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings("BacktestPro", "BacktestPro")
    , m_backtestRunning(false)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
    setWindowTitle("BacktestPro - Strategy Backtester");
    setMinimumSize(1280, 800);

    // Create MT5 bridge
    m_mt5Bridge = std::make_unique<MT5Bridge>(this);
    connect(m_mt5Bridge.get(), &MT5Bridge::connected, this, &MainWindow::onMT5Connected);
    connect(m_mt5Bridge.get(), &MT5Bridge::instrumentsLoaded, this, &MainWindow::onInstrumentsLoaded);

    createActions();
    createMenus();
    createToolBar();
    createStatusBar();
    createDockWidgets();
    createCentralWidget();
    loadSettings();

    // Initial status
    m_statusLabel->setText("Ready");
    m_connectionLabel->setText("MT5: Disconnected");
}

MainWindow::~MainWindow()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void MainWindow::createActions()
{
    // File menu actions
    m_newAction = new QAction(tr("&New Project"), this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openAction = new QAction(tr("&Open Project..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openProject);

    m_saveAction = new QAction(tr("&Save Project"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);

    m_saveAsAction = new QAction(tr("Save Project &As..."), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);

    m_exportAction = new QAction(tr("&Export Results..."), this);
    m_exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportResults);

    m_exitAction = new QAction(tr("E&xit"), this);
    m_exitAction->setShortcut(QKeySequence::Quit);
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);

    // Tools menu actions
    m_downloadDataAction = new QAction(tr("&Download Tick Data..."), this);
    connect(m_downloadDataAction, &QAction::triggered, this, &MainWindow::downloadTickData);

    m_connectMT5Action = new QAction(tr("&Connect to MT5"), this);
    connect(m_connectMT5Action, &QAction::triggered, this, &MainWindow::connectToMT5);

    m_settingsAction = new QAction(tr("&Settings..."), this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    // Backtest control actions
    m_startAction = new QAction(tr("&Start Backtest"), this);
    m_startAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_startAction, &QAction::triggered, this, &MainWindow::startBacktest);

    m_stopAction = new QAction(tr("S&top Backtest"), this);
    m_stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    m_stopAction->setEnabled(false);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopBacktest);

    m_pauseAction = new QAction(tr("&Pause Backtest"), this);
    m_pauseAction->setShortcut(QKeySequence(Qt::Key_F6));
    m_pauseAction->setEnabled(false);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::pauseBacktest);

    // Help menu actions
    m_aboutAction = new QAction(tr("&About BacktestPro"), this);
    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About BacktestPro"),
            tr("<h2>BacktestPro 1.0</h2>"
               "<p>Professional Strategy Backtesting Application</p>"
               "<p>Features:</p>"
               "<ul>"
               "<li>MT5-compatible tick-based backtesting</li>"
               "<li>Multi-threaded parameter optimization</li>"
               "<li>GPU-accelerated sweep (optional)</li>"
               "<li>Full MQL5 language support</li>"
               "</ul>"));
    });
}

void MainWindow::createMenus()
{
    m_fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu->addAction(m_newAction);
    m_fileMenu->addAction(m_openAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_saveAction);
    m_fileMenu->addAction(m_saveAsAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exportAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAction);

    m_editMenu = menuBar()->addMenu(tr("&Edit"));
    // Add edit actions later

    m_toolsMenu = menuBar()->addMenu(tr("&Tools"));
    m_toolsMenu->addAction(m_downloadDataAction);
    m_toolsMenu->addAction(m_connectMT5Action);
    m_toolsMenu->addSeparator();
    m_toolsMenu->addAction(m_settingsAction);

    m_helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu->addAction(m_aboutAction);
}

void MainWindow::createToolBar()
{
    m_mainToolBar = addToolBar(tr("Main Toolbar"));
    m_mainToolBar->setMovable(false);

    m_mainToolBar->addAction(m_newAction);
    m_mainToolBar->addAction(m_openAction);
    m_mainToolBar->addAction(m_saveAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_startAction);
    m_mainToolBar->addAction(m_pauseAction);
    m_mainToolBar->addAction(m_stopAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_connectMT5Action);
}

void MainWindow::createStatusBar()
{
    m_progressBar = new QProgressBar;
    m_progressBar->setMaximumWidth(200);
    m_progressBar->setVisible(false);

    m_statusLabel = new QLabel;
    m_connectionLabel = new QLabel;

    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_connectionLabel);
}

void MainWindow::createDockWidgets()
{
    // Left dock - Symbol selection and parameters
    m_leftDock = new QDockWidget(tr("Configuration"), this);
    m_leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *leftWidget = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);

    m_instrumentSelector = new InstrumentSelector;
    m_parameterGrid = new ParameterGrid;

    leftLayout->addWidget(m_instrumentSelector);
    leftLayout->addWidget(m_parameterGrid);

    m_leftDock->setWidget(leftWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    // Right dock - Results
    m_rightDock = new QDockWidget(tr("Results"), this);
    m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_resultsTable = new ResultsTable;
    m_rightDock->setWidget(m_resultsTable);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

    // Set dock sizes
    resizeDocks({m_leftDock, m_rightDock}, {300, 350}, Qt::Horizontal);
}

void MainWindow::createCentralWidget()
{
    QTabWidget *tabWidget = new QTabWidget;

    // Equity chart tab
    m_equityChart = new EquityChart;
    tabWidget->addTab(m_equityChart, tr("Equity Curve"));

    // Future tabs: Trade view, Optimization 3D, etc.
    // tabWidget->addTab(new QWidget, tr("Trade Details"));
    // tabWidget->addTab(new QWidget, tr("Optimization Map"));

    setCentralWidget(tabWidget);
}

void MainWindow::loadSettings()
{
    // Window geometry
    restoreGeometry(m_settings.value("geometry").toByteArray());
    restoreState(m_settings.value("windowState").toByteArray());

    // Last project
    m_currentProjectPath = m_settings.value("lastProject").toString();

    // MT5 path
    QString mt5Path = m_settings.value("mt5Path").toString();
    if (!mt5Path.isEmpty()) {
        m_mt5Bridge->setTerminalPath(mt5Path);
    }
}

void MainWindow::saveSettings()
{
    m_settings.setValue("geometry", saveGeometry());
    m_settings.setValue("windowState", saveState());
    m_settings.setValue("lastProject", m_currentProjectPath);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_backtestRunning) {
        auto result = QMessageBox::question(this,
            tr("Backtest Running"),
            tr("A backtest is currently running. Do you want to stop it and exit?"),
            QMessageBox::Yes | QMessageBox::No);

        if (result == QMessageBox::No) {
            event->ignore();
            return;
        }
        stopBacktest();
    }

    saveSettings();
    event->accept();
}

// Slot implementations
void MainWindow::newProject()
{
    // TODO: Reset all settings to defaults
    m_currentProjectPath.clear();
    setWindowTitle("BacktestPro - New Project");
}

void MainWindow::openProject()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open Project"), QString(), tr("BacktestPro Projects (*.btp)"));

    if (!fileName.isEmpty()) {
        // TODO: Load project file
        m_currentProjectPath = fileName;
        setWindowTitle(QString("BacktestPro - %1").arg(QFileInfo(fileName).fileName()));
    }
}

void MainWindow::saveProject()
{
    if (m_currentProjectPath.isEmpty()) {
        saveProjectAs();
    } else {
        // TODO: Save project file
    }
}

void MainWindow::saveProjectAs()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Save Project"), QString(), tr("BacktestPro Projects (*.btp)"));

    if (!fileName.isEmpty()) {
        m_currentProjectPath = fileName;
        saveProject();
        setWindowTitle(QString("BacktestPro - %1").arg(QFileInfo(fileName).fileName()));
    }
}

void MainWindow::exportResults()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Export Results"), QString(),
        tr("CSV Files (*.csv);;HTML Report (*.html);;Excel (*.xlsx)"));

    if (!fileName.isEmpty()) {
        // TODO: Export results based on file extension
    }
}

void MainWindow::downloadTickData()
{
    // TODO: Show tick data download dialog
}

void MainWindow::connectToMT5()
{
    m_statusLabel->setText("Connecting to MT5...");
    m_mt5Bridge->connect();
}

void MainWindow::openSettings()
{
    // TODO: Show settings dialog
}

void MainWindow::startBacktest()
{
    m_backtestRunning = true;
    m_startAction->setEnabled(false);
    m_stopAction->setEnabled(true);
    m_pauseAction->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText("Running backtest...");

    // TODO: Create and start worker thread
}

void MainWindow::stopBacktest()
{
    if (m_worker) {
        // TODO: Signal worker to stop
    }
    m_backtestRunning = false;
    m_startAction->setEnabled(true);
    m_stopAction->setEnabled(false);
    m_pauseAction->setEnabled(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Backtest stopped");
}

void MainWindow::pauseBacktest()
{
    // TODO: Toggle pause state
}

void MainWindow::onBacktestProgress(int current, int total, double bestReturn)
{
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_statusLabel->setText(QString("Progress: %1/%2 | Best: %3x")
        .arg(current).arg(total).arg(bestReturn, 0, 'f', 2));
}

void MainWindow::onBacktestComplete()
{
    m_backtestRunning = false;
    m_startAction->setEnabled(true);
    m_stopAction->setEnabled(false);
    m_pauseAction->setEnabled(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Backtest complete");

    QMessageBox::information(this, tr("Backtest Complete"),
        tr("The backtest has finished. Results are displayed in the Results panel."));
}

void MainWindow::onBacktestError(const QString &error)
{
    m_backtestRunning = false;
    m_startAction->setEnabled(true);
    m_stopAction->setEnabled(false);
    m_pauseAction->setEnabled(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Error");

    QMessageBox::critical(this, tr("Backtest Error"), error);
}

void MainWindow::onMT5Connected(bool success)
{
    if (success) {
        m_connectionLabel->setText("MT5: Connected");
        m_connectionLabel->setStyleSheet("color: #00ff00;");
        m_statusLabel->setText("Connected to MT5");

        // Load instruments
        m_mt5Bridge->loadInstruments();
    } else {
        m_connectionLabel->setText("MT5: Connection Failed");
        m_connectionLabel->setStyleSheet("color: #ff0000;");
        m_statusLabel->setText("Failed to connect to MT5");
    }
}

void MainWindow::onInstrumentsLoaded(const QStringList &instruments)
{
    m_instrumentSelector->setInstruments(instruments);
    m_statusLabel->setText(QString("Loaded %1 instruments from MT5").arg(instruments.size()));
}
