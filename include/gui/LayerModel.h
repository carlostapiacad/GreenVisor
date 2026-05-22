#pragma once

#include <QAbstractItemModel>
#include <QDate>
#include <QMap>
#include <QVariant>
#include <QFont>

#include <vector>

struct GeometryEntity
{
    QMap<QString, QVariant> properties;
};

struct LayerVersion
{
    QList<GeometryEntity> entities;
};

class LayerNode
{
public:
    enum class Type
    {
        Folder,
        Geometry,
        BlockModel,
        EconomicModel,
        Legend
    };

    explicit LayerNode(Type type, const QString &name, LayerNode *parent = nullptr);
    ~LayerNode();

    Type type() const { return m_type; }
    const QString &name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    LayerNode *parent() const { return m_parent; }
    const std::vector<LayerNode *> &children() const { return m_children; }
    int row() const;

    void appendChild(LayerNode *child);
    void insertChild(int row, LayerNode *child);
    void removeChild(LayerNode *child);

    bool isGeometry() const { return m_type == Type::Geometry; }
    bool isBlockModel() const { return m_type == Type::BlockModel; }
    bool isEconomicModel() const { return m_type == Type::EconomicModel; }
    bool isLegend() const { return m_type == Type::Legend; }
    QMap<qint64, LayerVersion> &history() { return m_history; }
    const QMap<qint64, LayerVersion> &history() const { return m_history; }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    bool isSystemNode() const { return m_systemNode; }
    void setSystemNode(bool systemNode) { m_systemNode = systemNode; }

    qint64 activeDate() const { return m_activeDate; }
    void setActiveDate(qint64 key) { m_activeDate = key; }

private:
    Type m_type;
    QString m_name;
    LayerNode *m_parent = nullptr;
    std::vector<LayerNode *> m_children;
    QMap<qint64, LayerVersion> m_history;
    qint64 m_activeDate = 0;
    bool m_visible = true;
    bool m_systemNode = false;
};

class LayerModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit LayerModel(QObject *parent = nullptr);
    ~LayerModel() override;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    LayerNode *root() const { return m_root; }
    LayerNode *nodeFromIndex(const QModelIndex &index) const;
    QModelIndex indexFromNode(LayerNode *node, int column = 0) const;

    LayerNode *addFolder(const QString &name, LayerNode *parent = nullptr);
    LayerNode *addGeometryLayer(const QString &name, LayerNode *parent = nullptr);
    LayerNode *addBlockModelLayer(const QString &name, LayerNode *parent = nullptr);
    LayerNode *addEconomicModelLayer(const QString &name, LayerNode *parent);
    LayerNode *addLegendLayer(const QString &name, LayerNode *parent);
    bool removeNode(LayerNode *node);
    bool moveNode(LayerNode *node, LayerNode *newParent, int newRow);

    void addVersion(LayerNode *node, qint64 dateKey, const QList<GeometryEntity> &entities = {});
    bool setActiveDate(LayerNode *node, qint64 dateKey);
    bool setActiveDate(const QModelIndex &index, qint64 dateKey);
    bool setActiveLayer(LayerNode *node);
    LayerNode *activeLayer() const { return m_activeLayer; }

    QList<LayerNode *> geometryLayers() const;
    QList<LayerNode *> blockModelLayers() const;
    LayerNode *findGeometryLayerByName(const QString &name) const;
    LayerNode *findBlockModelLayerByName(const QString &name) const;

    static qint64 DateKey(const QDate &date);
    static QDate DateFromKey(qint64 key);

private:
    LayerNode *m_root = nullptr;
    LayerNode *m_activeLayer = nullptr;

signals:
    void visibilityChanged(LayerNode *node, bool visible);
    void activeLayerChanged(LayerNode *node);
};
