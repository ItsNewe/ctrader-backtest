/**
 * MT5Bridge Implementation - Python API integration
 */

#include "mt5bridge.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

MT5Bridge::MT5Bridge(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_connected(false)
    , m_downloadCancelled(false)
{
    // Default paths
    m_pythonPath = "python";  // Assumes python is in PATH
    m_bridgeScript = QCoreApplication::applicationDirPath() + "/scripts/mt5_bridge.py";

    // Check for bundled Python script
    if (!QFileInfo::exists(m_bridgeScript)) {
        // Try relative to source
        m_bridgeScript = QDir::currentPath() + "/../bridge/mt5_bridge.py";
    }
}

MT5Bridge::~MT5Bridge()
{
    disconnect();
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
        delete m_process;
    }
}

void MT5Bridge::startPythonBridge()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
        delete m_process;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &MT5Bridge::onProcessOutput);
    connect(m_process, &QProcess::errorOccurred,
            this, &MT5Bridge::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MT5Bridge::onProcessFinished);

    QStringList args;
    args << m_bridgeScript;
    if (!m_terminalPath.isEmpty()) {
        args << "--terminal" << m_terminalPath;
    }

    m_process->start(m_pythonPath, args);
}

void MT5Bridge::connect()
{
    startPythonBridge();

    if (!m_process->waitForStarted(5000)) {
        emit error(tr("Failed to start Python bridge"));
        emit connected(false);
        return;
    }

    sendCommand("connect");
}

void MT5Bridge::disconnect()
{
    if (m_process && m_process->state() == QProcess::Running) {
        sendCommand("disconnect");
        m_process->waitForFinished(2000);
    }
    m_connected = false;
    emit disconnected();
}

void MT5Bridge::loadInstruments()
{
    if (!m_connected) {
        emit error(tr("Not connected to MT5"));
        return;
    }
    sendCommand("get_symbols");
}

void MT5Bridge::loadSymbolInfo(const QString &symbol)
{
    if (!m_connected) {
        emit error(tr("Not connected to MT5"));
        return;
    }

    QJsonObject params;
    params["symbol"] = symbol;
    sendCommand("get_symbol_info", params);
}

void MT5Bridge::downloadTickData(const QString &symbol,
                                  const QDateTime &from,
                                  const QDateTime &to,
                                  const QString &outputPath)
{
    if (!m_connected) {
        emit error(tr("Not connected to MT5"));
        return;
    }

    m_currentDownloadSymbol = symbol;
    m_downloadCancelled = false;

    QJsonObject params;
    params["symbol"] = symbol;
    params["from_date"] = from.toString(Qt::ISODate);
    params["to_date"] = to.toString(Qt::ISODate);
    params["output_path"] = outputPath;
    sendCommand("download_ticks", params);
}

void MT5Bridge::cancelDownload()
{
    m_downloadCancelled = true;
    sendCommand("cancel_download");
}

QStringList MT5Bridge::availableSymbols() const
{
    return m_instrumentList;
}

MT5SymbolInfo MT5Bridge::symbolInfo(const QString &symbol) const
{
    return m_symbols.value(symbol, MT5SymbolInfo());
}

bool MT5Bridge::hasSymbol(const QString &symbol) const
{
    return m_symbols.contains(symbol);
}

void MT5Bridge::sendCommand(const QString &command, const QJsonObject &params)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        emit error(tr("Python bridge not running"));
        return;
    }

    QJsonObject msg;
    msg["command"] = command;
    if (!params.isEmpty()) {
        msg["params"] = params;
    }

    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    m_process->write(data);
}

void MT5Bridge::onProcessOutput()
{
    while (m_process->canReadLine()) {
        QByteArray line = m_process->readLine().trimmed();
        if (!line.isEmpty()) {
            parseResponse(line);
        }
    }
}

void MT5Bridge::onProcessError()
{
    emit error(tr("Python bridge error: %1").arg(m_process->errorString()));
}

void MT5Bridge::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(status)

    m_connected = false;
    emit disconnected();
}

