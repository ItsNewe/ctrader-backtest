/**
 * InstrumentSelector Widget
 * Allows selecting trading instruments from MT5
 */

#ifndef INSTRUMENTSELECTOR_H
#define INSTRUMENTSELECTOR_H

#include <QWidget>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QDateEdit;
class QPushButton;
class QGroupBox;
class QLabel;

class InstrumentSelector : public QWidget
{
    Q_OBJECT

public:
    explicit InstrumentSelector(QWidget *parent = nullptr);

    void setInstruments(const QStringList &instruments);
    QString selectedInstrument() const;
    QDate startDate() const;
    QDate endDate() const;

signals:
    void instrumentChanged(const QString &symbol);
    void dateRangeChanged();
    void downloadRequested(const QString &symbol, const QDate &start, const QDate &end);

private slots:
    void onInstrumentChanged(int index);
    void onDownloadClicked();
    void updateTickInfo();

private:
    void setupUI();

    QComboBox *m_instrumentCombo;
    QLineEdit *m_searchEdit;
    QDateEdit *m_startDateEdit;
    QDateEdit *m_endDateEdit;
    QPushButton *m_downloadButton;
    QLabel *m_tickInfoLabel;
    QGroupBox *m_symbolGroup;
    QGroupBox *m_dateGroup;
};

#endif // INSTRUMENTSELECTOR_H
