/**
 * ParameterGrid Widget - Strategy parameter configuration
 */

#ifndef PARAMETERGRID_H
#define PARAMETERGRID_H

#include <QWidget>
#include <QMap>
#include <QVariant>

class QFormLayout;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QScrollArea;

struct ParameterDef {
    QString name;
    QString displayName;
    QString description;
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    QVariant step;
    enum Type { Double, Int, Bool, Enum } type;
    QStringList enumValues;  // For Enum type
    bool optimizable;        // Can be optimized
};

struct OptimizationRange {
    double start;
    double stop;
    double step;
    bool enabled;
};

class ParameterGrid : public QWidget
{
    Q_OBJECT

public:
    explicit ParameterGrid(QWidget *parent = nullptr);

    // Parameter definition
    void addParameter(const ParameterDef &param);
    void addParameterGroup(const QString &groupName, const QVector<ParameterDef> &params);
    void clearParameters();

    // Get/Set values
    QVariant getParameterValue(const QString &name) const;
    void setParameterValue(const QString &name, const QVariant &value);
    QMap<QString, QVariant> getAllParameters() const;
    void setAllParameters(const QMap<QString, QVariant> &params);

    // Optimization ranges
    OptimizationRange getOptimizationRange(const QString &name) const;
    void setOptimizationRange(const QString &name, const OptimizationRange &range);
    QMap<QString, OptimizationRange> getAllOptimizationRanges() const;
    bool hasOptimizableParameters() const;

    // Presets
    void loadPreset(const QString &filePath);
    void savePreset(const QString &filePath);

signals:
    void parameterChanged(const QString &name, const QVariant &value);
    void optimizationRangeChanged(const QString &name, const OptimizationRange &range);
    void presetLoaded(const QString &filePath);

public slots:
    void resetToDefaults();
    void toggleOptimizationMode(bool enabled);

private:
    void setupUI();
    QWidget* createParameterWidget(const ParameterDef &param);
    QWidget* createOptimizationWidget(const ParameterDef &param);
    void updateOptimizationVisibility(bool visible);

    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QFormLayout *m_mainLayout;

    QMap<QString, ParameterDef> m_parameterDefs;
    QMap<QString, QWidget*> m_valueWidgets;
    QMap<QString, QWidget*> m_optimizationWidgets;
    QMap<QString, QGroupBox*> m_groupBoxes;

    bool m_optimizationMode;
};

#endif // PARAMETERGRID_H
