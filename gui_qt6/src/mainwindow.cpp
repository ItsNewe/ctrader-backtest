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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QDialog>
#include <QDateEdit>
#include <QComboBox>
#include <QGroupBox>
#include <QPushButton>

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
    // Reset all settings to defaults
    m_currentProjectPath.clear();
    setWindowTitle("BacktestPro - New Project");

    // Reset parameter grid to defaults
    m_parameterGrid->resetToDefaults();

    // Clear results
    m_resultsTable->clearResults();

    // Clear equity chart
    m_equityChart->clear();

    m_statusLabel->setText("New project created");
}

void MainWindow::openProject()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open Project"), QString(), tr("BacktestPro Projects (*.btp)"));

    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Error"),
                tr("Could not open project file: %1").arg(file.errorString()));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();

        if (parseError.error != QJsonParseError::NoError) {
            QMessageBox::warning(this, tr("Error"),
                tr("Invalid project file: %1").arg(parseError.errorString()));
            return;
        }

        QJsonObject root = doc.object();

        // Load parameters
        if (root.contains("parameters")) {
            QJsonObject paramsObj = root["parameters"].toObject();
            QMap<QString, QVariant> params;
            for (auto it = paramsObj.begin(); it != paramsObj.end(); ++it) {
                params[it.key()] = it.value().toVariant();
            }
            m_parameterGrid->setAllParameters(params);
        }

        // Load optimization ranges if present
        if (root.contains("optimizationRanges")) {
            QJsonObject rangesObj = root["optimizationRanges"].toObject();
            for (auto it = rangesObj.begin(); it != rangesObj.end(); ++it) {
                QJsonObject rangeObj = it.value().toObject();
                OptimizationRange range;
                range.start = rangeObj["start"].toDouble();
                range.stop = rangeObj["stop"].toDouble();
                range.step = rangeObj["step"].toDouble();
                range.enabled = rangeObj["enabled"].toBool();
                m_parameterGrid->setOptimizationRange(it.key(), range);
            }
        }

        m_currentProjectPath = fileName;
        setWindowTitle(QString("BacktestPro - %1").arg(QFileInfo(fileName).fileName()));
        m_statusLabel->setText(QString("Loaded project: %1").arg(QFileInfo(fileName).fileName()));
    }
}

void MainWindow::saveProject()
{
    if (m_currentProjectPath.isEmpty()) {
        saveProjectAs();
        return;
    }

    QFile file(m_currentProjectPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not save project file: %1").arg(file.errorString()));
        return;
    }

    QJsonObject root;

    // Save version for future compatibility
    root["version"] = "1.0";
    root["application"] = "BacktestPro";

    // Save instrument selection
    root["symbol"] = m_instrumentSelector->selectedInstrument();
    root["startDate"] = m_instrumentSelector->startDate().toString(Qt::ISODate);
    root["endDate"] = m_instrumentSelector->endDate().toString(Qt::ISODate);

    // Save parameters
    QJsonObject paramsObj;
    QMap<QString, QVariant> params = m_parameterGrid->getAllParameters();
    for (auto it = params.begin(); it != params.end(); ++it) {
        paramsObj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    root["parameters"] = paramsObj;

    // Save optimization ranges
    QJsonObject rangesObj;
    QMap<QString, OptimizationRange> ranges = m_parameterGrid->getAllOptimizationRanges();
    for (auto it = ranges.begin(); it != ranges.end(); ++it) {
        QJsonObject rangeObj;
        rangeObj["start"] = it.value().start;
        rangeObj["stop"] = it.value().stop;
        rangeObj["step"] = it.value().step;
        rangeObj["enabled"] = it.value().enabled;
        rangesObj[it.key()] = rangeObj;
    }
    root["optimizationRanges"] = rangesObj;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_statusLabel->setText(QString("Saved project: %1").arg(QFileInfo(m_currentProjectPath).fileName()));
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
        QString ext = QFileInfo(fileName).suffix().toLower();

        if (ext == "csv") {
            m_resultsTable->exportToCSV(fileName);
            m_statusLabel->setText(QString("Exported results to: %1").arg(QFileInfo(fileName).fileName()));
        } else if (ext == "html") {
            m_resultsTable->exportToHTML(fileName);
            m_statusLabel->setText(QString("Exported HTML report to: %1").arg(QFileInfo(fileName).fileName()));
        } else if (ext == "xlsx") {
            // Excel export not yet implemented
            QMessageBox::information(this, tr("Export"),
                tr("Excel export is not yet implemented. Please use CSV or HTML format."));
        } else {
            // Default to CSV
            m_resultsTable->exportToCSV(fileName);
            m_statusLabel->setText(QString("Exported results to: %1").arg(QFileInfo(fileName).fileName()));
        }
    }
}

