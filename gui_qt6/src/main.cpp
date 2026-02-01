/**
 * BacktestPro - Professional Strategy Backtesting Application
 * Main entry point
 */

#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // Enable high DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // Set application metadata
    app.setOrganizationName("BacktestPro");
    app.setApplicationName("BacktestPro");
    app.setApplicationVersion("1.0.0");

    // Use Grid style for consistent cross-platform look
    app.setStyle(QStyleFactory::create("Grid"));

    // Apply dark theme palette
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
    app.setPalette(darkPalette);

    // Set stylesheet for additional customization
    app.setStyleSheet(R"(
        QToolTip {
            color: #ffffff;
            background-color: #2a2a2a;
            border: 1px solid #767676;
            padding: 4px;
        }
        QMenuBar {
            background-color: #353535;
        }
        QMenuBar::item:selected {
            background-color: #2a82da;
        }
        QMenu {
            background-color: #353535;
            border: 1px solid #767676;
        }
        QMenu::item:selected {
            background-color: #2a82da;
        }
        QTabWidget::pane {
            border: 1px solid #767676;
        }
        QTabBar::tab {
            background-color: #353535;
            padding: 8px 16px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background-color: #2a82da;
        }
        QProgressBar {
            border: 1px solid #767676;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #2a82da;
        }
        QHeaderView::section {
            background-color: #353535;
            padding: 4px;
            border: 1px solid #767676;
        }
        QTableView {
            gridline-color: #767676;
        }
    )");

    // Create and show main window
    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
