#include "features/pit_optimization/widgets/EconomicTableWidgets.h"

#include <QLineEdit>
#include <QPainter>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QStyleOptionHeader>

#include <algorithm>

ProductGroupHeader::ProductGroupHeader(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setSectionsClickable(false);
    setDefaultAlignment(Qt::AlignCenter);
}

void ProductGroupHeader::setProducts(const QStringList &products)
{
    m_products = products;
    viewport()->update();
}

QSize ProductGroupHeader::sectionSizeFromContents(int logicalIndex) const
{
    QSize size = QHeaderView::sectionSizeFromContents(logicalIndex);
    size.setHeight(std::max(size.height(), 72));
    return size;
}

void ProductGroupHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    if (!painter || !rect.isValid()) {
        return;
    }
    const int productIndex = (logicalIndex - 3) / 3;
    const bool productColumn = logicalIndex >= 3 && productIndex >= 0 && productIndex < m_products.size();
    if (!productColumn) {
        QHeaderView::paintSection(painter, rect, logicalIndex);
        return;
    }

    const int topHeight = 28;
    const QRect bottomRect(rect.left(), rect.top() + topHeight, rect.width(), rect.height() - topHeight);
    const QString subText = model() ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString() : QString();
    if ((logicalIndex - 3) % 3 == 0) {
        int spanWidth = 0;
        for (int i = 0; i < 3 && logicalIndex + i < count(); ++i) {
            spanWidth += sectionSize(logicalIndex + i);
        }
        QStyleOptionHeader topOpt;
        initStyleOption(&topOpt);
        topOpt.rect = QRect(rect.left(), rect.top(), spanWidth, topHeight);
        topOpt.section = logicalIndex;
        topOpt.text = m_products[productIndex];
        topOpt.textAlignment = Qt::AlignCenter;
        style()->drawControl(QStyle::CE_Header, &topOpt, painter, this);
    }

    QStyleOptionHeader bottomOpt;
    initStyleOption(&bottomOpt);
    bottomOpt.rect = bottomRect;
    bottomOpt.section = logicalIndex;
    bottomOpt.text = subText;
    bottomOpt.textAlignment = Qt::AlignCenter;
    style()->drawControl(QStyle::CE_Header, &bottomOpt, painter, this);
}

EconomicValueDelegate::EconomicValueDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *EconomicValueDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const
{
    QLineEdit *editor = new QLineEdit(parent);
    editor->setValidator(new QRegularExpressionValidator(
        QRegularExpression(R"(^\s*(?:[-+]?\d+(?:\.\d{1,2})?|[A-Za-z_][A-Za-z0-9_]*)?\s*$)"),
        editor));
    return editor;
}

DecimalValueDelegate::DecimalValueDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *DecimalValueDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const
{
    QLineEdit *editor = new QLineEdit(parent);
    editor->setValidator(new QRegularExpressionValidator(
        QRegularExpression(R"(^\s*(?:[-+]?\d+(?:\.\d{1,2})?)?\s*$)"),
        editor));
    return editor;
}
