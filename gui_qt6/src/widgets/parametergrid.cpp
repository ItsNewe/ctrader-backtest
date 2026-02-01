/**
 * ParameterGrid Widget Implementation
 */

#include "parametergrid.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ParameterGrid::ParameterGrid(QWidget *parent)
    : QWidget(parent)
    , m_optimizationMode(false)
{
    setupUI();
}

void ParameterGrid::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Header with mode toggle
    QHBoxLayout *headerLayout = new QHBoxLayout;
    QLabel *titleLabel = new QLabel(tr("Strategy Parameters"));
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px;");

    QCheckBox *optimizeCheck = new QCheckBox(tr("Optimization Mode"));
    connect(optimizeCheck, &QCheckBox::toggled, this, &ParameterGrid::toggleOptimizationMode);

    QPushButton *resetBtn = new QPushButton(tr("Reset"));
    connect(resetBtn, &QPushButton::clicked, this, &ParameterGrid::resetToDefaults);

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(optimizeCheck);
    headerLayout->addWidget(resetBtn);

    mainLayout->addLayout(headerLayout);

    // Scroll area for parameters
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentWidget = new QWidget;
    m_mainLayout = new QFormLayout(m_contentWidget);
    m_mainLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);
}

void ParameterGrid::addParameter(const ParameterDef &param)
{
    m_parameterDefs[param.name] = param;

    QWidget *valueWidget = createParameterWidget(param);
    m_valueWidgets[param.name] = valueWidget;

    QWidget *optWidget = nullptr;
    if (param.optimizable) {
        optWidget = createOptimizationWidget(param);
        m_optimizationWidgets[param.name] = optWidget;
        optWidget->setVisible(m_optimizationMode);
    }

    // Create row with label and widgets
    QHBoxLayout *rowLayout = new QHBoxLayout;
    rowLayout->addWidget(valueWidget);
    if (optWidget) {
        rowLayout->addWidget(optWidget);
    }

    QWidget *rowWidget = new QWidget;
    rowWidget->setLayout(rowLayout);

    QString labelText = param.displayName.isEmpty() ? param.name : param.displayName;
    m_mainLayout->addRow(labelText + ":", rowWidget);
}

void ParameterGrid::addParameterGroup(const QString &groupName, const QVector<ParameterDef> &params)
{
    QGroupBox *groupBox = new QGroupBox(groupName);
    QFormLayout *groupLayout = new QFormLayout(groupBox);

    for (const auto &param : params) {
        m_parameterDefs[param.name] = param;

        QWidget *valueWidget = createParameterWidget(param);
        m_valueWidgets[param.name] = valueWidget;

        QWidget *optWidget = nullptr;
        if (param.optimizable) {
            optWidget = createOptimizationWidget(param);
            m_optimizationWidgets[param.name] = optWidget;
            optWidget->setVisible(m_optimizationMode);
        }

        QHBoxLayout *rowLayout = new QHBoxLayout;
        rowLayout->addWidget(valueWidget);
        if (optWidget) {
            rowLayout->addWidget(optWidget);
        }

        QWidget *rowWidget = new QWidget;
        rowWidget->setLayout(rowLayout);

        QString labelText = param.displayName.isEmpty() ? param.name : param.displayName;
        groupLayout->addRow(labelText + ":", rowWidget);
    }

    m_groupBoxes[groupName] = groupBox;
    m_mainLayout->addRow(groupBox);
}

QWidget* ParameterGrid::createParameterWidget(const ParameterDef &param)
{
    switch (param.type) {
    case ParameterDef::Double: {
        QDoubleSpinBox *spin = new QDoubleSpinBox;
        spin->setRange(param.minValue.toDouble(), param.maxValue.toDouble());
        spin->setSingleStep(param.step.toDouble());
        spin->setValue(param.defaultValue.toDouble());
        spin->setDecimals(4);
        spin->setToolTip(param.description);

        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, name = param.name](double value) {
            emit parameterChanged(name, value);
        });

        return spin;
    }

    case ParameterDef::Int: {
        QSpinBox *spin = new QSpinBox;
        spin->setRange(param.minValue.toInt(), param.maxValue.toInt());
        spin->setSingleStep(param.step.toInt());
        spin->setValue(param.defaultValue.toInt());
        spin->setToolTip(param.description);

        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, name = param.name](int value) {
            emit parameterChanged(name, value);
        });

        return spin;
    }

    case ParameterDef::Bool: {
        QCheckBox *check = new QCheckBox;
        check->setChecked(param.defaultValue.toBool());
        check->setToolTip(param.description);

        connect(check, &QCheckBox::toggled,
                this, [this, name = param.name](bool value) {
            emit parameterChanged(name, value);
        });

        return check;
    }

    case ParameterDef::Enum: {
        QComboBox *combo = new QComboBox;
        combo->addItems(param.enumValues);
        combo->setCurrentText(param.defaultValue.toString());
        combo->setToolTip(param.description);

        connect(combo, &QComboBox::currentTextChanged,
                this, [this, name = param.name](const QString &value) {
            emit parameterChanged(name, value);
        });

        return combo;
    }
    }

    return new QWidget;
}

