#include "features/block_model/engine/BlockModelFieldUtils.h"

#include <QSet>

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

QStringList BlockModelFieldNames(const visor::datamine::InternalBlockModelInfo &info)
{
    QStringList fields = {
        "IJK", "I", "J", "K", "XC", "YC", "ZC", "XINC", "YINC", "ZINC", "VOLUME"};
    for (const auto &field : info.sourceHeader.fields) {
        const QString name = QString::fromStdString(field.name);
        if (!name.isEmpty() && !fields.contains(name, Qt::CaseInsensitive)) {
            fields << name;
        }
    }
    fields.sort(Qt::CaseInsensitive);
    return fields;
}

QStringList UniqueBlockModelFieldValues(const QString &internalPath, const QString &fieldName)
{
    QStringList values;
    QSet<QString> seen;
    if (internalPath.isEmpty() || fieldName.isEmpty()) {
        return values;
    }
    try {
        visor::datamine::DmBlockModelImporter::forEachInternalCellValue(
            internalPath.toStdString(),
            fieldName.toStdString(),
            [&](const visor::datamine::BlockCell &cell, double value, bool hasValue, std::int64_t, std::int64_t) {
                QString text;
                const QString targetField = fieldName.trimmed();
                for (const auto &entry : cell.alphaAttributes) {
                    if (QString::fromStdString(entry.first).compare(targetField, Qt::CaseInsensitive) == 0) {
                        text = QString::fromStdString(entry.second).trimmed();
                        break;
                    }
                }
                if (text.isEmpty() && hasValue) {
                    const double rounded = std::round(value);
                    text = std::abs(value - rounded) < 1e-9
                        ? QString::number(static_cast<qlonglong>(rounded))
                        : QString::number(value, 'g', 10);
                }
                if (!text.isEmpty() && !seen.contains(text)) {
                    seen.insert(text);
                    values << text;
                }
                return values.size() < 200;
            },
            std::nullopt,
            true);
    } catch (...) {
    }
    values.sort(Qt::CaseInsensitive);
    return values;
}

bool BlockModelBounds(const visor::datamine::InternalBlockModelInfo &info, double bounds[6])
{
    const auto &p = info.prototype;
    if (p.nx <= 0 || p.ny <= 0 || p.nz <= 0 ||
        p.parentX <= 0.0 || p.parentY <= 0.0 || p.parentZ <= 0.0) {
        return false;
    }
    bounds[0] = p.xOrigin;
    bounds[1] = p.xOrigin + static_cast<double>(p.nx) * p.parentX;
    bounds[2] = p.yOrigin;
    bounds[3] = p.yOrigin + static_cast<double>(p.ny) * p.parentY;
    bounds[4] = p.zOrigin;
    bounds[5] = p.zOrigin + static_cast<double>(p.nz) * p.parentZ;
    if (bounds[0] > bounds[1]) {
        std::swap(bounds[0], bounds[1]);
    }
    if (bounds[2] > bounds[3]) {
        std::swap(bounds[2], bounds[3]);
    }
    if (bounds[4] > bounds[5]) {
        std::swap(bounds[4], bounds[5]);
    }
    return true;
}
