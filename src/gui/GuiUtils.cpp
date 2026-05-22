#include "gui/GuiUtils.h"

#include "gui/LayerModel.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QSet>
#include <QSize>

QString EnsureProjectExtension(const QString &path)
{
    if (path.endsWith(".gvs", Qt::CaseInsensitive)) {
        return path;
    }
    return path + ".gvs";
}

QString SanitizeName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (QChar ch : name) {
        if (ch.isLetterOrNumber()) {
            out.append(ch);
        } else {
            out.append('_');
        }
    }
    if (out.isEmpty()) {
        out = "layer";
    }
    return out;
}

QString ProjectDataDir(const QString &projectPath)
{
    QFileInfo info(projectPath);
    const QString baseName = info.completeBaseName();
    return info.dir().filePath(baseName + "_data");
}

QString BlockModelCacheDir(const QString &projectPath)
{
    if (!projectPath.isEmpty()) {
        return QDir(ProjectDataDir(projectPath)).filePath("blockmodels");
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath("blockmodel_cache");
}

QString EconomicModelCacheDir(const QString &projectPath)
{
    if (!projectPath.isEmpty()) {
        return QDir(ProjectDataDir(projectPath)).filePath("economicmodels");
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath("economicmodel_cache");
}

QString FindAssetPath(const QString &relativePath)
{
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(base).filePath("../" + relativePath),
        QDir(base).filePath("../../" + relativePath),
        relativePath};
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QIcon MakeTrimmedIcon(const QString &imagePath, const QSize &targetSize)
{
    if (imagePath.isEmpty()) {
        return {};
    }
    QImage image(imagePath);
    if (image.isNull()) {
        return {};
    }

    QRect bounds(image.width(), image.height(), 0, 0);
    const bool hasAlpha = image.hasAlphaChannel();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor px = image.pixelColor(x, y);
            bool keep = false;
            if (hasAlpha) {
                keep = (px.alpha() > 10);
            } else {
                keep = !(px.red() > 245 && px.green() > 245 && px.blue() > 245);
            }
            if (keep) {
                bounds = bounds.united(QRect(x, y, 1, 1));
            }
        }
    }

    if (bounds.width() <= 0 || bounds.height() <= 0) {
        bounds = image.rect();
    }

    QImage cropped = image.copy(bounds).scaled(
        targetSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    return QIcon(QPixmap::fromImage(cropped));
}

QString UniqueChildName(LayerNode *parent, const QString &baseName)
{
    if (!parent) {
        return baseName;
    }
    QSet<QString> names;
    for (LayerNode *child : parent->children()) {
        if (child) {
            names.insert(child->name().toLower());
        }
    }
    QString candidate = baseName;
    int suffix = 1;
    while (names.contains(candidate.toLower())) {
        candidate = QString("%1 %2").arg(baseName).arg(suffix++);
    }
    return candidate;
}
