#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct EconomicModelDefinition
{
    struct Variable
    {
        QString name;
        QString formula;
    };

    struct Destination
    {
        bool enabled = true;
        QString name;
        double processingCost = 0.0;
        QStringList productValues;
    };

    struct RockTypeSettings
    {
        QString rockType;
        double dilution = 0.0;
        double miningRecovery = 100.0;
        double miningCost = 0.0;
        QList<Destination> destinations;
    };

    QString name;
    QString blockModelLayerName;
    QHash<QString, QString> fieldRoles;
    QHash<QString, QString> fieldUnits;
    QHash<QString, QStringList> uniqueFieldValues;
    QList<Variable> variables;
    QList<RockTypeSettings> rockTypeSettings;
    bool profitModel = false;
    QString profitField;
    QString generatedInternalPath;
    QString generatedStoredRelativePath;
    qint64 generatedCellCount = 0;
};

inline QString EconomicModelKey(const QString &blockModelLayerName, const QString &economicModelName)
{
    return blockModelLayerName + "/" + economicModelName;
}
