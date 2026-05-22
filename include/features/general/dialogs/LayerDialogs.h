#pragma once

#include <QDate>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QDateEdit;
class QLineEdit;
class QTableWidget;
class LayerNode;

class DxfLayerDialog : public QDialog
{
public:
    struct Selection
    {
        QString sourceLayer;
        QString targetLayer;
        QDate date;
        bool createNew = true;
    };

    DxfLayerDialog(const QStringList &layers, const QStringList &existingLayers, QWidget *parent = nullptr);

    QList<Selection> selections() const;

private:
    QTableWidget *m_table = nullptr;
    QStringList m_existingLayers;
};

class AddDateDialog : public QDialog
{
public:
    struct Selection
    {
        QString layerName;
        QDate date;
        bool copyExisting = false;
        qint64 sourceKey = 0;
    };

    AddDateDialog(const QList<LayerNode *> &layers, QWidget *parent = nullptr);

    QList<Selection> selections() const;

private:
    void AddRow();

    QTableWidget *m_table = nullptr;
    QStringList m_layerNames;
    QHash<QString, QList<qint64>> m_layerDateKeys;
};

class NewGeometryDialog : public QDialog
{
public:
    explicit NewGeometryDialog(QWidget *parent = nullptr);

    QString name() const;
    QDate date() const;

private:
    QLineEdit *m_nameEdit = nullptr;
    QDateEdit *m_dateEdit = nullptr;
};
