#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

struct PitOptimizationDestinationReport
{
    double tonnes = 0.0;
    QHash<QString, double> productWeightedSums;
};

struct PitOptimizationReportRow
{
    QString label;
    double revenueFactor = 0.0;
    double oreTonnes = 0.0;
    double wasteTonnes = 0.0;
    double totalTonnes = 0.0;
    QHash<QString, double> oreProductWeightedSums;
    QHash<QString, PitOptimizationDestinationReport> destinations;
};

struct PitOptimizationReport
{
    QString economicModelName;
    QStringList products;
    QHash<QString, QString> productUnits;
    QStringList destinationNames;
    QList<PitOptimizationReportRow> economicModelRows;
    QList<PitOptimizationReportRow> rows;
};

class PitOptimizationReportDialog : public QDialog
{
public:
    explicit PitOptimizationReportDialog(const PitOptimizationReport &report, QWidget *parent = nullptr);
};
