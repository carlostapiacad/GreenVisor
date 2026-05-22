#include "gui/LayerModel.h"

#include <QBrush>
#include <QColor>
#include <QDate>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QStyle>
#include <QApplication>
#include <QFont>
#include <algorithm>
#include <functional>
#include <deque>

namespace
{
QIcon LayerManagerIcon(const QString &fileName)
{
    static QHash<QString, QIcon> cache;
    if (cache.contains(fileName)) {
        return cache.value(fileName);
    }

    const QString base = QApplication::applicationDirPath();
    const QString relative = QDir("assets/Icons/Layer Manager").filePath(fileName);
    const QString optimizedRelative = QDir("assets/Icons/Layer Manager/Optimized").filePath(fileName);
    const QStringList candidates = {
        QDir(base).filePath("../" + optimizedRelative),
        QDir(base).filePath("../../" + optimizedRelative),
        optimizedRelative,
        QDir(base).filePath("../" + relative),
        QDir(base).filePath("../../" + relative),
        relative};
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            QImage image(candidate);
            if (image.isNull()) {
                continue;
            }
            image = image.convertToFormat(QImage::Format_ARGB32);

            auto isBackground = [](const QColor &color) {
                if (color.alpha() <= 10) {
                    return true;
                }
                const int maxChannel = std::max({color.red(), color.green(), color.blue()});
                const int minChannel = std::min({color.red(), color.green(), color.blue()});
                return minChannel >= 238 && (maxChannel - minChannel) <= 22;
            };

            const int width = image.width();
            const int height = image.height();
            std::vector<unsigned char> visited(static_cast<std::size_t>(width * height), 0);
            std::deque<QPoint> queue;
            auto enqueue = [&](int x, int y) {
                if (x < 0 || y < 0 || x >= width || y >= height) {
                    return;
                }
                const int index = y * width + x;
                if (visited[static_cast<std::size_t>(index)]) {
                    return;
                }
                const QColor color = image.pixelColor(x, y);
                if (!isBackground(color)) {
                    return;
                }
                visited[static_cast<std::size_t>(index)] = 1;
                queue.push_back(QPoint(x, y));
            };

            for (int x = 0; x < width; ++x) {
                enqueue(x, 0);
                enqueue(x, height - 1);
            }
            for (int y = 0; y < height; ++y) {
                enqueue(0, y);
                enqueue(width - 1, y);
            }

            while (!queue.empty()) {
                const QPoint point = queue.front();
                queue.pop_front();
                const int x = point.x();
                const int y = point.y();
                QColor transparent = image.pixelColor(x, y);
                transparent.setAlpha(0);
                image.setPixelColor(x, y, transparent);
                enqueue(x + 1, y);
                enqueue(x - 1, y);
                enqueue(x, y + 1);
                enqueue(x, y - 1);
            }

            QRect bounds;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (image.pixelColor(x, y).alpha() <= 10) {
                        continue;
                    }
                    const QRect pixelRect(x, y, 1, 1);
                    bounds = bounds.isNull() ? pixelRect : bounds.united(pixelRect);
                }
            }
            if (bounds.isNull()) {
                return QIcon(candidate);
            }

            constexpr int padding = 8;
            bounds = bounds.adjusted(-padding, -padding, padding, padding).intersected(image.rect());
            const QImage trimmed = image.copy(bounds).scaled(
                QSize(22, 22),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
            const QIcon icon(QPixmap::fromImage(trimmed));
            cache.insert(fileName, icon);
            return icon;
        }
    }
    return {};
}

LayerNode *BlockModelAncestor(LayerNode *node)
{
    for (LayerNode *current = node; current; current = current->parent()) {
        if (current->isBlockModel()) {
            return current;
        }
    }
    return nullptr;
}
}

LayerNode::LayerNode(Type type, const QString &name, LayerNode *parent)
    : m_type(type), m_name(name), m_parent(parent)
{
}

