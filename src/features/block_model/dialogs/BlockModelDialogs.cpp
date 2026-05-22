#include "features/block_model/dialogs/BlockModelDialogs.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace
{
class ColorButton : public QPushButton
{
public:
    explicit ColorButton(const QColor &color, QWidget *parent = nullptr)
        : QPushButton(parent), m_color(color)
    {
        setFixedSize(160, 34);
        connect(this, &QPushButton::clicked, this, [this]() {
            const QColor chosen = QColorDialog::getColor(m_color, this, "Select Color");
            if (chosen.isValid()) {
                setColor(chosen);
            }
        });
        Refresh();
    }

    QColor color() const { return m_color; }
    void setColor(const QColor &color)
    {
        m_color = color;
        Refresh();
    }

private:
    void Refresh()
    {
        setStyleSheet(QString(
            "QPushButton { background-color: %1; border: 1px solid #6a6a6a; border-radius: 3px; }"
            "QPushButton:hover { border: 1px solid #8a8a8a; }")
                          .arg(m_color.name()));
    }

    QColor m_color;
};
}

CreateBlockModelLegendDialog::CreateBlockModelLegendDialog(
    const QStringList &fields,
    const QString &layerName,
    const MainWindow::BlockModelLegend *existingLegend,
    QWidget *parent)
    : QDialog(parent), m_layerName(layerName)
{
    setWindowTitle(existingLegend ? "Modify Legend" : "Create Legend");
    setModal(true);
    resize(760, 520);

    QVBoxLayout *root = new QVBoxLayout(this);
    QHBoxLayout *top = new QHBoxLayout();
    QFormLayout *form = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    if (existingLegend) {
        m_nameEdit->setText(existingLegend->name);
    }
    QLineEdit *blockModelEdit = new QLineEdit(layerName, this);
    blockModelEdit->setReadOnly(true);
    form->addRow("Name", m_nameEdit);
    form->addRow("Block Model", blockModelEdit);
    top->addLayout(form, 1);
    root->addLayout(top);

    QHBoxLayout *body = new QHBoxLayout();
    QVBoxLayout *fieldPanel = new QVBoxLayout();
    fieldPanel->addWidget(new QLabel("Available Fields", this));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search fields...");
    fieldPanel->addWidget(m_searchEdit);
    m_fieldList = new QListWidget(this);
    m_fieldList->addItems(fields);
    fieldPanel->addWidget(m_fieldList, 1);
    body->addLayout(fieldPanel, 1);

    QVBoxLayout *legendPanel = new QVBoxLayout();
    m_selectedFieldLabel = new QLabel(this);
    legendPanel->addWidget(new QLabel("Selected Field", this));
    legendPanel->addWidget(m_selectedFieldLabel);
    QHBoxLayout *typeLayout = new QHBoxLayout();
    QRadioButton *discreteRadio = new QRadioButton("Discrete Bins", this);
    QRadioButton *gradientRadio = new QRadioButton("Continuous Gradient", this);
    discreteRadio->setChecked(true);
    gradientRadio->setEnabled(false);
    typeLayout->addWidget(discreteRadio);
    typeLayout->addWidget(gradientRadio);
    typeLayout->addStretch(1);
    legendPanel->addLayout(typeLayout);

    m_binsTable = new QTableWidget(0, 5, this);
    m_binsTable->setHorizontalHeaderLabels({"Visible", "Min", "Max", "Color", "Label"});
    m_binsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    legendPanel->addWidget(m_binsTable, 1);
    body->addLayout(legendPanel, 2);
    root->addLayout(body, 1);

    auto setField = [this](const QString &field) {
        m_fieldName = field;
        if (m_selectedFieldLabel) {
            m_selectedFieldLabel->setText(field);
        }
    };
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row = 0; row < m_fieldList->count(); ++row) {
            QListWidgetItem *item = m_fieldList->item(row);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
        }
    });
    connect(m_fieldList, &QListWidget::currentTextChanged, this, setField);

    const QString initialField = existingLegend && !existingLegend->fieldName.isEmpty()
        ? existingLegend->fieldName
        : (fields.isEmpty() ? QString() : fields.first());
    QList<QListWidgetItem *> matches = m_fieldList->findItems(initialField, Qt::MatchFixedString);
    if (!matches.isEmpty()) {
        m_fieldList->setCurrentItem(matches.first());
    } else {
        setField(initialField);
    }

    if (existingLegend && !existingLegend->bins.isEmpty()) {
        for (const MainWindow::BlockModelLegendBin &bin : existingLegend->bins) {
            AddBin(bin);
        }
    } else {
        const QColor colors[] = {
            QColor(170, 170, 170), QColor(25, 104, 205), QColor(27, 190, 205),
            QColor(82, 205, 42), QColor(245, 225, 20), QColor(245, 160, 0), QColor(220, 25, 35)};
        const double ranges[][2] = {{0.0, 0.2}, {0.2, 0.5}, {0.5, 1.0}, {1.0, 1.5}, {1.5, 2.0}, {2.0, 3.0}, {3.0, std::numeric_limits<double>::infinity()}};
        for (int i = 0; i < 7; ++i) {
            MainWindow::BlockModelLegendBin bin;
            bin.minValue = ranges[i][0];
            bin.maxValue = ranges[i][1];
            bin.color = colors[i];
            bin.label = std::isinf(bin.maxValue)
                ? QString("> %1").arg(bin.minValue, 0, 'f', 2)
                : QString("%1 - %2").arg(bin.minValue, 0, 'f', 2).arg(bin.maxValue, 0, 'f', 2);
            AddBin(bin);
        }
    }

    QHBoxLayout *binButtons = new QHBoxLayout();
    QPushButton *addBinButton = new QPushButton("Add Bin", this);
    connect(addBinButton, &QPushButton::clicked, this, [this]() {
        MainWindow::BlockModelLegendBin bin;
        bin.color = QColor(80, 190, 190);
        AddBin(bin);
    });
    QPushButton *removeBinButton = new QPushButton("Remove Bin", this);
    connect(removeBinButton, &QPushButton::clicked, this, [this]() {
        const int row = m_binsTable->currentRow();
        if (row >= 0) {
            m_binsTable->removeRow(row);
        }
    });
    binButtons->addWidget(addBinButton);
    binButtons->addWidget(removeBinButton);
    binButtons->addStretch(1);
    root->addLayout(binButtons);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    buttons->addButton(existingLegend ? "Save" : "Create", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (name().isEmpty() || fieldName().isEmpty()) {
            QMessageBox::warning(this, "Legend", "Name and field are required.");
            return;
        }
        if (bins().isEmpty()) {
            QMessageBox::warning(this, "Legend", "Add at least one valid bin.");
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QString CreateBlockModelLegendDialog::name() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QString CreateBlockModelLegendDialog::layerName() const
{
    return m_layerName;
}

QString CreateBlockModelLegendDialog::fieldName() const
{
    return m_fieldName;
}

QList<MainWindow::BlockModelLegendBin> CreateBlockModelLegendDialog::bins() const
{
    QList<MainWindow::BlockModelLegendBin> out;
    for (int row = 0; row < m_binsTable->rowCount(); ++row) {
        bool okMin = false;
        bool okMax = false;
        const bool visible = m_binsTable->item(row, 0) ? m_binsTable->item(row, 0)->checkState() == Qt::Checked : true;
        const double minValue = m_binsTable->item(row, 1) ? m_binsTable->item(row, 1)->text().toDouble(&okMin) : 0.0;
        const QString maxText = m_binsTable->item(row, 2) ? m_binsTable->item(row, 2)->text().trimmed() : QString();
        const double maxValue = (maxText == "+oo" || maxText == "+inf" || maxText.compare("inf", Qt::CaseInsensitive) == 0)
            ? std::numeric_limits<double>::infinity()
            : maxText.toDouble(&okMax);
        if (std::isinf(maxValue)) {
            okMax = true;
        }
        ColorButton *button = dynamic_cast<ColorButton *>(m_binsTable->cellWidget(row, 3));
        if (okMin && okMax && button) {
            MainWindow::BlockModelLegendBin bin;
            bin.visible = visible;
            bin.minValue = minValue;
            bin.maxValue = maxValue;
            bin.color = button->color();
            bin.label = m_binsTable->item(row, 4) ? m_binsTable->item(row, 4)->text().trimmed() : QString();
            out.append(bin);
        }
    }
    return out;
}

void CreateBlockModelLegendDialog::AddBin(const MainWindow::BlockModelLegendBin &bin)
{
    const int row = m_binsTable->rowCount();
    m_binsTable->insertRow(row);
    QTableWidgetItem *visibleItem = new QTableWidgetItem();
    visibleItem->setFlags(visibleItem->flags() | Qt::ItemIsUserCheckable);
    visibleItem->setCheckState(bin.visible ? Qt::Checked : Qt::Unchecked);
    m_binsTable->setItem(row, 0, visibleItem);
    m_binsTable->setItem(row, 1, new QTableWidgetItem(QString::number(bin.minValue, 'f', 2)));
    m_binsTable->setItem(row, 2, new QTableWidgetItem(std::isinf(bin.maxValue) ? QString("+oo") : QString::number(bin.maxValue, 'f', 2)));
    m_binsTable->setCellWidget(row, 3, new ColorButton(bin.color.isValid() ? bin.color : QColor(80, 190, 190), m_binsTable));
    m_binsTable->setItem(row, 4, new QTableWidgetItem(bin.label));
}

BlockModelPropertiesDialog::BlockModelPropertiesDialog(
    const QString &layerName,
    const visor::datamine::InternalBlockModelInfo &info,
    const MainWindow::BlockModelDisplaySettings &settings,
    const QStringList &legendNames,
    MainWindow::ViewMode viewMode,
    QWidget *parent)
    : QDialog(parent), m_settings(settings)
{
    Q_UNUSED(info);
    Q_UNUSED(viewMode);
    setWindowTitle("Block Model Properties");
    setModal(true);
    resize(620, 520);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 18);
    root->setSpacing(16);

    QLabel *title = new QLabel("Block Model Properties", this);
    title->setStyleSheet("font-size: 24px; color: #0f172a;");
    root->addWidget(title);

    QFrame *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("color: #d7dde5; background: #d7dde5;");
    root->addWidget(separator);

    QFormLayout *top = new QFormLayout();
    top->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    top->setHorizontalSpacing(40);
    m_nameEdit = new QLineEdit(layerName, this);
    top->addRow("Layer Name", m_nameEdit);
    m_renderModeCombo = new QComboBox(this);
    m_renderModeCombo->addItem("2D section", static_cast<int>(MainWindow::BlockModelRenderMode::Section2D));
    m_renderModeCombo->addItem("3D blocks", static_cast<int>(MainWindow::BlockModelRenderMode::Solid3D));
    const int renderModeIndex = m_renderModeCombo->findData(static_cast<int>(settings.renderMode));
    if (renderModeIndex >= 0) {
        m_renderModeCombo->setCurrentIndex(renderModeIndex);
    }
    top->addRow("View Mode", m_renderModeCombo);
    root->addLayout(top);

    m_blocksCheck = new QCheckBox("Visible", this);
    m_blocksCheck->setChecked(settings.blocksEnabled);
    m_blockLegendCombo = BuildLegendCombo(legendNames, settings.blockLegend);
    m_legendButton = new QPushButton("...", this);
    m_legendButton->setFixedWidth(42);
    m_gapSpin = new QDoubleSpinBox(this);
    m_gapSpin->setRange(0.0, 60.0);
    m_gapSpin->setSuffix(" %");
    m_gapSpin->setSingleStep(1.0);
    m_gapSpin->setValue(settings.blockGapPercent);
    root->addWidget(BuildBlocksGroup());

    root->addStretch(1);
    QFrame *bottomSeparator = new QFrame(this);
    bottomSeparator->setFrameShape(QFrame::HLine);
    bottomSeparator->setStyleSheet("color: #d7dde5; background: #d7dde5;");
    root->addWidget(bottomSeparator);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton *applyButton = new QPushButton("Apply", this);
    QPushButton *okButton = new QPushButton("OK", this);
    okButton->setObjectName("primaryButton");
    QPushButton *cancelButton = new QPushButton("Cancel", this);
    buttons->addWidget(applyButton);
    buttons->addWidget(okButton);
    buttons->addWidget(cancelButton);
    connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    root->addLayout(buttons);
}

QString BlockModelPropertiesDialog::layerName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

MainWindow::BlockModelDisplaySettings BlockModelPropertiesDialog::settings() const
{
    MainWindow::BlockModelDisplaySettings out = m_settings;
    out.blocksEnabled = m_blocksCheck && m_blocksCheck->isChecked();
    out.linesEnabled = false;
    out.labelsEnabled = false;
    out.blockColor = QColor(80, 190, 190);
    out.blockLegend = SelectedLegend(m_blockLegendCombo);
    out.lineLegend.clear();
    out.labelField.clear();
    out.blockGapPercent = m_gapSpin ? m_gapSpin->value() : 8.0;
    out.renderMode = static_cast<MainWindow::BlockModelRenderMode>(
        m_renderModeCombo ? m_renderModeCombo->currentData().toInt() : static_cast<int>(MainWindow::BlockModelRenderMode::Section2D));
    return out;
}

QPushButton *BlockModelPropertiesDialog::legendButton() const
{
    return m_legendButton;
}

QString BlockModelPropertiesDialog::selectedLegend() const
{
    return SelectedLegend(m_blockLegendCombo);
}

void BlockModelPropertiesDialog::setLegendNames(const QStringList &legendNames, const QString &current)
{
    if (!m_blockLegendCombo) {
        return;
    }
    m_blockLegendCombo->clear();
    m_blockLegendCombo->addItem("None", "");
    for (const QString &legend : legendNames) {
        m_blockLegendCombo->addItem(legend, legend);
    }
    const int idx = m_blockLegendCombo->findData(current);
    if (idx >= 0) {
        m_blockLegendCombo->setCurrentIndex(idx);
    }
}

QComboBox *BlockModelPropertiesDialog::BuildLegendCombo(const QStringList &legendNames, const QString &current)
{
    QComboBox *combo = new QComboBox(this);
    combo->addItem("None", "");
    for (const QString &legend : legendNames) {
        combo->addItem(legend, legend);
    }
    const int idx = combo->findData(current);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    }
    return combo;
}

QString BlockModelPropertiesDialog::SelectedLegend(QComboBox *combo)
{
    if (!combo) {
        return {};
    }
    const QString value = combo->currentData().toString();
    return value == "__create__" ? QString() : value;
}

QGroupBox *BlockModelPropertiesDialog::BuildBlocksGroup()
{
    QGroupBox *group = new QGroupBox("Blocks", this);
    QGridLayout *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(28);
    layout->setVerticalSpacing(16);
    layout->addWidget(m_blocksCheck, 0, 0, 1, 3);
    layout->addWidget(new QLabel("Block Gap", group), 1, 0);
    layout->addWidget(m_gapSpin, 1, 1, 1, 2);
    layout->addWidget(new QLabel("Legend", group), 2, 0);
    layout->addWidget(m_blockLegendCombo, 2, 1);
    layout->addWidget(m_legendButton, 2, 2);
    layout->setColumnStretch(1, 1);
    return group;
}
