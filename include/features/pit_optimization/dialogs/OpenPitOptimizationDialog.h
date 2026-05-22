#pragma once

#include "gui/MainWindow.h"
#include "features/block_model/engine/BlockModelFieldUtils.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
class OpenPitOptimizationDialog : public QDialog
{
public:
    struct EconomicModelOption
    {
        QString label;
        QString key;
        QString blockModelName;
        QString modelName;
        QString internalPath;
        MainWindow::EconomicModelDefinition definition;
    };
    struct SurfaceOption
    {
        QString layerName;
        QMap<qint64, QString> dates;
    };
    struct Settings
    {
        QString economicModelKey;
        double startRevenueFactor = 0.2;
        double maxRevenueFactor = 1.0;
        double revenueFactorStep = 0.05;
        double globalSlopeAngleDeg = 45.0;
    };

    OpenPitOptimizationDialog(
        const QList<EconomicModelOption> &economicModels,
        const QList<SurfaceOption> &surfaces,
        QWidget *parent = nullptr)
        : QDialog(parent), m_economicModels(economicModels), m_surfaces(surfaces)
    {
        setWindowTitle("Open Pit Optimization");
        resize(1180, 760);
        setModal(true);

        QVBoxLayout *root = new QVBoxLayout(this);
        m_tabs = new QTabWidget(this);
        root->addWidget(m_tabs, 1);
        BuildEconomicTab();
        BuildSlopesTab();
        BuildIntangiblesTab();

        QDialogButtonBox *buttons = new QDialogButtonBox(this);
        QPushButton *runButton = buttons->addButton("Run Optimization", QDialogButtonBox::AcceptRole);
        runButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
        buttons->addButton("Help", QDialogButtonBox::HelpRole);
        root->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, [this]() { AcceptSettings(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        PopulateEconomicModels();
        PopulateSurfaces();
        if (!m_economicModels.isEmpty()) {
            LoadEconomicModel(0);
        }
        UpdateRevenueSummary();
    }

    Settings settings() const { return m_settings; }

private:
    void AcceptSettings()
    {
        if (!m_modelList || !m_modelList->currentItem()) {
            QMessageBox::warning(this, "Open Pit Optimization", "Select an economic model before running optimization.");
            return;
        }
        bool okStart = false;
        bool okMax = false;
        bool okStep = false;
        bool okSlope = false;
        const double start = m_startRfEdit->text().toDouble(&okStart);
        const double max = m_maxRfEdit->text().toDouble(&okMax);
        const double step = m_stepRfEdit->text().toDouble(&okStep);
        const double slope = m_globalSlopeEdit->text().toDouble(&okSlope);
        if (!okStart || !okMax || !okStep || step <= 0.0 || max < start) {
            QMessageBox::warning(this, "Open Pit Optimization", "Enter a valid revenue factor range.");
            return;
        }
        if (!okSlope || slope <= 0.0 || slope >= 90.0) {
            QMessageBox::warning(this, "Open Pit Optimization", "Enter a valid global slope angle between 0 and 90 degrees.");
            return;
        }
        m_settings.economicModelKey = m_modelList->currentItem()->data(Qt::UserRole).toString();
        m_settings.startRevenueFactor = start;
        m_settings.maxRevenueFactor = max;
        m_settings.revenueFactorStep = step;
        m_settings.globalSlopeAngleDeg = slope;
        accept();
    }

    void BuildEconomicTab()
    {
        QWidget *page = new QWidget(m_tabs);
        QHBoxLayout *layout = new QHBoxLayout(page);
        layout->setSpacing(18);

        QGroupBox *modelGroup = new QGroupBox("Economic Model", page);
        QVBoxLayout *modelLayout = new QVBoxLayout(modelGroup);
        modelLayout->addWidget(new QLabel("Select economic model", modelGroup));
        m_modelList = new QListWidget(modelGroup);
        modelLayout->addWidget(m_modelList, 1);
        layout->addWidget(modelGroup, 1);

        QGroupBox *rfGroup = new QGroupBox("Revenue Factors", page);
        QFormLayout *rfLayout = new QFormLayout(rfGroup);
        m_startRfEdit = new QLineEdit("0.20", rfGroup);
        m_maxRfEdit = new QLineEdit("1.00", rfGroup);
        m_stepRfEdit = new QLineEdit("0.05", rfGroup);
        QRegularExpressionValidator *rfValidator = new QRegularExpressionValidator(
            QRegularExpression(R"(^\s*\d+(?:\.\d{0,4})?\s*$)"),
            rfGroup);
        m_startRfEdit->setValidator(rfValidator);
        m_maxRfEdit->setValidator(rfValidator);
        m_stepRfEdit->setValidator(rfValidator);
        rfLayout->addRow("Start RF", m_startRfEdit);
        rfLayout->addRow("Max RF", m_maxRfEdit);
        rfLayout->addRow("RF Increment (Step)", m_stepRfEdit);
        m_shellsLabel = new QLabel("Estimated shells: 17", rfGroup);
        rfLayout->addRow(m_shellsLabel);
        layout->addWidget(rfGroup, 1);

        QGroupBox *surfaceGroup = new QGroupBox("Topography Surface", page);
        QFormLayout *surfaceLayout = new QFormLayout(surfaceGroup);
        m_surfaceCombo = new QComboBox(surfaceGroup);
        m_surfaceDateCombo = new QComboBox(surfaceGroup);
        surfaceLayout->addRow("Surface", m_surfaceCombo);
        surfaceLayout->addRow("Date", m_surfaceDateCombo);
        layout->addWidget(surfaceGroup, 1);

        connect(m_modelList, &QListWidget::currentRowChanged, this, [this](int row) { LoadEconomicModel(row); });
        connect(m_startRfEdit, &QLineEdit::textChanged, this, [this]() { UpdateRevenueSummary(); });
        connect(m_maxRfEdit, &QLineEdit::textChanged, this, [this]() { UpdateRevenueSummary(); });
        connect(m_stepRfEdit, &QLineEdit::textChanged, this, [this]() { UpdateRevenueSummary(); });
        connect(m_surfaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int row) {
            PopulateSurfaceDates(row - 1);
        });

        m_tabs->addTab(page, "Economic Model");
    }

    void BuildSlopesTab()
    {
        QWidget *page = new QWidget(m_tabs);
        QHBoxLayout *layout = new QHBoxLayout(page);

        QGroupBox *leftGroup = new QGroupBox("Slope Settings", page);
        QVBoxLayout *left = new QVBoxLayout(leftGroup);
        QFormLayout *form = new QFormLayout();
        m_globalSlopeEdit = new QLineEdit("45.0", leftGroup);
        m_globalSlopeEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(R"(^\s*\d+(?:\.\d{0,2})?\s*$)"), m_globalSlopeEdit));
        m_slopeFieldCombo = new QComboBox(leftGroup);
        form->addRow("Global Slope Angle (deg)", m_globalSlopeEdit);
        form->addRow("Slope Field", m_slopeFieldCombo);
        left->addLayout(form);
        m_slopeStatusLabel = new QLabel("Select an economic model.", leftGroup);
        left->addWidget(m_slopeStatusLabel);
        m_slopeTable = new QTableWidget(leftGroup);
        m_slopeTable->setColumnCount(2);
        m_slopeTable->setHorizontalHeaderLabels({"Slope Value", "Assigned Angle (deg)"});
        m_slopeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        left->addWidget(m_slopeTable, 1);
        layout->addWidget(leftGroup, 2);