LayerNode::~LayerNode()
{
    for (LayerNode *child : m_children) {
        delete child;
    }
}

int LayerNode::row() const
{
    if (!m_parent) {
        return 0;
    }
    const auto &siblings = m_parent->m_children;
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == this) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void LayerNode::appendChild(LayerNode *child)
{
    if (!child) {
        return;
    }
    child->m_parent = this;
    m_children.push_back(child);
}

void LayerNode::insertChild(int row, LayerNode *child)
{
    if (!child) {
        return;
    }
    child->m_parent = this;
    if (row < 0 || row > static_cast<int>(m_children.size())) {
        m_children.push_back(child);
    } else {
        m_children.insert(m_children.begin() + row, child);
    }
}

void LayerNode::removeChild(LayerNode *child)
{
    if (!child) {
        return;
    }
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (*it == child) {
            m_children.erase(it);
            return;
        }
    }
}

LayerModel::LayerModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_root = new LayerNode(LayerNode::Type::Folder, "Root");
}

LayerModel::~LayerModel()
{
    delete m_root;
}

QModelIndex LayerModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.column() > 0) {
        return {};
    }
    if (!hasIndex(row, column, parent)) {
        return {};
    }
    LayerNode *parentNode = nodeFromIndex(parent);
    if (!parentNode) {
        return {};
    }
    const auto &children = parentNode->children();
    if (row < 0 || row >= static_cast<int>(children.size())) {
        return {};
    }
    return createIndex(row, column, children[static_cast<size_t>(row)]);
}

QModelIndex LayerModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return {};
    }
    LayerNode *node = nodeFromIndex(child);
    if (!node) {
        return {};
    }
    LayerNode *parentNode = node->parent();
    if (!parentNode || parentNode == m_root) {
        return {};
    }
    return createIndex(parentNode->row(), 0, parentNode);
}

int LayerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    LayerNode *parentNode = nodeFromIndex(parent);
    if (!parentNode) {
        return 0;
    }
    return static_cast<int>(parentNode->children().size());
}

int LayerModel::columnCount(const QModelIndex &) const
{
    return 2;
}

QVariant LayerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    LayerNode *node = nodeFromIndex(index);
    if (!node) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        if (index.column() == 0) {
            QString text = node->name();
            if (node->isGeometry() && node->isVisible() && node->activeDate() > 0) {
                QDate date = DateFromKey(node->activeDate());
                if (date.isValid()) {
                    text += QString(" (%1)").arg(date.toString("yyyy-MM-dd"));
                }
            }
            if (node->type() == LayerNode::Type::Folder && node->children().empty()) {
                text += " (empty)";
            }
            return text;
        }
        return {};
    }

    if (role == Qt::DecorationRole && index.column() == 0) {
        if (node->type() == LayerNode::Type::Folder) {
            const QIcon icon = LayerManagerIcon("Folder.png");
            return icon.isNull() ? QApplication::style()->standardIcon(QStyle::SP_DirIcon) : icon;
        }
        if (node->isBlockModel()) {
            const bool pitShell = node->parent() && node->parent()->isEconomicModel();
            const QIcon icon = LayerManagerIcon(pitShell ? "Pit Shell.png" : "Block Model.png");
            return icon.isNull() ? QApplication::style()->standardIcon(QStyle::SP_ComputerIcon) : icon;
        }
        if (node->isEconomicModel()) {
            const QIcon icon = LayerManagerIcon("Economic Model.png");
            return icon.isNull() ? QApplication::style()->standardIcon(QStyle::SP_DriveHDIcon) : icon;
        }
        if (node->isLegend()) {
            const QIcon icon = LayerManagerIcon("Legend.png");
            return icon.isNull() ? QApplication::style()->standardIcon(QStyle::SP_FileIcon) : icon;
        }
        if (node->isGeometry() && node->parent() &&
            node->parent()->name().compare("Topography", Qt::CaseInsensitive) == 0) {
            const QIcon icon = LayerManagerIcon("Topography.png");
            return icon.isNull() ? QApplication::style()->standardIcon(QStyle::SP_FileIcon) : icon;
        }
        const QIcon icon = LayerManagerIcon("Geometry Layer.png");
        return icon.isNull() ? QApplication::style()->standardIcon(QStyle::SP_FileIcon) : icon;
    }

    if (role == Qt::FontRole && index.column() == 0) {
        QFont font;
        font.setBold(node->isVisible() && !node->isLegend());
        font.setWeight(node->isVisible() ? QFont::DemiBold : QFont::Light);
        if (node->isLegend() || (node->type() == LayerNode::Type::Folder && node->children().empty())) {
            font.setItalic(true);
            font.setWeight(QFont::Normal);
        }
        return font;
    }

    if (role == Qt::ForegroundRole && index.column() == 0) {
        if (!node->isVisible()) {
            return QBrush(QColor("#9a9a9a"));
        }
        if (node->isLegend() || (node->type() == LayerNode::Type::Folder && node->children().empty())) {
            return QBrush(QColor("#8a8f98"));
        }
        return QBrush(QColor("#1f2933"));
    }

    return {};
}