QWidget* ParameterGrid::createOptimizationWidget(const ParameterDef &param)
{
    QWidget *container = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    QCheckBox *enableCheck = new QCheckBox;
    enableCheck->setToolTip(tr("Enable optimization for this parameter"));

    QDoubleSpinBox *startSpin = new QDoubleSpinBox;
    startSpin->setRange(param.minValue.toDouble(), param.maxValue.toDouble());
    startSpin->setValue(param.minValue.toDouble());
    startSpin->setDecimals(4);
    startSpin->setPrefix(tr("Start: "));

    QDoubleSpinBox *stopSpin = new QDoubleSpinBox;
    stopSpin->setRange(param.minValue.toDouble(), param.maxValue.toDouble());
    stopSpin->setValue(param.maxValue.toDouble());
    stopSpin->setDecimals(4);
    stopSpin->setPrefix(tr("Stop: "));

    QDoubleSpinBox *stepSpin = new QDoubleSpinBox;
    stepSpin->setRange(0.0001, 1000.0);
    stepSpin->setValue(param.step.toDouble());
    stepSpin->setDecimals(4);
    stepSpin->setPrefix(tr("Step: "));

    layout->addWidget(enableCheck);
    layout->addWidget(startSpin);
    layout->addWidget(stopSpin);
    layout->addWidget(stepSpin);

    // Connect to emit optimization range changes
    auto emitChange = [this, name = param.name, enableCheck, startSpin, stopSpin, stepSpin]() {
        OptimizationRange range;
        range.enabled = enableCheck->isChecked();
        range.start = startSpin->value();
        range.stop = stopSpin->value();
        range.step = stepSpin->value();
        emit optimizationRangeChanged(name, range);
    };

    connect(enableCheck, &QCheckBox::toggled, this, emitChange);
    connect(startSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitChange);
    connect(stopSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitChange);
    connect(stepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitChange);

    return container;
}

void ParameterGrid::clearParameters()
{
    // Clear all widgets
    while (m_mainLayout->rowCount() > 0) {
        m_mainLayout->removeRow(0);
    }

    m_parameterDefs.clear();
    m_valueWidgets.clear();
    m_optimizationWidgets.clear();
    m_groupBoxes.clear();
}

QVariant ParameterGrid::getParameterValue(const QString &name) const
{
    if (!m_valueWidgets.contains(name) || !m_parameterDefs.contains(name)) {
        return QVariant();
    }

    QWidget *widget = m_valueWidgets[name];
    const ParameterDef &param = m_parameterDefs[name];

    switch (param.type) {
    case ParameterDef::Double:
        return qobject_cast<QDoubleSpinBox*>(widget)->value();
    case ParameterDef::Int:
        return qobject_cast<QSpinBox*>(widget)->value();
    case ParameterDef::Bool:
        return qobject_cast<QCheckBox*>(widget)->isChecked();
    case ParameterDef::Enum:
        return qobject_cast<QComboBox*>(widget)->currentText();
    }

    return QVariant();
}

void ParameterGrid::setParameterValue(const QString &name, const QVariant &value)
{
    if (!m_valueWidgets.contains(name) || !m_parameterDefs.contains(name)) {
        return;
    }

    QWidget *widget = m_valueWidgets[name];
    const ParameterDef &param = m_parameterDefs[name];

    switch (param.type) {
    case ParameterDef::Double:
        qobject_cast<QDoubleSpinBox*>(widget)->setValue(value.toDouble());
        break;
    case ParameterDef::Int:
        qobject_cast<QSpinBox*>(widget)->setValue(value.toInt());
        break;
    case ParameterDef::Bool:
        qobject_cast<QCheckBox*>(widget)->setChecked(value.toBool());
        break;
    case ParameterDef::Enum:
        qobject_cast<QComboBox*>(widget)->setCurrentText(value.toString());
        break;
    }
}

