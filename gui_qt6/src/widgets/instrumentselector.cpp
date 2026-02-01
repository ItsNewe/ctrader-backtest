/**
 * InstrumentSelector Widget Implementation
 */

#include "instrumentselector.h"

#include <QComboBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QCompleter>
#include <QSortFilterProxyModel>
#include <QStringListModel>

InstrumentSelector::InstrumentSelector(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void InstrumentSelector::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Symbol selection group
    m_symbolGroup = new QGroupBox(tr("Instrument"));
    QVBoxLayout *symbolLayout = new QVBoxLayout(m_symbolGroup);

    // Search/filter
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Search symbol..."));
    m_searchEdit->setClearButtonEnabled(true);
    symbolLayout->addWidget(m_searchEdit);

    // Instrument combo
    m_instrumentCombo = new QComboBox;
    m_instrumentCombo->setEditable(false);
    m_instrumentCombo->setMinimumWidth(150);
    symbolLayout->addWidget(m_instrumentCombo);

    // Tick info
    m_tickInfoLabel = new QLabel(tr("No data loaded"));
    m_tickInfoLabel->setStyleSheet("color: #888888;");
    symbolLayout->addWidget(m_tickInfoLabel);

    mainLayout->addWidget(m_symbolGroup);

    // Date range group
    m_dateGroup = new QGroupBox(tr("Date Range"));
    QFormLayout *dateLayout = new QFormLayout(m_dateGroup);

    m_startDateEdit = new QDateEdit;
    m_startDateEdit->setCalendarPopup(true);
    m_startDateEdit->setDate(QDate(2025, 1, 1));
    m_startDateEdit->setDisplayFormat("yyyy-MM-dd");
    dateLayout->addRow(tr("Start:"), m_startDateEdit);

    m_endDateEdit = new QDateEdit;
    m_endDateEdit->setCalendarPopup(true);
    m_endDateEdit->setDate(QDate(2026, 1, 29));
    m_endDateEdit->setDisplayFormat("yyyy-MM-dd");
    dateLayout->addRow(tr("End:"), m_endDateEdit);

    mainLayout->addWidget(m_dateGroup);

    // Download button
    m_downloadButton = new QPushButton(tr("Download Tick Data"));
    m_downloadButton->setEnabled(false);
    mainLayout->addWidget(m_downloadButton);

    mainLayout->addStretch();

    // Connections
    connect(m_instrumentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstrumentSelector::onInstrumentChanged);
    connect(m_downloadButton, &QPushButton::clicked,
            this, &InstrumentSelector::onDownloadClicked);
    connect(m_startDateEdit, &QDateEdit::dateChanged,
            this, &InstrumentSelector::dateRangeChanged);
    connect(m_endDateEdit, &QDateEdit::dateChanged,
            this, &InstrumentSelector::dateRangeChanged);

    // Search filter
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        // Simple filter - hide non-matching items
        for (int i = 0; i < m_instrumentCombo->count(); ++i) {
            if (m_instrumentCombo->itemText(i).contains(text, Qt::CaseInsensitive)) {
                m_instrumentCombo->setCurrentIndex(i);
                break;
            }
        }
    });
}

void InstrumentSelector::setInstruments(const QStringList &instruments)
{
    m_instrumentCombo->clear();
    m_instrumentCombo->addItems(instruments);

    // Enable download button if instruments available
    m_downloadButton->setEnabled(!instruments.isEmpty());

    // Set up completer for search
    QCompleter *completer = new QCompleter(instruments, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_searchEdit->setCompleter(completer);

    // Select common instruments by default
    int xauIndex = instruments.indexOf("XAUUSD");
    if (xauIndex >= 0) {
        m_instrumentCombo->setCurrentIndex(xauIndex);
    }
}

QString InstrumentSelector::selectedInstrument() const
{
    return m_instrumentCombo->currentText();
}

QDate InstrumentSelector::startDate() const
{
    return m_startDateEdit->date();
}

QDate InstrumentSelector::endDate() const
{
    return m_endDateEdit->date();
}

void InstrumentSelector::onInstrumentChanged(int index)
{
    Q_UNUSED(index);
    QString symbol = m_instrumentCombo->currentText();
    emit instrumentChanged(symbol);
    updateTickInfo();
}

void InstrumentSelector::onDownloadClicked()
{
    emit downloadRequested(
        m_instrumentCombo->currentText(),
        m_startDateEdit->date(),
        m_endDateEdit->date()
    );
}

void InstrumentSelector::updateTickInfo()
{
    // TODO: Query tick data availability from file system
    QString symbol = m_instrumentCombo->currentText();
    if (symbol.isEmpty()) {
        m_tickInfoLabel->setText(tr("No data loaded"));
        return;
    }

    // Check if tick file exists
    QString tickFile = QString("validation/Grid/%1_TICKS_2025_EXTENDED.csv").arg(symbol);
    QFileInfo fileInfo(tickFile);
    if (fileInfo.exists()) {
        qint64 size = fileInfo.size();
        QString sizeStr;
        if (size > 1024*1024*1024) {
            sizeStr = QString("%1 GB").arg(size / (1024.0*1024*1024), 0, 'f', 2);
        } else if (size > 1024*1024) {
            sizeStr = QString("%1 MB").arg(size / (1024.0*1024), 0, 'f', 1);
        } else {
            sizeStr = QString("%1 KB").arg(size / 1024.0, 0, 'f', 0);
        }
        m_tickInfoLabel->setText(QString("Data: %1").arg(sizeStr));
        m_tickInfoLabel->setStyleSheet("color: #00ff00;");
    } else {
        m_tickInfoLabel->setText(tr("No data available"));
        m_tickInfoLabel->setStyleSheet("color: #ff8800;");
    }
}