QVariant LayerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    Q_UNUSED(section);
    return {};
}

Qt::ItemFlags LayerModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

bool LayerModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.column() != 0) {
        return false;
    }
    LayerNode *node = nodeFromIndex(index);
    if (!node) {
        return false;
    }

    if (role == Qt::EditRole) {
        node->setName(value.toString());
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::DecorationRole});
        return true;
    }

    if (role == Qt::CheckStateRole) {
        const bool visible = (value.toInt() == Qt::Checked);
        if (node->isVisible() == visible) {
            return true;
        }

        node->setVisible(visible);
        QModelIndex idx = indexFromNode(node, 0);
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::FontRole, Qt::ForegroundRole});
        emit visibilityChanged(node, visible);
        return true;
    }

    return false;
}

LayerNode *LayerModel::nodeFromIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return m_root;
    }
    return static_cast<LayerNode *>(index.internalPointer());
}

QModelIndex LayerModel::indexFromNode(LayerNode *node, int column) const
{
    if (!node || node == m_root) {
        return {};
    }
    return createIndex(node->row(), column, node);
}

LayerNode *LayerModel::addFolder(const QString &name, LayerNode *parent)
{
    LayerNode *parentNode = parent ? parent : m_root;
    if (!parentNode ||
        (parentNode->type() != LayerNode::Type::Folder &&
         parentNode->type() != LayerNode::Type::BlockModel &&
         parentNode->type() != LayerNode::Type::EconomicModel)) {
        parentNode = m_root;
    }

    int row = static_cast<int>(parentNode->children().size());
    beginInsertRows(indexFromNode(parentNode), row, row);
    LayerNode *node = new LayerNode(LayerNode::Type::Folder, name, parentNode);
    parentNode->appendChild(node);
    endInsertRows();
    return node;
}

LayerNode *LayerModel::addGeometryLayer(const QString &name, LayerNode *parent)
{
    LayerNode *parentNode = parent ? parent : m_root;
    if (!parentNode || parentNode->type() != LayerNode::Type::Folder) {
        parentNode = m_root;
    }

    int row = static_cast<int>(parentNode->children().size());
    beginInsertRows(indexFromNode(parentNode), row, row);
    LayerNode *node = new LayerNode(LayerNode::Type::Geometry, name, parentNode);
    parentNode->appendChild(node);
    endInsertRows();
    return node;
}

