#pragma once

#include <QIcon>
#include <QString>

class QSize;
class LayerNode;

QString EnsureProjectExtension(const QString &path);
QString SanitizeName(const QString &name);
QString ProjectDataDir(const QString &projectPath);
QString BlockModelCacheDir(const QString &projectPath);
QString EconomicModelCacheDir(const QString &projectPath);
QString FindAssetPath(const QString &relativePath);
QIcon MakeTrimmedIcon(const QString &imagePath, const QSize &targetSize);
QString UniqueChildName(LayerNode *parent, const QString &baseName);