void MainWindow::downloadTickData()
{
    // Create tick data download dialog
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Download Tick Data"));
    dialog.setMinimumWidth(400);

    QFormLayout *layout = new QFormLayout(&dialog);

    // Symbol selection
    QLineEdit *symbolEdit = new QLineEdit(m_instrumentSelector->selectedInstrument());
    layout->addRow(tr("Symbol:"), symbolEdit);

    // Date range
    QDateEdit *startEdit = new QDateEdit(m_instrumentSelector->startDate());
    startEdit->setCalendarPopup(true);
    layout->addRow(tr("Start Date:"), startEdit);

    QDateEdit *endEdit = new QDateEdit(m_instrumentSelector->endDate());
    endEdit->setCalendarPopup(true);
    layout->addRow(tr("End Date:"), endEdit);

    // Source selection
    QComboBox *sourceCombo = new QComboBox;
    sourceCombo->addItem(tr("Dukascopy (Free)"), "dukascopy");
    sourceCombo->addItem(tr("MT5 Terminal"), "mt5");
    sourceCombo->addItem(tr("Local File"), "local");
    layout->addRow(tr("Data Source:"), sourceCombo);

    // Output path
    QLineEdit *outputEdit = new QLineEdit;
    outputEdit->setPlaceholderText(tr("Leave empty for default location"));
    QPushButton *browseBtn = new QPushButton(tr("Browse..."));
    QHBoxLayout *outputLayout = new QHBoxLayout;
    outputLayout->addWidget(outputEdit);
    outputLayout->addWidget(browseBtn);
    layout->addRow(tr("Output Path:"), outputLayout);

    connect(browseBtn, &QPushButton::clicked, [&]() {
        QString path = QFileDialog::getSaveFileName(&dialog,
            tr("Save Tick Data"), QString(),
            tr("CSV Files (*.csv)"));
        if (!path.isEmpty()) {
            outputEdit->setText(path);
        }
    });

    // Dialog buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QString symbol = symbolEdit->text();
        QDate start = startEdit->date();
        QDate end = endEdit->date();
        QString source = sourceCombo->currentData().toString();

        if (source == "mt5" && m_mt5Bridge) {
            m_statusLabel->setText(QString("Downloading tick data for %1...").arg(symbol));
            // MT5 download would be triggered here
            emit m_instrumentSelector->downloadRequested(symbol, start, end);
        } else if (source == "dukascopy") {
            QMessageBox::information(this, tr("Download"),
                tr("Dukascopy download requires external Python script.\n"
                   "Run: python scripts/download_dukascopy.py --symbol %1 --start %2 --end %3")
                .arg(symbol)
                .arg(start.toString("yyyy-MM-dd"))
                .arg(end.toString("yyyy-MM-dd")));
        } else if (source == "local") {
            QString filePath = QFileDialog::getOpenFileName(this,
                tr("Select Tick Data File"), QString(),
                tr("CSV Files (*.csv);;All Files (*)"));
            if (!filePath.isEmpty()) {
                m_statusLabel->setText(QString("Loaded tick data from: %1").arg(QFileInfo(filePath).fileName()));
            }
        }
    }
}

void MainWindow::connectToMT5()
{
    m_statusLabel->setText("Connecting to MT5...");
    m_mt5Bridge->connect();
}