LayerNode *LayerModel::addBlockModelLayer(const QString &name, LayerNode *parent)
{
    LayerNode *parentNode = parent ? parent : m_root;
    if (!parentNode || (parentNode->type() != LayerNode::Type::Folder && parentNode->type() != LayerNode::Type::EconomicModel)) {
        parentNode = m_root;
    }

    int row = static_cast<int>(parentNode->children().size());
    beginInsertRows(indexFromNode(parentNode), row, row);
    LayerNode *node = new LayerNode(LayerNode::Type::BlockModel, name, parentNode);
    parentNode->appendChild(node);
    endInsertRows();
    return node;
}

LayerNode *LayerModel::addEconomicModelLayer(const QString &name, LayerNode *parent)
{
    LayerNode *parentNode = parent;
    if (!parentNode) {
        return nullptr;
    }
    if (!parentNode->isBlockModel() && parentNode->type() == LayerNode::Type::Folder) {
        if (!BlockModelAncestor(parentNode) || parentNode->name().compare("Economic Models", Qt::CaseInsensitive) != 0) {
            return nullptr;
        }
    } else if (!parentNode->isBlockModel()) {
        return nullptr;
    }

    int row = static_cast<int>(parentNode->children().size());
    beginInsertRows(indexFromNode(parentNode), row, row);
    LayerNode *node = new LayerNode(LayerNode::Type::EconomicModel, name, parentNode);
    parentNode->appendChild(node);
    endInsertRows();
    return node;
}

LayerNode *LayerModel::addLegendLayer(const QString &name, LayerNode *parent)
{
    LayerNode *parentNode = parent;
    if (!parentNode || parentNode->type() != LayerNode::Type::Folder) {
        return nullptr;
    }

    int row = static_cast<int>(parentNode->children().size());
    beginInsertRows(indexFromNode(parentNode), row, row);
    LayerNode *node = new LayerNode(LayerNode::Type::Legend, name, parentNode);
    node->setVisible(true);
    parentNode->appendChild(node);
    endInsertRows();
    return node;
}

bool LayerModel::removeNode(LayerNode *node)
{
    if (!node || node == m_root) {
        return false;
    }
    std::function<bool(LayerNode *, LayerNode *)> containsNode = [&](LayerNode *parent, LayerNode *target) -> bool {
        if (!parent || !target) {
            return false;
        }
        if (parent == target) {
            return true;
        }
        for (LayerNode *child : parent->children()) {
            if (containsNode(child, target)) {
                return true;
            }
        }
        return false;
    };
    if (m_activeLayer && containsNode(node, m_activeLayer)) {
        LayerNode *prev = m_activeLayer;
        m_activeLayer = nullptr;
        QModelIndex prevIdx = indexFromNode(prev, 0);
        if (prevIdx.isValid()) {
            emit dataChanged(prevIdx, prevIdx, {Qt::FontRole});
        }
        emit activeLayerChanged(nullptr);
    }

    LayerNode *parentNode = node->parent();
    if (!parentNode) {
        return false;
    }
    int row = node->row();
    beginRemoveRows(indexFromNode(parentNode), row, row);
    parentNode->removeChild(node);
    delete node;
    endRemoveRows();
    return true;
}

bool LayerModel::moveNode(LayerNode *node, LayerNode *newParent, int newRow)
{
    if (!node || node == m_root || !newParent) {
        return false;
    }
    if (newParent->type() != LayerNode::Type::Folder) {
        return false;
    }
    for (LayerNode *p = newParent; p != nullptr; p = p->parent()) {
        if (p == node) {
            return false;
        }
    }

    LayerNode *oldParent = node->parent();
    if (!oldParent) {
        return false;
    }
    int oldRow = node->row();
    if (newRow < 0) {
        newRow = static_cast<int>(newParent->children().size());
    }

    beginMoveRows(indexFromNode(oldParent), oldRow, oldRow, indexFromNode(newParent), newRow);
    oldParent->removeChild(node);
    newParent->insertChild(newRow, node);
    endMoveRows();
    return true;
}

