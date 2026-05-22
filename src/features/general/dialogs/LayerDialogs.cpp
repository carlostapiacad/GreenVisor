#include "features/general/dialogs/LayerDialogs.h"

#include "gui/LayerModel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariant>

DxfLayerDialog::DxfLayerDialog(const QStringList &layers, const QStringList &existingLayers, QWidget *parent)
    : QDialog(parent), m_existingLayers(existingLayers)
{
    setWindowTitle("Select DXF Layers");
    setModal(true);
    resize(520, 360);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *label = new QLabel("Select layers to import:", this);
    layout->addWidget(label);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({"Layer", "Target", "Date"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setRowCount(layers.size());

    for (int row = 0; row < layers.size(); ++row) {
        const QString &layerName = layers[row];
        QTableWidgetItem *layerItem = new QTableWidgetItem(layerName);
        layerItem->setFlags(layerItem->flags() | Qt::ItemIsUserCheckable);
        layerItem->setCheckState(Qt::Checked);
        m_table->setItem(row, 0, layerItem);

        QComboBox *combo = new QComboBox(m_table);
        combo->addItem("New layer");
        combo->addItems(existingLayers);
        m_table->setCellWidget(row, 1, combo);

        QDateEdit *dateEdit = new QDateEdit(QDate::currentDate(), m_table);
        dateEdit->setDisplayFormat("yyyy-MM-dd");
        dateEdit->setCalendarPopup(true);
        m_table->setCellWidget(row, 2, dateEdit);
    }

    m_table->resizeColumnsToContents();
    layout->addWidget(m_table, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    buttons->addButton("Import", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QList<DxfLayerDialog::Selection> DxfLayerDialog::selections() const
{
    QList<Selection> out;
    if (!m_table) {
        return out;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *item = m_table->item(row, 0);
        if (!item || item->checkState() != Qt::Checked) {
            continue;
        }
        Selection selection;
        selection.sourceLayer = item->text();
        QComboBox *combo = qobject_cast<QComboBox *>(m_table->cellWidget(row, 1));
        if (combo && combo->currentIndex() > 0) {
            selection.targetLayer = combo->currentText();
            selection.createNew = false;
        } else {
            selection.targetLayer = selection.sourceLayer;
            selection.createNew = true;
        }
        QDateEdit *dateEdit = qobject_cast<QDateEdit *>(m_table->cellWidget(row, 2));
        selection.date = dateEdit ? dateEdit->date() : QDate::currentDate();
        out.append(selection);
    }
    return out;
}

AddDateDialog::AddDateDialog(const QList<LayerNode *> &layers, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Add Layer Date");
    setModal(true);
    resize(560, 360);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *label = new QLabel("Select layers to add a date:", this);
    layout->addWidget(label);

    for (LayerNode *node : layers) {
        if (!node || !node->isGeometry()) {
            continue;
        }
        m_layerNames << node->name();
        QList<qint64> keys;
        const QMap<qint64, LayerVersion> &history = node->history();
        for (auto it = history.constBegin(); it != history.constEnd(); ++it) {
            keys.append(it.key());
        }
        m_layerDateKeys.insert(node->name(), keys);
    }

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Layer", "Date", "Copy Existing?", "Source Date"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int row = 0; row < 3; ++row) {
        AddRow();
    }

    m_table->setColumnWidth(0, 200);
    m_table->setColumnWidth(1, 140);
    m_table->resizeColumnsToContents();
    layout->addWidget(m_table, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    buttons->addButton("Add Date", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QList<AddDateDialog::Selection> AddDateDialog::selections() const
{
    QList<Selection> out;
    if (!m_table) {
        return out;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QComboBox *layerCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, 0));
        if (!layerCombo || layerCombo->currentIndex() < 0) {
            continue;
        }
        Selection sel;
        sel.layerName = layerCombo->currentText();

        QDateEdit *dateEdit = qobject_cast<QDateEdit *>(m_table->cellWidget(row, 1));
        sel.date = dateEdit ? dateEdit->date() : QDate::currentDate();

        QWidget *checkHost = m_table->cellWidget(row, 2);
        QCheckBox *copyCheck = checkHost ? checkHost->findChild<QCheckBox *>() : nullptr;
        QComboBox *sourceCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, 3));
        if (copyCheck && copyCheck->isChecked() && sourceCombo && sourceCombo->isEnabled()) {
            const QVariant keyData = sourceCombo->currentData();
            if (keyData.isValid()) {
                sel.copyExisting = true;
                sel.sourceKey = keyData.toLongLong();
            }
        }
        out.append(sel);
    }
    return out;
}

void AddDateDialog::AddRow()
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    QComboBox *layerCombo = new QComboBox(m_table);
    layerCombo->addItems(m_layerNames);
    layerCombo->setCurrentIndex(-1);
    m_table->setCellWidget(row, 0, layerCombo);

    QDateEdit *dateEdit = new QDateEdit(QDate::currentDate(), m_table);
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    dateEdit->setCalendarPopup(true);
    m_table->setCellWidget(row, 1, dateEdit);

    QCheckBox *copyCheck = new QCheckBox(m_table);
    QWidget *checkHost = new QWidget(m_table);
    QHBoxLayout *checkLayout = new QHBoxLayout(checkHost);
    checkLayout->setContentsMargins(0, 0, 0, 0);
    checkLayout->setAlignment(Qt::AlignCenter);
    checkLayout->addWidget(copyCheck);
    m_table->setCellWidget(row, 2, checkHost);

    QComboBox *sourceCombo = new QComboBox(m_table);
    sourceCombo->setEnabled(false);
    m_table->setCellWidget(row, 3, sourceCombo);

    auto refreshSource = [this, layerCombo, sourceCombo, copyCheck]() {
        sourceCombo->clear();
        const QString layerName = layerCombo->currentText();
        const QList<qint64> keys = m_layerDateKeys.value(layerName);
        for (qint64 key : keys) {
            const QDate date = LayerModel::DateFromKey(key);
            if (date.isValid()) {
                sourceCombo->addItem(date.toString("yyyy-MM-dd"), QVariant::fromValue(key));
            }
        }
        const bool hasDates = sourceCombo->count() > 0;
        copyCheck->setEnabled(hasDates);
        if (!hasDates) {
            sourceCombo->addItem("No dates");
            sourceCombo->setEnabled(false);
            copyCheck->setChecked(false);
        } else {
            sourceCombo->setEnabled(copyCheck->isChecked());
        }
    };

    connect(layerCombo, &QComboBox::currentTextChanged, this, [refreshSource]() { refreshSource(); });
    connect(copyCheck, &QCheckBox::toggled, sourceCombo, [sourceCombo](bool checked) {
        sourceCombo->setEnabled(checked && sourceCombo->count() > 0);
    });
}

NewGeometryDialog::NewGeometryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("New Geometry Layer");
    setModal(true);
    resize(360, 140);

    QVBoxLayout *root = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    m_nameEdit = new QLineEdit(this);
    m_dateEdit = new QDateEdit(QDate::currentDate(), this);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");
    m_dateEdit->setCalendarPopup(true);

    form->addRow("Name", m_nameEdit);
    form->addRow("Date", m_dateEdit);
    root->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    buttons->addButton("Create", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QString NewGeometryDialog::name() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QDate NewGeometryDialog::date() const
{
    return m_dateEdit ? m_dateEdit->date() : QDate();
}