void MT5Bridge::parseResponse(const QByteArray &data)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse MT5 response:" << parseError.errorString();
        return;
    }

    QJsonObject root = doc.object();
    QString type = root["type"].toString();
    bool success = root["success"].toBool();

    if (type == "connect") {
        m_connected = success;
        if (success) {
            parseAccountInfo(root["account"].toObject());
        }
        emit connected(success);
    }
    else if (type == "symbols") {
        if (success) {
            parseInstrumentList(root);
        }
    }
    else if (type == "symbol_info") {
        if (success) {
            parseSymbolInfo(root["info"].toObject());
        }
    }
    else if (type == "tick_progress") {
        parseTickProgress(root);
    }
    else if (type == "tick_complete") {
        QString symbol = root["symbol"].toString();
        QString path = root["path"].toString();
        qint64 totalTicks = root["total_ticks"].toVariant().toLongLong();
        emit tickDownloadComplete(symbol, path, totalTicks);
    }
    else if (type == "tick_error") {
        QString symbol = root["symbol"].toString();
        QString errorMsg = root["error"].toString();
        emit tickDownloadError(symbol, errorMsg);
    }
    else if (type == "error") {
        emit error(root["message"].toString());
    }
}

void MT5Bridge::parseAccountInfo(const QJsonObject &json)
{
    m_accountInfo.login = json["login"].toString();
    m_accountInfo.server = json["server"].toString();
    m_accountInfo.company = json["company"].toString();
    m_accountInfo.name = json["name"].toString();
    m_accountInfo.balance = json["balance"].toDouble();
    m_accountInfo.equity = json["equity"].toDouble();
    m_accountInfo.margin = json["margin"].toDouble();
    m_accountInfo.freeMargin = json["free_margin"].toDouble();
    m_accountInfo.leverage = json["leverage"].toInt();
    m_accountInfo.currency = json["currency"].toString();
    m_accountInfo.tradeAllowed = json["trade_allowed"].toBool();
}

void MT5Bridge::parseSymbolInfo(const QJsonObject &json)
{
    MT5SymbolInfo info;
    info.name = json["name"].toString();
    info.description = json["description"].toString();
    info.path = json["path"].toString();
    info.baseCurrency = json["base_currency"].toString();
    info.profitCurrency = json["profit_currency"].toString();
    info.marginCurrency = json["margin_currency"].toString();

    info.bid = json["bid"].toDouble();
    info.ask = json["ask"].toDouble();
    info.point = json["point"].toDouble();
    info.digits = json["digits"].toInt();
    info.tickSize = json["tick_size"].toDouble();
    info.tickValue = json["tick_value"].toDouble();
    info.contractSize = json["contract_size"].toDouble();
    info.volumeMin = json["volume_min"].toDouble();
    info.volumeMax = json["volume_max"].toDouble();
    info.volumeStep = json["volume_step"].toDouble();

    info.swapLong = json["swap_long"].toDouble();
    info.swapShort = json["swap_short"].toDouble();
    info.swapMode = json["swap_mode"].toInt();
    info.swap3Days = json["swap_3days"].toInt();

    info.marginInitial = json["margin_initial"].toDouble();
    info.marginMaintenance = json["margin_maintenance"].toDouble();

    info.visible = json["visible"].toBool();
    info.tradeable = json["tradeable"].toBool();

    m_symbols[info.name] = info;
    emit symbolInfoLoaded(info.name, info);
}

void MT5Bridge::parseInstrumentList(const QJsonObject &json)
{
    m_instrumentList.clear();

    QJsonArray symbols = json["symbols"].toArray();
    for (const auto &sym : symbols) {
        m_instrumentList.append(sym.toString());
    }

    emit instrumentsLoaded(m_instrumentList);
}

void MT5Bridge::parseTickProgress(const QJsonObject &json)
{
    QString symbol = json["symbol"].toString();
    int percent = json["percent"].toInt();
    qint64 ticks = json["ticks_downloaded"].toVariant().toLongLong();
    emit tickDownloadProgress(symbol, percent, ticks);
}