void LayerModel::addVersion(LayerNode *node, qint64 dateKey, const QList<GeometryEntity> &entities)
{
    if (!node || !node->isGeometry()) {
        return;
    }
    LayerVersion version;
    version.entities = entities;
    node->history().insert(dateKey, version);
    if (node->activeDate() == 0 || dateKey >= node->activeDate()) {
        node->setActiveDate(dateKey);
        QModelIndex idx = indexFromNode(node, 1);
        emit dataChanged(idx, idx, {Qt::DisplayRole});
    }
}

bool LayerModel::setActiveDate(LayerNode *node, qint64 dateKey)
{
    if (!node || !node->isGeometry()) {
        return false;
    }
    if (!node->history().contains(dateKey)) {
        return false;
    }
    node->setActiveDate(dateKey);
    QModelIndex idx = indexFromNode(node, 1);
    emit dataChanged(idx, idx, {Qt::DisplayRole});
    return true;
}

bool LayerModel::setActiveDate(const QModelIndex &index, qint64 dateKey)
{
    return setActiveDate(nodeFromIndex(index), dateKey);
}

bool LayerModel::setActiveLayer(LayerNode *node)
{
    if (!node || !node->isGeometry()) {
        return false;
    }
    if (m_activeLayer == node) {
        return true;
    }
    LayerNode *prev = m_activeLayer;
    m_activeLayer = node;
    if (prev) {
        QModelIndex prevIdx = indexFromNode(prev, 0);
        emit dataChanged(prevIdx, prevIdx, {Qt::FontRole});
    }
    QModelIndex idx = indexFromNode(node, 0);
    emit dataChanged(idx, idx, {Qt::FontRole});
    emit activeLayerChanged(node);
    return true;
}

QList<LayerNode *> LayerModel::geometryLayers() const
{
    QList<LayerNode *> nodes;
    std::vector<LayerNode *> stack{m_root};
    while (!stack.empty()) {
        LayerNode *node = stack.back();
        stack.pop_back();
        if (!node) {
            continue;
        }
        if (node->isGeometry()) {
            nodes.append(node);
        }
        const auto &children = node->children();
        for (LayerNode *child : children) {
            stack.push_back(child);
        }
    }
    return nodes;
}

QList<LayerNode *> LayerModel::blockModelLayers() const
{
    QList<LayerNode *> nodes;
    std::vector<LayerNode *> stack{m_root};
    while (!stack.empty()) {
        LayerNode *node = stack.back();
        stack.pop_back();
        if (!node) {
            continue;
        }
        if (node->isBlockModel()) {
            nodes.append(node);
        }
        const auto &children = node->children();
        for (LayerNode *child : children) {
            stack.push_back(child);
        }
    }
    return nodes;
}

LayerNode *LayerModel::findGeometryLayerByName(const QString &name) const
{
    const QList<LayerNode *> nodes = geometryLayers();
    for (LayerNode *node : nodes) {
        if (node && node->name().compare(name, Qt::CaseInsensitive) == 0) {
            return node;
        }
    }
    return nullptr;
}

LayerNode *LayerModel::findBlockModelLayerByName(const QString &name) const
{
    const QList<LayerNode *> nodes = blockModelLayers();
    for (LayerNode *node : nodes) {
        if (node && node->name().compare(name, Qt::CaseInsensitive) == 0) {
            return node;
        }
    }
    return nullptr;
}

qint64 LayerModel::DateKey(const QDate &date)
{
    if (!date.isValid()) {
        return 0;
    }
    return static_cast<qint64>(date.year()) * 10000 +
        static_cast<qint64>(date.month()) * 100 +
        static_cast<qint64>(date.day());
}

QDate LayerModel::DateFromKey(qint64 key)
{
    if (key <= 0) {
        return {};
    }
    int year = static_cast<int>(key / 10000);
    int month = static_cast<int>((key / 100) % 100);
    int day = static_cast<int>(key % 100);
    return QDate(year, month, day);
}
