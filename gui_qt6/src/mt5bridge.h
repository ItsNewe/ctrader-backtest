/**
 * MT5Bridge - Interface to MetaTrader 5 via Python API
 */

#ifndef MT5BRIDGE_H
#define MT5BRIDGE_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QVector>
#include <QJsonObject>

struct MT5AccountInfo {
    QString login;
    QString server;
    QString company;
    QString name;
    double balance;
    double equity;
    double margin;
    double freeMargin;
    int leverage;
    QString currency;
    bool tradeAllowed;
};

struct MT5SymbolInfo {
    QString name;
    QString description;
    QString path;
    QString baseCurrency;
    QString profitCurrency;
    QString marginCurrency;

    double bid;
    double ask;
    double point;
    int digits;
    double tickSize;
    double tickValue;
    double contractSize;
    double volumeMin;
    double volumeMax;
    double volumeStep;

    double swapLong;
    double swapShort;
    int swapMode;
    int swap3Days;

    double marginInitial;
    double marginMaintenance;

    bool visible;
    bool tradeable;
};

struct MT5TickData {
    qint64 timestamp;
    double bid;
    double ask;
    double last;
    qint64 volume;
    int flags;
};

class MT5Bridge : public QObject
{
    Q_OBJECT

public:
    explicit MT5Bridge(QObject *parent = nullptr);
    ~MT5Bridge();

    // Connection
    bool isConnected() const { return m_connected; }
    void setTerminalPath(const QString &path) { m_terminalPath = path; }
    QString terminalPath() const { return m_terminalPath; }

    // Account info
    MT5AccountInfo accountInfo() const { return m_accountInfo; }

    // Symbol info
    QStringList availableSymbols() const;
    MT5SymbolInfo symbolInfo(const QString &symbol) const;
    bool hasSymbol(const QString &symbol) const;

public slots:
    // Async operations - results via signals
    void connect();
    void disconnect();
    void loadInstruments();
    void loadSymbolInfo(const QString &symbol);
    void downloadTickData(const QString &symbol,
                          const QDateTime &from,
                          const QDateTime &to,
                          const QString &outputPath);
    void cancelDownload();

signals:
    void connected(bool success);
    void disconnected();
    void instrumentsLoaded(const QStringList &instruments);
    void symbolInfoLoaded(const QString &symbol, const MT5SymbolInfo &info);
    void tickDownloadProgress(const QString &symbol, int percent, qint64 ticksDownloaded);
    void tickDownloadComplete(const QString &symbol, const QString &filePath, qint64 totalTicks);
    void tickDownloadError(const QString &symbol, const QString &error);
    void error(const QString &message);

private slots:
    void onProcessOutput();
    void onProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void startPythonBridge();
    void sendCommand(const QString &command, const QJsonObject &params = QJsonObject());
    void parseResponse(const QByteArray &data);
    void parseAccountInfo(const QJsonObject &json);
    void parseSymbolInfo(const QJsonObject &json);
    void parseInstrumentList(const QJsonObject &json);
    void parseTickProgress(const QJsonObject &json);

    QProcess *m_process;
    QString m_terminalPath;
    QString m_pythonPath;
    QString m_bridgeScript;

    bool m_connected;
    MT5AccountInfo m_accountInfo;
    QMap<QString, MT5SymbolInfo> m_symbols;
    QStringList m_instrumentList;

    QString m_currentDownloadSymbol;
    bool m_downloadCancelled;
};

#endif // MT5BRIDGE_H
