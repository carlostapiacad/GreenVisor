#pragma once

#include "features/pit_optimization/models/EconomicModelDefinition.h"

#include <QHash>
#include <QList>
#include <QString>

double EvaluateEconomicFormulaValue(
    const QString &text,
    const QHash<QString, double> &context,
    const QList<EconomicModelDefinition::Variable> &variables,
    bool *ok = nullptr);

bool EvaluateEconomicDestinationValue(
    const QString &text,
    const QHash<QString, double> &context,
    const QList<EconomicModelDefinition::Variable> &variables,
    double *value);