void MainWindow::openSettings()
{
    // Create settings dialog
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Settings"));
    dialog.setMinimumWidth(500);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    // MT5 Settings Group
    QGroupBox *mt5Group = new QGroupBox(tr("MT5 Integration"));
    QFormLayout *mt5Layout = new QFormLayout(mt5Group);

    QLineEdit *mt5PathEdit = new QLineEdit(m_settings.value("mt5Path").toString());
    QPushButton *browseMT5Btn = new QPushButton(tr("Browse..."));
    QHBoxLayout *mt5PathLayout = new QHBoxLayout;
    mt5PathLayout->addWidget(mt5PathEdit);
    mt5PathLayout->addWidget(browseMT5Btn);
    mt5Layout->addRow(tr("MT5 Terminal Path:"), mt5PathLayout);

    connect(browseMT5Btn, &QPushButton::clicked, [&]() {
        QString path = QFileDialog::getExistingDirectory(&dialog,
            tr("Select MT5 Terminal Folder"));
        if (!path.isEmpty()) {
            mt5PathEdit->setText(path);
        }
    });

    mainLayout->addWidget(mt5Group);

    // Backtest Settings Group
    QGroupBox *btGroup = new QGroupBox(tr("Backtest Defaults"));
    QFormLayout *btLayout = new QFormLayout(btGroup);

    QDoubleSpinBox *balanceSpin = new QDoubleSpinBox;
    balanceSpin->setRange(100, 10000000);
    balanceSpin->setValue(m_settings.value("defaultBalance", 10000.0).toDouble());
    balanceSpin->setPrefix("$");
    btLayout->addRow(tr("Initial Balance:"), balanceSpin);

    QDoubleSpinBox *leverageSpin = new QDoubleSpinBox;
    leverageSpin->setRange(1, 2000);
    leverageSpin->setValue(m_settings.value("defaultLeverage", 500.0).toDouble());
    leverageSpin->setSuffix(":1");
    btLayout->addRow(tr("Leverage:"), leverageSpin);

    QSpinBox *threadsSpin = new QSpinBox;
    threadsSpin->setRange(1, QThread::idealThreadCount());
    threadsSpin->setValue(m_settings.value("numThreads", QThread::idealThreadCount()).toInt());
    btLayout->addRow(tr("Optimization Threads:"), threadsSpin);

    mainLayout->addWidget(btGroup);

    // Data Settings Group
    QGroupBox *dataGroup = new QGroupBox(tr("Tick Data"));
    QFormLayout *dataLayout = new QFormLayout(dataGroup);

    QLineEdit *dataPathEdit = new QLineEdit(m_settings.value("tickDataPath").toString());
    QPushButton *browseDataBtn = new QPushButton(tr("Browse..."));
    QHBoxLayout *dataPathLayout = new QHBoxLayout;
    dataPathLayout->addWidget(dataPathEdit);
    dataPathLayout->addWidget(browseDataBtn);
    dataLayout->addRow(tr("Default Data Folder:"), dataPathLayout);

    connect(browseDataBtn, &QPushButton::clicked, [&]() {
        QString path = QFileDialog::getExistingDirectory(&dialog,
            tr("Select Tick Data Folder"));
        if (!path.isEmpty()) {
            dataPathEdit->setText(path);
        }
    });

    mainLayout->addWidget(dataGroup);

    // Dialog buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        // Save settings
        m_settings.setValue("mt5Path", mt5PathEdit->text());
        m_settings.setValue("defaultBalance", balanceSpin->value());
        m_settings.setValue("defaultLeverage", leverageSpin->value());
        m_settings.setValue("numThreads", threadsSpin->value());
        m_settings.setValue("tickDataPath", dataPathEdit->text());

        // Update MT5 bridge
        if (m_mt5Bridge && !mt5PathEdit->text().isEmpty()) {
            m_mt5Bridge->setTerminalPath(mt5PathEdit->text());
        }

        m_statusLabel->setText("Settings saved");
    }
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

    // Clear previous results
    m_resultsTable->clearResults();
    m_equityChart->clear();

    // Create worker thread if not exists
    if (!m_workerThread) {
        m_workerThread = new QThread(this);
        m_worker = new BacktestWorker();
        m_worker->moveToThread(m_workerThread);

        // Connect worker signals
        connect(m_worker, &BacktestWorker::progress, this, [this](const BacktestProgress &prog) {
            onBacktestProgress(prog.currentRun, prog.totalRuns, prog.bestReturn);
        });
        connect(m_worker, &BacktestWorker::backtestComplete, this, [this](const SingleBacktestResult &result) {
            // Convert SingleBacktestResult to BacktestResult for ResultsTable
            BacktestResult btResult;
            btResult.id = 1;
            btResult.finalBalance = result.finalBalance;
            btResult.netProfit = result.netProfit;
            btResult.grossProfit = result.grossProfit;
            btResult.grossLoss = result.grossLoss;
            btResult.profitFactor = result.profitFactor;
            btResult.maxDrawdown = result.maxDrawdown;
            btResult.maxDrawdownPct = result.maxDrawdownPct;
            btResult.sharpeRatio = result.sharpeRatio;
            btResult.sortinoRatio = result.sortinoRatio;
            btResult.calmarRatio = result.calmarRatio;
            btResult.totalTrades = result.totalTrades;
            btResult.winRate = result.winRate;
            btResult.avgWin = result.averageWin;
            btResult.avgLoss = result.averageLoss;
            btResult.profitTrades = result.winningTrades;
            btResult.lossTrades = result.losingTrades;

            // Copy parameters
            for (auto it = result.parameters.begin(); it != result.parameters.end(); ++it) {
                btResult.parameters[it.key()] = it.value();
            }

            m_resultsTable->addResult(btResult);
        });
        connect(m_worker, &BacktestWorker::error, this, &MainWindow::onBacktestError);
        connect(m_worker, &BacktestWorker::finished, this, &MainWindow::onBacktestComplete);

        m_workerThread->start();
    }

    // Configure backtest from current UI settings
    BacktestConfig config;
    config.symbol = m_instrumentSelector->selectedInstrument();
    config.startDate = m_instrumentSelector->startDate().toString("yyyy.MM.dd");
    config.endDate = m_instrumentSelector->endDate().toString("yyyy.MM.dd");
    config.initialBalance = m_settings.value("defaultBalance", 10000.0).toDouble();
    config.leverage = m_settings.value("defaultLeverage", 500.0).toDouble();

    // Get symbol-specific settings
    if (config.symbol == "XAUUSD") {
        config.contractSize = 100.0;
        config.pipSize = 0.01;
        config.swapLong = -66.99;
        config.swapShort = 41.2;
    } else if (config.symbol == "XAGUSD") {
        config.contractSize = 5000.0;
        config.pipSize = 0.001;
        config.swapLong = -15.0;
        config.swapShort = 13.72;
    } else {
        // Default forex settings
        config.contractSize = 100000.0;
        config.pipSize = 0.0001;
        config.swapLong = 0.0;
        config.swapShort = 0.0;
    }

    config.swapMode = 1;  // In money
    config.swap3Days = 3; // Wednesday

    // Get tick data path
    QString dataPath = m_settings.value("tickDataPath").toString();
    if (!dataPath.isEmpty()) {
        config.tickDataPath = QString("%1/%2_TICKS.csv").arg(dataPath).arg(config.symbol);
    } else {
        // Use default path from CLAUDE.md
        config.tickDataPath = QString("C:/Users/user/Documents/ctrader-backtest/validation/Grid/%1_TICKS_2025.csv")
            .arg(config.symbol);
    }

    // Get strategy parameters from grid
    QMap<QString, QVariant> params = m_parameterGrid->getAllParameters();
    for (auto it = params.begin(); it != params.end(); ++it) {
        config.parameters[it.key()] = it.value().toDouble();
    }

    m_worker->setBacktestConfig(config);

    // Check if optimization is enabled
    if (m_parameterGrid->hasOptimizableParameters()) {
        // Setup optimization config
        OptimizationConfig optConfig;
        QMap<QString, OptimizationRange> ranges = m_parameterGrid->getAllOptimizationRanges();
        for (auto it = ranges.begin(); it != ranges.end(); ++it) {
            if (it.value().enabled) {
                OptimizationConfig::ParamRange range;
                range.name = it.key();
                range.start = it.value().start;
                range.stop = it.value().stop;
                range.step = it.value().step;
                optConfig.ranges.append(range);
            }
        }
        optConfig.numThreads = m_settings.value("numThreads", QThread::idealThreadCount()).toInt();
        optConfig.criterion = OptimizationConfig::MaxProfit;
        optConfig.useGeneticAlgorithm = false;

        m_worker->setOptimizationConfig(optConfig);

        // Start optimization
        QMetaObject::invokeMethod(m_worker, "startOptimization", Qt::QueuedConnection);
        m_statusLabel->setText("Running optimization...");
    } else {
        // Start single backtest
        QMetaObject::invokeMethod(m_worker, "startSingleBacktest", Qt::QueuedConnection);
    }
}

void MainWindow::stopBacktest()
{
    if (m_worker) {
        m_worker->stop();
    }
    m_backtestRunning = false;
    m_startAction->setEnabled(true);
    m_stopAction->setEnabled(false);
    m_pauseAction->setEnabled(false);
    m_pauseAction->setText(tr("&Pause Backtest"));
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Backtest stopped");
}

void MainWindow::pauseBacktest()
{
    if (!m_worker) return;

    if (m_worker->isPaused()) {
        m_worker->resume();
        m_pauseAction->setText(tr("&Pause Backtest"));
        m_statusLabel->setText("Backtest resumed");
    } else {
        m_worker->pause();
        m_pauseAction->setText(tr("&Resume Backtest"));
        m_statusLabel->setText("Backtest paused");
    }
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