        QGroupBox *summaryGroup = new QGroupBox("Slope Summary", page);
        QVBoxLayout *summary = new QVBoxLayout(summaryGroup);
        m_slopeSummaryLabel = new QLabel(summaryGroup);
        m_slopeSummaryLabel->setWordWrap(true);
        summary->addWidget(m_slopeSummaryLabel);
        summary->addStretch(1);
        layout->addWidget(summaryGroup, 1);

        connect(m_slopeFieldCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            LoadSlopeValuesAsync();
        });
        connect(m_globalSlopeEdit, &QLineEdit::textChanged, this, [this]() {
            UpdateSlopeSummary();
        });

        m_tabs->addTab(page, "Slopes");
    }

    void BuildIntangiblesTab()
    {
        QWidget *page = new QWidget(m_tabs);
        QHBoxLayout *layout = new QHBoxLayout(page);

        QGroupBox *fieldsGroup = new QGroupBox("Selected Attribute Fields", page);
        QVBoxLayout *fieldsLayout = new QVBoxLayout(fieldsGroup);
        m_attributeList = new QListWidget(fieldsGroup);
        fieldsLayout->addWidget(m_attributeList, 1);
        layout->addWidget(fieldsGroup, 1);

        QGroupBox *restrictionsGroup = new QGroupBox("Block Mining Restrictions", page);
        QVBoxLayout *restrictionsLayout = new QVBoxLayout(restrictionsGroup);
        m_intangibleStatusLabel = new QLabel("Select an attribute field.", restrictionsGroup);
        restrictionsLayout->addWidget(m_intangibleStatusLabel);
        m_intangibleTable = new QTableWidget(restrictionsGroup);
        m_intangibleTable->setColumnCount(3);
        m_intangibleTable->setHorizontalHeaderLabels({"Attribute Field", "Value", "Status"});
        m_intangibleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        restrictionsLayout->addWidget(m_intangibleTable, 1);
        layout->addWidget(restrictionsGroup, 2);

        QGroupBox *summaryGroup = new QGroupBox("Restriction Summary", page);
        QVBoxLayout *summary = new QVBoxLayout(summaryGroup);
        m_intangibleSummaryLabel = new QLabel(summaryGroup);
        m_intangibleSummaryLabel->setWordWrap(true);
        summary->addWidget(m_intangibleSummaryLabel);
        summary->addStretch(1);
        layout->addWidget(summaryGroup, 1);

        connect(m_attributeList, &QListWidget::currentTextChanged, this, [this]() {
            LoadAttributeValuesAsync();
        });

        m_tabs->addTab(page, "Intangibles");
    }

    void PopulateEconomicModels()
    {
        m_modelList->clear();
        for (const EconomicModelOption &option : m_economicModels) {
            QListWidgetItem *item = new QListWidgetItem(option.label);
            item->setData(Qt::UserRole, option.key);
            m_modelList->addItem(item);
        }
        if (m_modelList->count() > 0) {
            m_modelList->setCurrentRow(0);
        }
    }

    void PopulateSurfaces()
    {
        m_surfaceCombo->clear();
        m_surfaceCombo->addItem("None");
        for (const SurfaceOption &surface : m_surfaces) {
            m_surfaceCombo->addItem(surface.layerName);
        }
        PopulateSurfaceDates(-1);
    }

    void PopulateSurfaceDates(int surfaceIndex)
    {
        m_surfaceDateCombo->clear();
        if (surfaceIndex < 0 || surfaceIndex >= m_surfaces.size()) {
            m_surfaceDateCombo->addItem("No surface selected");
            m_surfaceDateCombo->setEnabled(false);
            return;
        }
        m_surfaceDateCombo->setEnabled(true);
        const SurfaceOption &surface = m_surfaces[surfaceIndex];
        for (auto it = surface.dates.constBegin(); it != surface.dates.constEnd(); ++it) {
            m_surfaceDateCombo->addItem(it.value(), QVariant::fromValue(it.key()));
        }
        if (m_surfaceDateCombo->count() == 0) {
            m_surfaceDateCombo->addItem("No dates");
            m_surfaceDateCombo->setEnabled(false);
        }
    }

    void LoadEconomicModel(int row)
    {
        if (row < 0 || row >= m_economicModels.size()) {
            m_currentModelIndex = -1;
            return;
        }
        m_currentModelIndex = row;
        const auto &definition = m_economicModels[row].definition;
        QStringList slopeFields;
        QStringList attributeFields;
        for (auto it = definition.fieldRoles.constBegin(); it != definition.fieldRoles.constEnd(); ++it) {
            if (it.value() == "Slope Region") {
                slopeFields << it.key();
            } else if (it.value() == "Attributes") {
                attributeFields << it.key();
            }
        }
        slopeFields.sort(Qt::CaseInsensitive);
        attributeFields.sort(Qt::CaseInsensitive);

        m_slopeFieldCombo->blockSignals(true);
        m_slopeFieldCombo->clear();
        m_slopeFieldCombo->addItems(slopeFields);
        m_slopeFieldCombo->setEnabled(!slopeFields.isEmpty());
        m_slopeFieldCombo->blockSignals(false);

        m_attributeList->clear();
        m_attributeList->addItems(attributeFields);
        if (m_attributeList->count() > 0) {
            m_attributeList->setCurrentRow(0);
        } else {
            m_intangibleTable->setRowCount(0);
            m_intangibleStatusLabel->setText("No attribute fields were assigned in the economic model.");
        }

        LoadSlopeValuesAsync();
        UpdateRevenueSummary();
        UpdateSlopeSummary();
    }

    void LoadSlopeValuesAsync()
    {
        if (m_currentModelIndex < 0 || m_currentModelIndex >= m_economicModels.size()) {
            return;
        }
        const QString field = m_slopeFieldCombo->currentText();
        if (field.isEmpty()) {
            m_slopeTable->setRowCount(0);
            m_slopeStatusLabel->setText("No slope field was assigned in the economic model.");
            UpdateSlopeSummary();
            return;
        }
        const EconomicModelOption option = m_economicModels[m_currentModelIndex];
        const QString cachedKey = option.definition.uniqueFieldValues.contains(field) ? field : QString();
        if (!cachedKey.isEmpty()) {
            PopulateSlopeValues(option.definition.uniqueFieldValues.value(cachedKey));
            return;
        }
        m_slopeTable->setRowCount(0);
        m_slopeStatusLabel->setText("Loading slope values...");
        QPointer<OpenPitOptimizationDialog> guard(this);
        const QString internalPath = option.internalPath;
        QThread *worker = QThread::create([guard, internalPath, field]() {
            const QStringList values = UniqueBlockModelFieldValues(internalPath, field);
            if (!guard) {
                return;
            }
            QMetaObject::invokeMethod(guard.data(), [guard, field, values]() {
                if (!guard) {
                    return;
                }
                if (guard->m_slopeFieldCombo->currentText() != field) {
                    return;
                }
                guard->PopulateSlopeValues(values);
            }, Qt::QueuedConnection);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    }

    void PopulateSlopeValues(const QStringList &values)
    {
        if (values.size() > 20) {
            m_slopeTable->setRowCount(0);
            m_slopeStatusLabel->setText("The maximum number of unique slope values has been exceeded.");
            UpdateSlopeSummary();
            return;
        }
        m_slopeStatusLabel->setText(values.isEmpty() ? "No slope values were found." : "Slope values loaded.");
        m_slopeTable->setRowCount(0);
        for (const QString &value : values) {
            const int row = m_slopeTable->rowCount();
            m_slopeTable->insertRow(row);
            m_slopeTable->setItem(row, 0, new QTableWidgetItem(value));
            m_slopeTable->setItem(row, 1, new QTableWidgetItem(m_globalSlopeEdit->text()));
        }
        UpdateSlopeSummary();
    }

    void LoadAttributeValuesAsync()
    {
        if (m_currentModelIndex < 0 || m_currentModelIndex >= m_economicModels.size()) {
            return;
        }
        const QString field = m_attributeList->currentItem() ? m_attributeList->currentItem()->text() : QString();
        if (field.isEmpty()) {
            return;
        }
        const EconomicModelOption option = m_economicModels[m_currentModelIndex];
        if (option.definition.uniqueFieldValues.contains(field)) {
            PopulateAttributeValues(field, option.definition.uniqueFieldValues.value(field));
            return;
        }
        m_intangibleTable->setRowCount(0);
        m_intangibleStatusLabel->setText("Loading attribute values...");
        QPointer<OpenPitOptimizationDialog> guard(this);
        const QString internalPath = option.internalPath;
        QThread *worker = QThread::create([guard, internalPath, field]() {
            const QStringList values = UniqueBlockModelFieldValues(internalPath, field);
            if (!guard) {
                return;
            }
            QMetaObject::invokeMethod(guard.data(), [guard, field, values]() {
                if (!guard) {
                    return;
                }
                const QString current = guard->m_attributeList->currentItem()
                    ? guard->m_attributeList->currentItem()->text()
                    : QString();
                if (current != field) {
                    return;
                }
                guard->PopulateAttributeValues(field, values);
            }, Qt::QueuedConnection);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    }

    void PopulateAttributeValues(const QString &field, const QStringList &values)
    {
        m_intangibleStatusLabel->setText(values.isEmpty() ? "No attribute values were found." : "Attribute values loaded.");
        m_intangibleTable->setRowCount(0);
        for (const QString &value : values) {
            const int row = m_intangibleTable->rowCount();
            m_intangibleTable->insertRow(row);
            m_intangibleTable->setItem(row, 0, new QTableWidgetItem(field));
            m_intangibleTable->setItem(row, 1, new QTableWidgetItem(value));
            QComboBox *statusCombo = new QComboBox(m_intangibleTable);
            statusCombo->addItems({"Minable", "Non-minable"});
            m_intangibleTable->setCellWidget(row, 2, statusCombo);
            connect(statusCombo, &QComboBox::currentTextChanged, this, [this]() { UpdateIntangibleSummary(); });
        }
        UpdateIntangibleSummary();
    }

    void UpdateRevenueSummary()
    {
        bool okStart = false;
        bool okMax = false;
        bool okStep = false;
        const double start = m_startRfEdit ? m_startRfEdit->text().toDouble(&okStart) : 0.0;
        const double max = m_maxRfEdit ? m_maxRfEdit->text().toDouble(&okMax) : 0.0;
        const double step = m_stepRfEdit ? m_stepRfEdit->text().toDouble(&okStep) : 0.0;
        int shells = 0;
        if (okStart && okMax && okStep && step > 0.0 && max >= start) {
            shells = static_cast<int>(std::floor((max - start) / step + 0.5)) + 1;
        }
        if (m_shellsLabel) {
            m_shellsLabel->setText(QString("Estimated shells: %1").arg(shells));
        }
    }

    void UpdateSlopeSummary()
    {
        if (!m_slopeSummaryLabel) {
            return;
        }
        m_slopeSummaryLabel->setText(QString(
            "Global slope angle (default)\n%1 deg\n\nSlope field\n%2\n\nMapped slope categories\n%3")
            .arg(m_globalSlopeEdit ? m_globalSlopeEdit->text() : "45.0",
                 m_slopeFieldCombo && !m_slopeFieldCombo->currentText().isEmpty() ? m_slopeFieldCombo->currentText() : "-")
            .arg(m_slopeTable ? m_slopeTable->rowCount() : 0));
    }

    void UpdateIntangibleSummary()
    {
        int minable = 0;
        int nonMinable = 0;
        if (m_intangibleTable) {
            for (int row = 0; row < m_intangibleTable->rowCount(); ++row) {
                QComboBox *combo = qobject_cast<QComboBox *>(m_intangibleTable->cellWidget(row, 2));
                if (combo && combo->currentText() == "Non-minable") {
                    ++nonMinable;
                } else {
                    ++minable;
                }
            }
        }
        if (m_intangibleSummaryLabel) {
            m_intangibleSummaryLabel->setText(QString(
                "Restricted (Non-minable)\n%1\n\nAllowed (Minable)\n%2\n\nTotal mapped\n%3")
                .arg(nonMinable)
                .arg(minable)
                .arg(nonMinable + minable));
        }
    }

    QList<EconomicModelOption> m_economicModels;
    QList<SurfaceOption> m_surfaces;
    Settings m_settings;
    int m_currentModelIndex = -1;
    QTabWidget *m_tabs = nullptr;
    QListWidget *m_modelList = nullptr;
    QLineEdit *m_startRfEdit = nullptr;
    QLineEdit *m_maxRfEdit = nullptr;
    QLineEdit *m_stepRfEdit = nullptr;
    QLabel *m_shellsLabel = nullptr;
    QComboBox *m_surfaceCombo = nullptr;
    QComboBox *m_surfaceDateCombo = nullptr;
    QLineEdit *m_globalSlopeEdit = nullptr;
    QComboBox *m_slopeFieldCombo = nullptr;
    QLabel *m_slopeStatusLabel = nullptr;
    QTableWidget *m_slopeTable = nullptr;
    QLabel *m_slopeSummaryLabel = nullptr;
    QListWidget *m_attributeList = nullptr;
    QLabel *m_intangibleStatusLabel = nullptr;
    QTableWidget *m_intangibleTable = nullptr;
    QLabel *m_intangibleSummaryLabel = nullptr;
};
