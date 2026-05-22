#pragma once

#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QStringList>

class ProductGroupHeader : public QHeaderView
{
public:
    explicit ProductGroupHeader(Qt::Orientation orientation, QWidget *parent = nullptr);

    void setProducts(const QStringList &products);
    QSize sectionSizeFromContents(int logicalIndex) const override;

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;

private:
    QStringList m_products;
};

class EconomicValueDelegate : public QStyledItemDelegate
{
public:
    explicit EconomicValueDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override;
};

class DecimalValueDelegate : public QStyledItemDelegate
{
public:
    explicit DecimalValueDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override;
};