QMap<QString, QVariant> ParameterGrid::getAllParameters() const
{
    QMap<QString, QVariant> params;
    for (const QString &name : m_parameterDefs.keys()) {
        params[name] = getParameterValue(name);
    }
    return params;
}

void ParameterGrid::setAllParameters(const QMap<QString, QVariant> &params)
{
    for (auto it = params.begin(); it != params.end(); ++it) {
        setParameterValue(it.key(), it.value());
    }
}

OptimizationRange ParameterGrid::getOptimizationRange(const QString &name) const
{
    OptimizationRange range = {0, 0, 0, false};

    if (!m_optimizationWidgets.contains(name)) {
        return range;
    }

    QWidget *container = m_optimizationWidgets[name];
    QCheckBox *enableCheck = container->findChild<QCheckBox*>();
    QList<QDoubleSpinBox*> spins = container->findChildren<QDoubleSpinBox*>();

    if (enableCheck && spins.size() >= 3) {
        range.enabled = enableCheck->isChecked();
        range.start = spins[0]->value();
        range.stop = spins[1]->value();
        range.step = spins[2]->value();
    }

    return range;
}

void ParameterGrid::setOptimizationRange(const QString &name, const OptimizationRange &range)
{
    if (!m_optimizationWidgets.contains(name)) {
        return;
    }

    QWidget *container = m_optimizationWidgets[name];
    QCheckBox *enableCheck = container->findChild<QCheckBox*>();
    QList<QDoubleSpinBox*> spins = container->findChildren<QDoubleSpinBox*>();

    if (enableCheck && spins.size() >= 3) {
        enableCheck->setChecked(range.enabled);
        spins[0]->setValue(range.start);
        spins[1]->setValue(range.stop);
        spins[2]->setValue(range.step);
    }
}

QMap<QString, OptimizationRange> ParameterGrid::getAllOptimizationRanges() const
{
    QMap<QString, OptimizationRange> ranges;
    for (const QString &name : m_optimizationWidgets.keys()) {
        ranges[name] = getOptimizationRange(name);
    }
    return ranges;
}

bool ParameterGrid::hasOptimizableParameters() const
{
    for (const auto &range : getAllOptimizationRanges()) {
        if (range.enabled) return true;
    }
    return false;
}

void ParameterGrid::resetToDefaults()
{
    for (const QString &name : m_parameterDefs.keys()) {
        setParameterValue(name, m_parameterDefs[name].defaultValue);
    }
}

void ParameterGrid::toggleOptimizationMode(bool enabled)
{
    m_optimizationMode = enabled;
    updateOptimizationVisibility(enabled);
}

void ParameterGrid::updateOptimizationVisibility(bool visible)
{
    for (QWidget *widget : m_optimizationWidgets.values()) {
        widget->setVisible(visible);
    }
}

void ParameterGrid::loadPreset(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();

    QJsonObject params = root["parameters"].toObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
        setParameterValue(it.key(), it.value().toVariant());
    }

    QJsonObject ranges = root["optimization"].toObject();
    for (auto it = ranges.begin(); it != ranges.end(); ++it) {
        QJsonObject rangeObj = it.value().toObject();
        OptimizationRange range;
        range.enabled = rangeObj["enabled"].toBool();
        range.start = rangeObj["start"].toDouble();
        range.stop = rangeObj["stop"].toDouble();
        range.step = rangeObj["step"].toDouble();
        setOptimizationRange(it.key(), range);
    }

    emit presetLoaded(filePath);
}

void ParameterGrid::savePreset(const QString &filePath)
{
    QJsonObject root;

    // Save parameters
    QJsonObject params;
    for (const QString &name : m_parameterDefs.keys()) {
        QVariant value = getParameterValue(name);
        params[name] = QJsonValue::fromVariant(value);
    }
    root["parameters"] = params;

    // Save optimization ranges
    QJsonObject ranges;
    for (const QString &name : m_optimizationWidgets.keys()) {
        OptimizationRange range = getOptimizationRange(name);
        QJsonObject rangeObj;
        rangeObj["enabled"] = range.enabled;
        rangeObj["start"] = range.start;
        rangeObj["stop"] = range.stop;
        rangeObj["step"] = range.step;
        ranges[name] = rangeObj;
    }
    root["optimization"] = ranges;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}
