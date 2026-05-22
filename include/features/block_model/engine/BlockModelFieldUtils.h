#pragma once

#include "io/DatamineImporter.h"

#include <QString>
#include <QStringList>

QStringList BlockModelFieldNames(const visor::datamine::InternalBlockModelInfo &info);
QStringList UniqueBlockModelFieldValues(const QString &internalPath, const QString &fieldName);
bool BlockModelBounds(const visor::datamine::InternalBlockModelInfo &info, double bounds[6]);
