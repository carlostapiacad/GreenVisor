#pragma once

#include "gui/MainWindow.h"
#include "features/block_model/engine/BlockModelFieldUtils.h"
#include "features/pit_optimization/dialogs/EconomicVariablesDialog.h"
#include "features/pit_optimization/engine/EconomicFormula.h"
#include "features/pit_optimization/widgets/EconomicTableWidgets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QInputDialog>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
class CreateEconomicModelDialog : public QDialog
{
public:
    CreateEconomicModelDialog(
        const QStringList &blockModelNames,
        const QHash<QString, QStringList> &fieldsByBlockModel,
        const QHash<QString, QString> &internalPathsByBlockModel,
        const QString &initialBlockModel,
        QWidget *parent = nullptr,
        bool editMode = false,
        const QHash<QString, QStringList> &economicModelsByBlockModel = {},
        const QHash<QString, MainWindow::EconomicModelDefinition> &definitionsByKey = {},
        const QString &initialEconomicModel = QString())
        : QDialog(parent),
          m_editMode(editMode),
          m_fieldsByBlockModel(fieldsByBlockModel),
          m_internalPathsByBlockModel(internalPathsByBlockModel),
          m_economicModelsByBlockModel(economicModelsByBlockModel),
          m_definitionsByKey(definitionsByKey)
    {
        setWindowTitle(editMode ? "Modify Economic Model" : "Economic Model Import");
        setModal(true);
        resize(980, 660);

        QVBoxLayout *root = new QVBoxLayout(this);
        m_tabs = new QTabWidget(this);

        QWidget *definitionPage = new QWidget(m_tabs);
        QVBoxLayout *definitionLayout = new QVBoxLayout(definitionPage);
        definitionLayout->setContentsMargins(10, 10, 10, 10);
        definitionLayout->setSpacing(10);

        QFormLayout *topForm = new QFormLayout();
        m_blockModelCombo = new QComboBox(definitionPage);
        m_blockModelCombo->addItems(blockModelNames);
        topForm->addRow("Block Model", m_blockModelCombo);
        if (m_editMode) {
            m_economicModelCombo = new QComboBox(definitionPage);
            topForm->addRow("Economic Model", m_economicModelCombo);
        } else {
            m_nameEdit = new QLineEdit(definitionPage);
            topForm->addRow("Economic Model Name", m_nameEdit);
        }
        definitionLayout->addLayout(topForm);

        QLabel *caption = new QLabel("Map block model fields to economic model roles", definitionPage);
        definitionLayout->addWidget(caption);

        m_fieldsTable = new QTableWidget(definitionPage);
        m_fieldsTable->setColumnCount(3);
        m_fieldsTable->setHorizontalHeaderLabels({"Field Name", "Role", "Unit"});
        m_fieldsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_fieldsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_fieldsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        QFont fieldHeaderFont = m_fieldsTable->horizontalHeader()->font();
        fieldHeaderFont.setBold(false);
        m_fieldsTable->horizontalHeader()->setFont(fieldHeaderFont);
        m_fieldsTable->verticalHeader()->setVisible(false);
        m_fieldsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_fieldsTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_fieldsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        definitionLayout->addWidget(m_fieldsTable, 1);

        m_tabs->addTab(definitionPage, "Definition");
        m_tabs->addTab(BuildOreDestinationsPage(), "Ore Destinations");
        m_tabs->addTab(BuildReviewPage(), "Review");
        root->addWidget(m_tabs, 1);

        QHBoxLayout *variableTools = new QHBoxLayout();
        QPushButton *setVariablesButton = new QPushButton("Set Variables", this);
        connect(setVariablesButton, &QPushButton::clicked, this, [this]() {
            SetEconomicVariablesDialog dialog(AssignedRoleFields(), m_variables, this);
            if (dialog.exec() == QDialog::Accepted) {
                m_variables = dialog.variables();
                RebuildOreDestinationTable();
            }
        });
        variableTools->addStretch(1);
        variableTools->addWidget(setVariablesButton);
        root->addLayout(variableTools);

        QDialogButtonBox *buttons = new QDialogButtonBox(this);
        buttons->addButton(m_editMode ? "Update Economic Model" : "Create Economic Model", QDialogButtonBox::AcceptRole);
        buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (blockModelName().isEmpty()) {
                QMessageBox::warning(this, "Economic Model", "Select a block model.");
                return;
            }
            if (economicModelName().isEmpty()) {
                QMessageBox::warning(this, "Economic Model", "Economic model name is required.");
                return;
            }
            QString roleError;
            if (!ValidateRequiredRoles(&roleError)) {
                QMessageBox::warning(this, "Economic Model", roleError);
                return;
            }
            SaveCurrentRockTypeState();
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);

        connect(m_blockModelCombo, &QComboBox::currentTextChanged, this, [this](const QString &name) {
            if (!m_editMode && m_nameEdit && m_nameEdit->text().trimmed().isEmpty()) {
                m_nameEdit->setText(name + "_Economic");
            }
            PopulateFields(name);
            PopulateEconomicModelCombo(name);
            if (m_editMode) {
                LoadSelectedEconomicModel();
            } else {
                RebuildOreDestinationControls();
            }
            RebuildReviewPage();
        });

        if (m_economicModelCombo) {
            connect(m_economicModelCombo, &QComboBox::currentTextChanged, this, [this]() {
                LoadSelectedEconomicModel();
            });
        }

        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
            if (m_tabs && m_tabs->tabText(index) == "Review") {
                QString roleError;
                if (!ValidateRequiredRoles(&roleError)) {
                    QMessageBox::warning(this, "Economic Model", roleError);
                }
                RebuildReviewPage();
            }
        });

        const int initialIndex = initialBlockModel.isEmpty()
            ? 0
            : m_blockModelCombo->findText(initialBlockModel, Qt::MatchFixedString);
        if (initialIndex >= 0) {
            m_blockModelCombo->setCurrentIndex(initialIndex);
        }
        if (m_blockModelCombo->count() > 0) {
            const QString current = m_blockModelCombo->currentText();
            if (!m_editMode && m_nameEdit && m_nameEdit->text().trimmed().isEmpty()) {
                m_nameEdit->setText(current + "_Economic");
            }
            PopulateFields(current);
            PopulateEconomicModelCombo(current);
            if (m_editMode) {
                const int economicIndex = m_economicModelCombo
                    ? m_economicModelCombo->findText(initialEconomicModel, Qt::MatchFixedString)
                    : -1;
                if (economicIndex >= 0) {
                    m_economicModelCombo->setCurrentIndex(economicIndex);
                }
                LoadSelectedEconomicModel();
            } else {
                RebuildOreDestinationControls();
            }
        }
    }

    QString blockModelName() const
    {
        return m_blockModelCombo ? m_blockModelCombo->currentText().trimmed() : QString();
    }

    QString economicModelName() const
    {
        if (m_editMode) {
            return m_economicModelCombo ? m_economicModelCombo->currentText().trimmed() : QString();
        }
        return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
    }

    QHash<QString, QString> fieldRoles() const
    {
        QHash<QString, QString> roles;
        if (!m_fieldsTable) {
            return roles;
        }
        for (int row = 0; row < m_fieldsTable->rowCount(); ++row) {
            QTableWidgetItem *fieldItem = m_fieldsTable->item(row, 0);
            QComboBox *roleCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 1));
            if (!fieldItem || !roleCombo) {
                continue;
            }
            roles.insert(fieldItem->text(), roleCombo->currentText());
        }
        return roles;
    }

    QHash<QString, QString> fieldUnits() const
    {
        QHash<QString, QString> units;
        if (!m_fieldsTable) {
            return units;
        }
        for (int row = 0; row < m_fieldsTable->rowCount(); ++row) {
            QTableWidgetItem *fieldItem = m_fieldsTable->item(row, 0);
            QComboBox *unitCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 2));
            if (fieldItem && unitCombo) {
                units.insert(fieldItem->text(), unitCombo->currentText());
            }
        }
        return units;
    }

    bool profitModel() const
    {
        return m_profitModelCheck && m_profitModelCheck->isChecked();
    }

    QString profitField() const
    {
        return m_profitFieldCombo ? m_profitFieldCombo->currentText().trimmed() : QString();
    }

    QList<MainWindow::EconomicModelDefinition::Variable> economicVariables() const
    {
        return m_variables;
    }

    QHash<QString, QStringList> economicUniqueFieldValues() const
    {
        QHash<QString, QStringList> values;
        const QString blockModel = blockModelName();
        for (auto it = m_rockTypeValueCache.constBegin(); it != m_rockTypeValueCache.constEnd(); ++it) {
            const QString prefix = blockModel + "/";
            if (it.key().startsWith(prefix)) {
                values.insert(it.key().mid(prefix.size()), it.value());
            }
        }
        return values;
    }

    QList<MainWindow::EconomicModelDefinition::RockTypeSettings> economicRockTypeSettings() const
    {
        QList<MainWindow::EconomicModelDefinition::RockTypeSettings> result;
        if (m_destinationTable && m_rockTypeCombo) {
            const_cast<CreateEconomicModelDialog *>(this)->SaveCurrentRockTypeState();
        }
        QStringList keys = m_rockTypeStates.keys();
        keys.sort(Qt::CaseInsensitive);
        for (const QString &rockType : keys) {
            const RockTypeState state = m_rockTypeStates.value(rockType);
            MainWindow::EconomicModelDefinition::RockTypeSettings saved;
            saved.rockType = rockType;
            saved.dilution = state.dilution;
            saved.miningRecovery = state.miningRecovery;
            saved.miningCost = state.miningCost;
            for (const DestinationState &destination : state.destinations) {
                MainWindow::EconomicModelDefinition::Destination savedDestination;
                savedDestination.enabled = destination.enabled;
                savedDestination.name = destination.name;
                savedDestination.processingCost = destination.processingCost;
                savedDestination.productValues = destination.productValues;
                saved.destinations.append(savedDestination);
            }
            result.append(saved);
        }
        return result;
    }

private:
    struct DestinationState
    {
        bool enabled = true;
        QString name;
        double processingCost = 0.0;
        QStringList productValues;
    };

    struct RockTypeState
    {
        double dilution = 0.0;
        double miningRecovery = 100.0;
        double miningCost = 0.0;
        QList<DestinationState> destinations;
    };

    void PopulateEconomicModelCombo(const QString &blockModelName)
    {
        if (!m_economicModelCombo) {
            return;
        }
        const QString current = m_economicModelCombo->currentText();
        m_economicModelCombo->blockSignals(true);
        m_economicModelCombo->clear();
        QStringList models = m_economicModelsByBlockModel.value(blockModelName);
        models.sort(Qt::CaseInsensitive);
        m_economicModelCombo->addItems(models);
        const int idx = m_economicModelCombo->findText(current, Qt::MatchFixedString);
        if (idx >= 0) {
            m_economicModelCombo->setCurrentIndex(idx);
        }
        m_economicModelCombo->blockSignals(false);
    }

    void LoadSelectedEconomicModel()
    {
        if (!m_editMode || !m_economicModelCombo) {
            return;
        }
        const QString modelName = m_economicModelCombo->currentText().trimmed();
        if (modelName.isEmpty()) {
            return;
        }
        const QString key = EconomicModelKey(blockModelName(), modelName);
        if (!m_definitionsByKey.contains(key)) {
            return;
        }
        LoadDefinition(m_definitionsByKey.value(key));
    }

    void LoadDefinition(const MainWindow::EconomicModelDefinition &definition)
    {
        if (m_loadingDefinition) {
            return;
        }
        m_loadingDefinition = true;
        m_rockTypeValueCache.clear();
        m_rockTypeStates.clear();
        m_loadedRockType.clear();
        const QString definitionBlockModel = definition.blockModelLayerName.isEmpty()
            ? blockModelName()
            : definition.blockModelLayerName;
        for (auto it = definition.uniqueFieldValues.constBegin(); it != definition.uniqueFieldValues.constEnd(); ++it) {
            if (!it.key().isEmpty() && !it.value().isEmpty()) {
                m_rockTypeValueCache.insert(definitionBlockModel + "/" + it.key(), it.value());
            }
        }

        for (int row = 0; m_fieldsTable && row < m_fieldsTable->rowCount(); ++row) {
            QTableWidgetItem *fieldItem = m_fieldsTable->item(row, 0);
            if (!fieldItem) {
                continue;
            }
            const QString field = fieldItem->text();
            QComboBox *roleCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 1));
            if (roleCombo) {
                roleCombo->setCurrentText(definition.fieldRoles.value(field, "None"));
            }
            QComboBox *unitCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 2));
            if (unitCombo && definition.fieldUnits.contains(field)) {
                unitCombo->setCurrentText(definition.fieldUnits.value(field));
            }
        }

        m_variables = definition.variables;
        for (const auto &savedRockType : definition.rockTypeSettings) {
            RockTypeState state;
            state.dilution = savedRockType.dilution;
            state.miningRecovery = savedRockType.miningRecovery;
            state.miningCost = savedRockType.miningCost;
            for (const auto &savedDestination : savedRockType.destinations) {
                DestinationState destination;
                destination.enabled = savedDestination.enabled;
                destination.name = savedDestination.name;
                destination.processingCost = savedDestination.processingCost;
                destination.productValues = savedDestination.productValues;
                state.destinations.append(destination);
            }
            if (!savedRockType.rockType.isEmpty()) {
                m_rockTypeStates.insert(savedRockType.rockType, state);
            }
        }
        const QString rockTypeField = definition.fieldRoles.key("Rock Type");
        if (!rockTypeField.isEmpty()) {
            const QString cacheKey = definitionBlockModel + "/" + rockTypeField;
            if (!m_rockTypeValueCache.contains(cacheKey)) {
                QStringList values;
                for (const auto &savedRockType : definition.rockTypeSettings) {
                    if (!savedRockType.rockType.isEmpty()) {
                        values << savedRockType.rockType;
                    }
                }
                values.removeDuplicates();
                values.sort(Qt::CaseInsensitive);
                if (!values.isEmpty()) {
                    m_rockTypeValueCache.insert(cacheKey, values);
                }
            }
        }
        if (m_profitModelCheck) {
            m_profitModelCheck->setChecked(definition.profitModel);
        }
        RebuildOreDestinationControls();
        if (m_profitFieldCombo && !definition.profitField.isEmpty()) {
            const int idx = m_profitFieldCombo->findText(definition.profitField, Qt::MatchFixedString);
            if (idx >= 0) {
                m_profitFieldCombo->setCurrentIndex(idx);
            }
        }
        RebuildReviewPage();
        m_loadingDefinition = false;
    }

    void ClearExclusiveRoleFromOtherRows(const QString &role, int keepRow)
    {
        if (role != "Rock Type" && role != "Density") {
            return;
        }
        if (!m_fieldsTable || m_applyingExclusiveRole || m_loadingDefinition) {
            return;
        }
        m_applyingExclusiveRole = true;
        for (int row = 0; row < m_fieldsTable->rowCount(); ++row) {
            if (row == keepRow) {
                continue;
            }
            QComboBox *roleCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 1));
            if (roleCombo && roleCombo->currentText() == role) {
                roleCombo->setCurrentText("None");
            }
        }
        m_applyingExclusiveRole = false;
    }

    QWidget *BuildOreDestinationsPage()
    {
        QWidget *page = new QWidget(m_tabs);
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(10);

        m_profitModelCheck = new QCheckBox("Profit Model", page);
        root->addWidget(m_profitModelCheck);

        m_profitPanel = new QWidget(page);
        QFormLayout *profitLayout = new QFormLayout(m_profitPanel);
        m_profitFieldCombo = new QComboBox(m_profitPanel);
        profitLayout->addRow("Select Field", m_profitFieldCombo);
        root->addWidget(m_profitPanel);

        m_orePanel = new QWidget(page);
        QVBoxLayout *oreLayout = new QVBoxLayout(m_orePanel);
        oreLayout->setContentsMargins(0, 0, 0, 0);
        oreLayout->setSpacing(10);

        QGridLayout *top = new QGridLayout();
        top->setHorizontalSpacing(12);
        m_rockTypeCombo = new QComboBox(m_orePanel);
        m_dilutionSpin = new QDoubleSpinBox(m_orePanel);
        m_dilutionSpin->setRange(0.0, 100.0);
        m_dilutionSpin->setDecimals(2);
        m_dilutionSpin->setSuffix(" %");
        m_dilutionSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_miningRecoverySpin = new QDoubleSpinBox(m_orePanel);
        m_miningRecoverySpin->setRange(0.0, 100.0);
        m_miningRecoverySpin->setDecimals(2);
        m_miningRecoverySpin->setSuffix(" %");
        m_miningRecoverySpin->setValue(100.0);
        m_miningRecoverySpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_miningCostSpin = new QDoubleSpinBox(m_orePanel);
        m_miningCostSpin->setRange(0.0, 1e9);
        m_miningCostSpin->setDecimals(2);
        m_miningCostSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        top->addWidget(new QLabel("Selected Rock Type", m_orePanel), 0, 0);
        top->addWidget(m_rockTypeCombo, 0, 1);
        top->addWidget(new QLabel("Dilution", m_orePanel), 0, 2);
        top->addWidget(m_dilutionSpin, 0, 3);
        top->addWidget(new QLabel("Mining Recovery", m_orePanel), 0, 4);
        top->addWidget(m_miningRecoverySpin, 0, 5);
        top->addWidget(new QLabel("Mining Cost", m_orePanel), 1, 0);
        top->addWidget(m_miningCostSpin, 1, 1);
        top->setColumnStretch(6, 1);
        oreLayout->addLayout(top);

        oreLayout->addWidget(new QLabel("Ore Destinations for selected rock type", m_orePanel));
        m_destinationTable = new QTableWidget(m_orePanel);
        ProductGroupHeader *productHeader = new ProductGroupHeader(Qt::Horizontal, m_destinationTable);
        m_destinationTable->setHorizontalHeader(productHeader);
        m_destinationTable->verticalHeader()->setVisible(false);
        m_destinationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_destinationTable->setSelectionMode(QAbstractItemView::SingleSelection);
        QFont destinationHeaderFont = m_destinationTable->horizontalHeader()->font();
        destinationHeaderFont.setBold(false);
        m_destinationTable->horizontalHeader()->setFont(destinationHeaderFont);
        oreLayout->addWidget(m_destinationTable, 1);

        QHBoxLayout *buttons = new QHBoxLayout();
        QPushButton *addButton = new QPushButton("Add Destination", m_orePanel);
        QPushButton *removeButton = new QPushButton("Remove Destination", m_orePanel);
        QPushButton *copyButton = new QPushButton("Copy From Rock Type", m_orePanel);
        connect(addButton, &QPushButton::clicked, this, [this]() {
            SaveCurrentRockTypeState();
            AddDestinationRow(QString("Destination %1").arg(std::max(1, m_destinationTable->rowCount())));
        });
        connect(removeButton, &QPushButton::clicked, this, [this]() {
            const int row = m_destinationTable ? m_destinationTable->currentRow() : -1;
            if (row <= 0) {
                return;
            }
            m_destinationTable->removeRow(row);
        });
        connect(copyButton, &QPushButton::clicked, this, [this]() {
            CopyFromRockType();
        });
        buttons->addWidget(addButton);
        buttons->addWidget(removeButton);
        buttons->addWidget(copyButton);
        buttons->addStretch(1);
        oreLayout->addLayout(buttons);

        root->addWidget(m_orePanel, 1);

        connect(m_profitModelCheck, &QCheckBox::toggled, this, [this](bool checked) {
            if (m_orePanel) {
                m_orePanel->setVisible(!checked);
            }
            if (m_profitPanel) {
                m_profitPanel->setVisible(checked);
            }
            RebuildOreDestinationControls();
        });
        connect(m_rockTypeCombo, &QComboBox::currentTextChanged, this, [this](const QString &name) {
            if (m_loadingRockType) {
                return;
            }
            SaveCurrentRockTypeState();
            LoadRockTypeState(name);
        });

        m_profitPanel->hide();
        return page;
    }

    QWidget *BuildReviewPage()
    {
        QWidget *page = new QWidget(m_tabs);
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(10);

        QLabel *description = new QLabel(
            "Review a sample block by entering values and checking the calculated destination and value.",
            page);
        root->addWidget(description);

        QHBoxLayout *body = new QHBoxLayout();

        QGroupBox *inputGroup = new QGroupBox("Input Values", page);
        QVBoxLayout *inputOuter = new QVBoxLayout(inputGroup);
        QScrollArea *inputScroll = new QScrollArea(inputGroup);
        inputScroll->setWidgetResizable(true);
        inputScroll->setFrameShape(QFrame::NoFrame);
        QWidget *inputContent = new QWidget(inputScroll);
        m_reviewInputForm = new QFormLayout(inputContent);
        m_reviewInputForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        inputScroll->setWidget(inputContent);
        inputOuter->addWidget(inputScroll);
        body->addWidget(inputGroup, 1);

        QGroupBox *evaluationGroup = new QGroupBox("Destination Evaluation", page);
        QVBoxLayout *evaluationLayout = new QVBoxLayout(evaluationGroup);
        m_reviewTable = new QTableWidget(evaluationGroup);
        m_reviewTable->setColumnCount(5);
        m_reviewTable->setHorizontalHeaderLabels({"Destination", "Revenue ($)", "Mining Cost ($)", "Processing Cost ($)", "Value ($)"});
        m_reviewTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_reviewTable->verticalHeader()->setVisible(false);
        m_reviewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_reviewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_reviewTable->setSelectionMode(QAbstractItemView::SingleSelection);
        evaluationLayout->addWidget(m_reviewTable, 1);
        QLabel *note = new QLabel("Values are calculated per tonne of material. Negative values are not economic.", evaluationGroup);
        evaluationLayout->addWidget(note);
        body->addWidget(evaluationGroup, 3);

        QGroupBox *resultGroup = new QGroupBox("Calculated Result", page);
        QFormLayout *resultLayout = new QFormLayout(resultGroup);
        m_reviewBestDestinationLabel = new QLabel("-", resultGroup);
        m_reviewBestDestinationLabel->setObjectName("reviewBestDestinationLabel");
        QFont bestFont = m_reviewBestDestinationLabel->font();
        bestFont.setPointSize(bestFont.pointSize() + 5);
        bestFont.setBold(true);
        m_reviewBestDestinationLabel->setFont(bestFont);
        m_reviewRevenueLabel = new QLabel("$0.00", resultGroup);
        m_reviewMiningCostLabel = new QLabel("$0.00", resultGroup);
        m_reviewProcessingCostLabel = new QLabel("$0.00", resultGroup);
        m_reviewBlockValueLabel = new QLabel("$0.00", resultGroup);
        QFont valueFont = m_reviewBlockValueLabel->font();
        valueFont.setPointSize(valueFont.pointSize() + 5);
        valueFont.setBold(true);
        m_reviewBlockValueLabel->setFont(valueFont);
        m_reviewDilutionLabel = new QLabel("0.00 %", resultGroup);
        m_reviewMiningRecoveryLabel = new QLabel("100.00 %", resultGroup);
        m_reviewRockTypeLabel = new QLabel("-", resultGroup);
        resultLayout->addRow("Best Destination", m_reviewBestDestinationLabel);
        resultLayout->addRow("Revenue", m_reviewRevenueLabel);
        resultLayout->addRow("Mining Cost", m_reviewMiningCostLabel);
        resultLayout->addRow("Processing Cost", m_reviewProcessingCostLabel);
        resultLayout->addRow("Block Value", m_reviewBlockValueLabel);
        resultLayout->addRow("Dilution", m_reviewDilutionLabel);
        resultLayout->addRow("Mining Recovery", m_reviewMiningRecoveryLabel);
        resultLayout->addRow("Selected Rock Type", m_reviewRockTypeLabel);
        body->addWidget(resultGroup, 1);

        root->addLayout(body, 1);

        QHBoxLayout *actions = new QHBoxLayout();
        QPushButton *resetButton = new QPushButton("Reset", page);
        QPushButton *calculateButton = new QPushButton("Calculate", page);
        connect(resetButton, &QPushButton::clicked, this, [this]() {
            ResetReviewInputs();
        });
        connect(calculateButton, &QPushButton::clicked, this, [this]() {
            CalculateReview();
        });
        actions->addWidget(resetButton);
        actions->addWidget(calculateButton);
        actions->addStretch(1);
        root->addLayout(actions);

        return page;
    }

    void PopulateFields(const QString &blockModelName)
    {
        if (!m_fieldsTable) {
            return;
        }
        const QStringList roles = {
            "None", "Product", "Density", "Rock Type", "Attributes", "Slope Region", "Category"};
        const QStringList fields = m_fieldsByBlockModel.value(blockModelName);
        m_rockTypeValueCache.clear();
        m_rockTypeStates.clear();
        m_fieldsTable->setRowCount(fields.size());
        for (int row = 0; row < fields.size(); ++row) {
            QTableWidgetItem *fieldItem = new QTableWidgetItem(fields[row]);
            fieldItem->setFlags(fieldItem->flags() & ~Qt::ItemIsEditable);
            m_fieldsTable->setItem(row, 0, fieldItem);

            QComboBox *roleCombo = new QComboBox(m_fieldsTable);
            roleCombo->addItems(roles);
            roleCombo->setCurrentText("None");
            m_fieldsTable->setCellWidget(row, 1, roleCombo);

            QComboBox *unitCombo = new QComboBox(m_fieldsTable);
            unitCombo->addItems({"Percentage", "Concentration (g/t)", "Mass (g)", "Mass (oz)", "Mass (lb)"});
            unitCombo->hide();
            QTableWidgetItem *unitItem = new QTableWidgetItem();
            unitItem->setFlags(unitItem->flags() & ~Qt::ItemIsEditable);
            m_fieldsTable->setItem(row, 2, unitItem);

            connect(roleCombo, &QComboBox::currentTextChanged, this, [this, row, unitCombo](const QString &role) {
                ClearExclusiveRoleFromOtherRows(role, row);
                if (role == "Product") {
                    m_fieldsTable->takeItem(row, 2);
                    m_fieldsTable->setCellWidget(row, 2, unitCombo);
                    unitCombo->show();
                } else {
                    m_fieldsTable->removeCellWidget(row, 2);
                    unitCombo->hide();
                    QTableWidgetItem *emptyItem = new QTableWidgetItem();
                    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsEditable);
                    m_fieldsTable->setItem(row, 2, emptyItem);
                }
                if (!m_loadingDefinition) {
                    RebuildOreDestinationControls();
                    RebuildReviewPage();
                }
            });
            connect(unitCombo, &QComboBox::currentTextChanged, this, [this]() {
                if (!m_loadingDefinition) {
                    RebuildOreDestinationTable();
                    RebuildReviewPage();
                }
            });
        }
    }

    QStringList FieldsForRole(const QString &role) const
    {
        QStringList fields;
        if (!m_fieldsTable) {
            return fields;
        }
        for (int row = 0; row < m_fieldsTable->rowCount(); ++row) {
            QTableWidgetItem *fieldItem = m_fieldsTable->item(row, 0);
            QComboBox *roleCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 1));
            if (fieldItem && roleCombo && roleCombo->currentText() == role) {
                fields << fieldItem->text();
            }
        }
        fields.sort(Qt::CaseInsensitive);
        return fields;
    }

    QStringList AssignedRoleFields() const
    {
        QStringList fields;
        if (!m_fieldsTable) {
            return fields;
        }
        for (int row = 0; row < m_fieldsTable->rowCount(); ++row) {
            QTableWidgetItem *fieldItem = m_fieldsTable->item(row, 0);
            QComboBox *roleCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 1));
            if (fieldItem && roleCombo && roleCombo->currentText() != "None") {
                fields << fieldItem->text();
            }
        }
        fields.removeDuplicates();
        fields.sort(Qt::CaseInsensitive);
        return fields;
    }

    QStringList FormulaValueChoices() const
    {
        QStringList choices;
        for (const auto &variable : m_variables) {
            const QString name = variable.name.trimmed();
            if (!name.isEmpty()) {
                choices << name;
            }
        }
        choices.sort(Qt::CaseInsensitive);
        return choices;
    }

    bool ValidateRequiredRoles(QString *error) const
    {
        const int productCount = FieldsForRole("Product").size();
        const int densityCount = FieldsForRole("Density").size();
        const int rockTypeCount = FieldsForRole("Rock Type").size();
        if (productCount < 1 || densityCount != 1 || rockTypeCount != 1) {
            if (error) {
                *error = "Please finish selecting roles before continuing. The economic model requires at least one Product, exactly one Density field, and exactly one Rock Type field.";
            }
            return false;
        }
        return true;
    }

    QStringList UniqueValuesForField(const QString &field)
    {
        if (field.isEmpty()) {
            return {};
        }
        const QString blockModel = blockModelName();
        const QString cacheKey = blockModel + "/" + field;
        QStringList values = m_rockTypeValueCache.value(cacheKey);
        if (values.isEmpty() && !m_rockTypeValueCache.contains(cacheKey)) {
            values = UniqueBlockModelFieldValues(m_internalPathsByBlockModel.value(blockModel), field);
            m_rockTypeValueCache.insert(cacheKey, values);
        }
        return values;
    }

    void ClearReviewInputs()
    {
        m_reviewInputWidgets.clear();
        if (!m_reviewInputForm) {
            return;
        }
        while (m_reviewInputForm->rowCount() > 0) {
            m_reviewInputForm->removeRow(0);
        }
    }

    void AddReviewLineEdit(const QString &field)
    {
        QLineEdit *edit = new QLineEdit(m_reviewInputForm->parentWidget());
        m_reviewInputForm->addRow(field, edit);
        m_reviewInputWidgets.insert(field, edit);
    }

    void AddReviewCombo(const QString &field, const QStringList &values)
    {
        QComboBox *combo = new QComboBox(m_reviewInputForm->parentWidget());
        combo->setEditable(true);
        combo->addItems(values);
        m_reviewInputForm->addRow(field, combo);
        m_reviewInputWidgets.insert(field, combo);
    }

    void RebuildReviewPage()
    {
        if (!m_reviewInputForm || !m_reviewTable) {
            return;
        }
        SaveCurrentRockTypeState();
        ClearReviewInputs();
        m_reviewTable->setRowCount(0);
        ResetReviewResultLabels();

        QString roleError;
        if (!ValidateRequiredRoles(&roleError)) {
            QLabel *message = new QLabel(roleError, m_reviewInputForm->parentWidget());
            message->setWordWrap(true);
            m_reviewInputForm->addRow(message);
            return;
        }

        const QString rockTypeField = FieldsForRole("Rock Type").value(0);
        AddReviewCombo(rockTypeField, RockTypeValues());

        for (const QString &product : FieldsForRole("Product")) {
            AddReviewLineEdit(product);
        }

        const QString densityField = FieldsForRole("Density").value(0);
        AddReviewLineEdit(densityField);

        for (const QString &field : FieldsForRole("Attributes")) {
            AddReviewCombo(field, UniqueValuesForField(field));
        }
        for (const QString &field : FieldsForRole("Category")) {
            AddReviewCombo(field, UniqueValuesForField(field));
        }
    }

    void ResetReviewInputs()
    {
        for (QWidget *widget : m_reviewInputWidgets) {
            if (QLineEdit *edit = qobject_cast<QLineEdit *>(widget)) {
                edit->clear();
            } else if (QComboBox *combo = qobject_cast<QComboBox *>(widget)) {
                combo->setCurrentText(QString());
            }
        }
        if (m_reviewTable) {
            m_reviewTable->setRowCount(0);
        }
        ResetReviewResultLabels();
    }

    QString ReviewInputText(const QString &field) const
    {
        QWidget *widget = m_reviewInputWidgets.value(field, nullptr);
        if (QLineEdit *edit = qobject_cast<QLineEdit *>(widget)) {
            return edit->text().trimmed();
        }
        if (QComboBox *combo = qobject_cast<QComboBox *>(widget)) {
            return combo->currentText().trimmed();
        }
        return QString();
    }

    double ReviewInputNumber(const QString &field) const
    {
        QHash<QString, double> context;
        for (auto it = m_reviewInputWidgets.constBegin(); it != m_reviewInputWidgets.constEnd(); ++it) {
            context.insert(it.key(), ReviewInputText(it.key()).toDouble());
        }
        bool ok = false;
        const double value = EvaluateFormulaValue(ReviewInputText(field), context, &ok);
        return ok ? value : 0.0;
    }

    class FormulaParser
    {
    public:
        FormulaParser(const QString &expression, const std::function<bool(const QString &, double &)> &resolver)
            : m_expression(expression), m_resolver(resolver)
        {
        }

        bool parse(double *value)
        {
            m_pos = 0;
            const bool ok = parseExpression(value);
            skipSpaces();
            return ok && m_pos >= m_expression.size();
        }

    private:
        bool parseExpression(double *value)
        {
            if (!parseTerm(value)) {
                return false;
            }
            while (true) {
                skipSpaces();
                if (match('+')) {
                    double rhs = 0.0;
                    if (!parseTerm(&rhs)) {
                        return false;
                    }
                    *value += rhs;
                } else if (match('-')) {
                    double rhs = 0.0;
                    if (!parseTerm(&rhs)) {
                        return false;
                    }
                    *value -= rhs;
                } else {
                    return true;
                }
            }
        }

        bool parseTerm(double *value)
        {
            if (!parseFactor(value)) {
                return false;
            }
            while (true) {
                skipSpaces();
                if (match('*')) {
                    double rhs = 0.0;
                    if (!parseFactor(&rhs)) {
                        return false;
                    }
                    *value *= rhs;
                } else if (match('/')) {
                    double rhs = 0.0;
                    if (!parseFactor(&rhs) || std::abs(rhs) < 1e-12) {
                        return false;
                    }
                    *value /= rhs;
                } else {
                    return true;
                }
            }
        }

        bool parseFactor(double *value)
        {
            skipSpaces();
            if (match('+')) {
                return parseFactor(value);
            }
            if (match('-')) {
                if (!parseFactor(value)) {
                    return false;
                }
                *value = -*value;
                return true;
            }
            if (match('(')) {
                if (!parseExpression(value)) {
                    return false;
                }
                skipSpaces();
                return match(')');
            }
            if (peek() == '[') {
                return parseBracketReference(value);
            }
            if (peek().isLetter() || peek() == '_') {
                return parseIdentifier(value);
            }
            return parseNumber(value);
        }

        bool parseNumber(double *value)
        {
            skipSpaces();
            const int start = m_pos;
            bool hasDigit = false;
            while (m_pos < m_expression.size()) {
                const QChar ch = m_expression[m_pos];
                if (ch.isDigit()) {
                    hasDigit = true;
                    ++m_pos;
                } else if (ch == '.') {
                    ++m_pos;
                } else {
                    break;
                }
            }
            if (m_pos < m_expression.size() && (m_expression[m_pos] == 'e' || m_expression[m_pos] == 'E')) {
                ++m_pos;
                if (m_pos < m_expression.size() && (m_expression[m_pos] == '+' || m_expression[m_pos] == '-')) {
                    ++m_pos;
                }
                while (m_pos < m_expression.size() && m_expression[m_pos].isDigit()) {
                    hasDigit = true;
                    ++m_pos;
                }
            }
            if (!hasDigit) {
                return false;
            }
            bool ok = false;
            *value = m_expression.mid(start, m_pos - start).toDouble(&ok);
            return ok;
        }

        bool parseIdentifier(double *value)
        {
            const int start = m_pos;
            while (m_pos < m_expression.size()) {
                const QChar ch = m_expression[m_pos];
                if (ch.isLetterOrNumber() || ch == '_' || ch == '.') {
                    ++m_pos;
                } else {
                    break;
                }
            }
            const QString name = m_expression.mid(start, m_pos - start).trimmed();
            skipSpaces();
            if (match('(')) {
                if (name.compare("IF", Qt::CaseInsensitive) == 0) {
                    return parseIf(value);
                }
                return false;
            }
            return m_resolver(name, *value);
        }

        bool parseIf(double *value)
        {
            double condition = 0.0;
            if (!parseExpression(&condition)) {
                return false;
            }
            skipSpaces();
            if (!match(',')) {
                return false;
            }
            double trueValue = 0.0;
            if (!parseExpression(&trueValue)) {
                return false;
            }
            skipSpaces();
            if (!match(',')) {
                return false;
            }
            double falseValue = 0.0;
            if (!parseExpression(&falseValue)) {
                return false;
            }
            skipSpaces();
            if (!match(')')) {
                return false;
            }
            *value = std::abs(condition) > 1e-12 ? trueValue : falseValue;
            return true;
        }

        bool parseBracketReference(double *value)
        {
            if (!match('[')) {
                return false;
            }
            const int start = m_pos;
            while (m_pos < m_expression.size() && m_expression[m_pos] != ']') {
                ++m_pos;
            }
            if (m_pos >= m_expression.size()) {
                return false;
            }
            const QString name = m_expression.mid(start, m_pos - start).trimmed();
            ++m_pos;
            return m_resolver(name, *value);
        }

        void skipSpaces()
        {
            while (m_pos < m_expression.size() && m_expression[m_pos].isSpace()) {
                ++m_pos;
            }
        }

        QChar peek() const
        {
            return m_pos < m_expression.size() ? m_expression[m_pos] : QChar();
        }

        bool match(QChar expected)
        {
            skipSpaces();
            if (m_pos < m_expression.size() && m_expression[m_pos] == expected) {
                ++m_pos;
                return true;
            }
            return false;
        }

        QString m_expression;
        std::function<bool(const QString &, double &)> m_resolver;
        int m_pos = 0;
    };

    double EvaluateFormulaValue(
        const QString &text,
        const QHash<QString, double> &context,
        bool *ok = nullptr,
        QSet<QString> stack = {}) const
    {
        QString expression = text.trimmed();
        if (expression.isEmpty()) {
            if (ok) {
                *ok = false;
            }
            return 0.0;
        }
        if (expression.startsWith('=')) {
            expression.remove(0, 1);
        }

        auto resolveByName = [this, &context, &stack](const QString &name, double &value) -> bool {
            const QString key = name.trimmed();
            for (auto it = context.constBegin(); it != context.constEnd(); ++it) {
                if (it.key().compare(key, Qt::CaseInsensitive) == 0) {
                    value = it.value();
                    return true;
                }
            }
            for (const auto &variable : m_variables) {
                if (variable.name.compare(key, Qt::CaseInsensitive) == 0) {
                    if (stack.contains(variable.name)) {
                        return false;
                    }
                    stack.insert(variable.name);
                    bool nestedOk = false;
                    value = EvaluateFormulaValue(variable.formula, context, &nestedOk, stack);
                    stack.remove(variable.name);
                    return nestedOk;
                }
            }
            return false;
        };

        double value = 0.0;
        FormulaParser parser(expression, resolveByName);
        const bool parsed = parser.parse(&value);
        if (ok) {
            *ok = parsed;
        }
        return parsed ? value : 0.0;
    }

    bool IsNumericText(const QString &text, double *value = nullptr) const
    {
        const QString trimmed = text.trimmed();
        static const QRegularExpression numericPattern(R"(^[-+]?\d+(?:\.\d{1,2})?$|^[-+]?\d+$)");
        if (!numericPattern.match(trimmed).hasMatch()) {
            return false;
        }
        bool ok = false;
        const double parsed = trimmed.toDouble(&ok);
        if (ok && value) {
            *value = parsed;
        }
        return ok;
    }

    bool EvaluateVariableByName(
        const QString &name,
        const QHash<QString, double> &context,
        double *value,
        QSet<QString> stack = {}) const
    {
        const QString key = name.trimmed();
        static const QRegularExpression variableNamePattern(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
        if (!variableNamePattern.match(key).hasMatch()) {
            return false;
        }
        for (const auto &variable : m_variables) {
            if (variable.name.compare(key, Qt::CaseInsensitive) == 0) {
                if (stack.contains(variable.name)) {
                    return false;
                }
                stack.insert(variable.name);
                bool ok = false;
                const double parsed = EvaluateFormulaValue(variable.formula, context, &ok, stack);
                stack.remove(variable.name);
                if (ok && value) {
                    *value = parsed;
                }
                return ok;
            }
        }
        return false;
    }

    bool EvaluateDestinationCellValue(
        const QString &text,
        const QHash<QString, double> &context,
        double *value) const
    {
        if (IsNumericText(text, value)) {
            return true;
        }
        return EvaluateVariableByName(text, context, value);
    }

    double DestinationProductValue(
        const DestinationState &destination,
        int productIndex,
        int offset,
        const QHash<QString, double> &context,
        bool *ok = nullptr) const
    {
        const int index = productIndex * 3 + offset;
        if (index < 0 || index >= destination.productValues.size()) {
            if (ok) {
                *ok = false;
            }
            return 0.0;
        }
        double value = 0.0;
        const bool parsed = EvaluateDestinationCellValue(destination.productValues[index], context, &value);
        if (ok) {
            *ok = parsed;
        }
        return parsed ? value : 0.0;
    }

    void ResetReviewResultLabels()
    {
        if (m_reviewBestDestinationLabel) {
            m_reviewBestDestinationLabel->setText("-");
        }
        if (m_reviewRevenueLabel) {
            m_reviewRevenueLabel->setText("$0.00");
        }
        if (m_reviewMiningCostLabel) {
            m_reviewMiningCostLabel->setText("$0.00");
        }
        if (m_reviewProcessingCostLabel) {
            m_reviewProcessingCostLabel->setText("$0.00");
        }
        if (m_reviewBlockValueLabel) {
            m_reviewBlockValueLabel->setText("$0.00");
        }
        if (m_reviewDilutionLabel) {
            m_reviewDilutionLabel->setText("0.00 %");
        }
        if (m_reviewMiningRecoveryLabel) {
            m_reviewMiningRecoveryLabel->setText("100.00 %");
        }
        if (m_reviewRockTypeLabel) {
            m_reviewRockTypeLabel->setText("-");
        }
    }

    void CalculateReview()
    {
        QString roleError;
        if (!ValidateRequiredRoles(&roleError)) {
            QMessageBox::warning(this, "Economic Model", roleError);
            return;
        }
        SaveCurrentRockTypeState();

        const QString rockTypeField = FieldsForRole("Rock Type").value(0);
        const QString rockType = ReviewInputText(rockTypeField);
        const RockTypeState state = m_rockTypeStates.value(rockType, DefaultRockTypeState());
        const QStringList products = FieldsForRole("Product");
        const double miningRecovery = state.miningRecovery / 100.0;
        const double dilution = state.dilution / 100.0;
        const double revenueTonnes = miningRecovery;
        const double costTonnes = miningRecovery / (1.0 + std::max(0.0, dilution));
        QHash<QString, double> formulaContext;
        for (auto it = m_reviewInputWidgets.constBegin(); it != m_reviewInputWidgets.constEnd(); ++it) {
            formulaContext.insert(it.key(), ReviewInputText(it.key()).toDouble());
        }

        m_reviewTable->setRowCount(0);
        QString bestDestination;
        double bestRevenue = 0.0;
        double bestMiningCost = 0.0;
        double bestProcessingCost = 0.0;
        double bestValue = -std::numeric_limits<double>::infinity();

        for (const DestinationState &destination : state.destinations) {
            if (!destination.enabled && destination.name != "Waste") {
                continue;
            }
            double revenue = 0.0;
            double additionalProcessing = 0.0;
            for (int productIndex = 0; productIndex < products.size(); ++productIndex) {
                const double productValue = EvaluateFormulaValue(
                    ReviewInputText(products[productIndex]),
                    formulaContext,
                    nullptr);
                bool priceOk = false;
                bool recoveryOk = false;
                bool addCostOk = false;
                const double price = DestinationProductValue(destination, productIndex, 0, formulaContext, &priceOk);
                const double recovery = DestinationProductValue(destination, productIndex, 1, formulaContext, &recoveryOk) / 100.0;
                const double addCost = DestinationProductValue(destination, productIndex, 2, formulaContext, &addCostOk);
                if (!priceOk || !recoveryOk || !addCostOk) {
                    QMessageBox::warning(
                        this,
                        "Economic Model",
                        QString("Destination values must be numbers with up to two decimals or existing variable names. Check destination '%1' for product '%2'.")
                            .arg(destination.name, products[productIndex]));
                    return;
                }
                const QString productUnit = UnitForProduct(products[productIndex]);
                const bool productIsBlockMass = productUnit.startsWith("Mass", Qt::CaseInsensitive);
                const double containedProduct = productIsBlockMass
                    ? productValue
                    : productValue * revenueTonnes;
                const double recoveredProduct = containedProduct * recovery;
                revenue += recoveredProduct * price;
                additionalProcessing += recoveredProduct * addCost;
            }
            const double miningCost = state.miningCost * costTonnes;
            const double processingCost = destination.processingCost * costTonnes + additionalProcessing;
            const double value = revenue - miningCost - processingCost;

            const int row = m_reviewTable->rowCount();
            m_reviewTable->insertRow(row);
            m_reviewTable->setItem(row, 0, new QTableWidgetItem(destination.name));
            m_reviewTable->setItem(row, 1, new QTableWidgetItem(QString::number(revenue, 'f', 2)));
            m_reviewTable->setItem(row, 2, new QTableWidgetItem(QString::number(miningCost, 'f', 2)));
            m_reviewTable->setItem(row, 3, new QTableWidgetItem(QString::number(processingCost, 'f', 2)));
            m_reviewTable->setItem(row, 4, new QTableWidgetItem(QString::number(value, 'f', 2)));
            if (value > bestValue) {
                bestValue = value;
                bestDestination = destination.name;
                bestRevenue = revenue;
                bestMiningCost = miningCost;
                bestProcessingCost = processingCost;
            }
        }

        if (m_reviewBestDestinationLabel) {
            m_reviewBestDestinationLabel->setText(bestDestination.isEmpty() ? "-" : bestDestination);
        }
        if (m_reviewRevenueLabel) {
            m_reviewRevenueLabel->setText(QString("$%1").arg(bestRevenue, 0, 'f', 2));
        }
        if (m_reviewMiningCostLabel) {
            m_reviewMiningCostLabel->setText(QString("$%1").arg(bestMiningCost, 0, 'f', 2));
        }
        if (m_reviewProcessingCostLabel) {
            m_reviewProcessingCostLabel->setText(QString("$%1").arg(bestProcessingCost, 0, 'f', 2));
        }
        if (m_reviewBlockValueLabel) {
            m_reviewBlockValueLabel->setText(QString("$%1").arg(std::isfinite(bestValue) ? bestValue : 0.0, 0, 'f', 2));
        }
        if (m_reviewDilutionLabel) {
            m_reviewDilutionLabel->setText(QString("%1 %").arg(state.dilution, 0, 'f', 2));
        }
        if (m_reviewMiningRecoveryLabel) {
            m_reviewMiningRecoveryLabel->setText(QString("%1 %").arg(state.miningRecovery, 0, 'f', 2));
        }
        if (m_reviewRockTypeLabel) {
            m_reviewRockTypeLabel->setText(rockType.isEmpty() ? "-" : rockType);
        }
    }

    QString UnitForProduct(const QString &field) const
    {
        if (!m_fieldsTable) {
            return QString();
        }
        for (int row = 0; row < m_fieldsTable->rowCount(); ++row) {
            QTableWidgetItem *fieldItem = m_fieldsTable->item(row, 0);
            QComboBox *unitCombo = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(row, 2));
            if (fieldItem && unitCombo && fieldItem->text() == field) {
                return unitCombo->currentText();
            }
        }
        return QString();
    }

    QString PriceUnitForProduct(const QString &field) const
    {
        const QString unit = UnitForProduct(field);
        if (unit == "Percentage") {
            return "$/t";
        }
        if (unit == "Concentration (g/t)") {
            return "$/g";
        }
        if (unit == "Mass (g)") {
            return "$/g";
        }
        if (unit == "Mass (oz)") {
            return "$/oz";
        }
        if (unit == "Mass (lb)") {
            return "$/lb";
        }
        return "$";
    }

    QStringList RockTypeValues()
    {
        QStringList fields = FieldsForRole("Rock Type");
        if (fields.isEmpty()) {
            return {"Default"};
        }
        QStringList values;
        QSet<QString> seen;
        const QString blockModel = blockModelName();
        const QString internalPath = m_internalPathsByBlockModel.value(blockModel);
        for (const QString &field : fields) {
            const QString cacheKey = blockModel + "/" + field;
            QStringList fieldValues = m_rockTypeValueCache.value(cacheKey);
            if (fieldValues.isEmpty() && !m_rockTypeValueCache.contains(cacheKey)) {
                fieldValues = UniqueBlockModelFieldValues(internalPath, field);
                if (fieldValues.isEmpty()) {
                    fieldValues = {field};
                }
                m_rockTypeValueCache.insert(cacheKey, fieldValues);
            }
            for (const QString &value : fieldValues) {
                if (!seen.contains(value)) {
                    seen.insert(value);
                    values << value;
                }
            }
        }
        values.sort(Qt::CaseInsensitive);
        return values.isEmpty() ? QStringList{"Default"} : values;
    }

    void RebuildOreDestinationControls()
    {
        const QStringList products = FieldsForRole("Product");
        if (m_profitFieldCombo) {
            const QString current = m_profitFieldCombo->currentText();
            m_profitFieldCombo->clear();
            m_profitFieldCombo->addItems(products);
            const int idx = m_profitFieldCombo->findText(current, Qt::MatchFixedString);
            if (idx >= 0) {
                m_profitFieldCombo->setCurrentIndex(idx);
            }
        }

        if (!m_profitModelCheck || !m_profitModelCheck->isChecked()) {
            const QStringList rockTypes = RockTypeValues();
            if (m_rockTypeCombo) {
                const QString current = m_rockTypeCombo->currentText();
                m_rockTypeCombo->blockSignals(true);
                m_rockTypeCombo->clear();
                m_rockTypeCombo->addItems(rockTypes);
                const int idx = m_rockTypeCombo->findText(current, Qt::MatchFixedString);
                if (idx >= 0) {
                    m_rockTypeCombo->setCurrentIndex(idx);
                }
                m_rockTypeCombo->blockSignals(false);
                if (!m_rockTypeCombo->currentText().isEmpty()) {
                    LoadRockTypeState(m_rockTypeCombo->currentText());
                }
            }
            RebuildOreDestinationTable();
        }
    }

    void RebuildOreDestinationTable()
    {
        if (!m_destinationTable) {
            return;
        }
        SaveCurrentRockTypeState();
        const QString currentRockType = m_rockTypeCombo ? m_rockTypeCombo->currentText() : QString("Default");
        const RockTypeState state = m_rockTypeStates.value(currentRockType, DefaultRockTypeState());
        const QStringList products = FieldsForRole("Product");
        QStringList headers = {"Enabled", "Destination Name", "Processing Cost ($/t ore)"};
        for (const QString &product : products) {
            headers << QString("Price\n(%1)").arg(PriceUnitForProduct(product));
            headers << "Recovery\n(%)";
            headers << "Add. Proc.\nCost";
        }
        m_destinationTable->clear();
        m_destinationTable->setColumnCount(headers.size());
        m_destinationTable->setHorizontalHeaderLabels(headers);
        if (ProductGroupHeader *header = dynamic_cast<ProductGroupHeader *>(m_destinationTable->horizontalHeader())) {
            header->setProducts(products);
        }
        m_destinationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_destinationTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_destinationTable->horizontalHeader()->setMinimumSectionSize(72);
        m_destinationTable->setColumnWidth(0, 72);
        m_destinationTable->setColumnWidth(1, 160);
        m_destinationTable->setColumnWidth(2, 155);
        m_destinationTable->setItemDelegateForColumn(2, new DecimalValueDelegate(m_destinationTable));
        for (int col = 3; col < headers.size(); ++col) {
            m_destinationTable->setColumnWidth(col, 115);
            m_destinationTable->setItemDelegateForColumn(col, new EconomicValueDelegate(m_destinationTable));
        }
        m_destinationTable->setRowCount(0);
        for (const DestinationState &destination : state.destinations) {
            AddDestinationRow(destination.name, destination.enabled, destination.processingCost, destination.productValues);
        }
        if (m_destinationTable->rowCount() == 0) {
            AddDestinationRow("Waste", true, 0.0);
        }
    }

    RockTypeState DefaultRockTypeState() const
    {
        RockTypeState state;
        DestinationState waste;
        waste.enabled = true;
        waste.name = "Waste";
        waste.processingCost = 0.0;
        state.destinations.append(waste);
        return state;
    }

    void AddDestinationRow(
        const QString &name,
        bool enabled = true,
        double processingCost = 0.0,
        const QStringList &productValues = {})
    {
        if (!m_destinationTable) {
            return;
        }
        const int row = m_destinationTable->rowCount();
        m_destinationTable->insertRow(row);
        QTableWidgetItem *enabledItem = new QTableWidgetItem();
        if (row == 0) {
            enabledItem->setFlags(enabledItem->flags() & ~(Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsEnabled));
            enabledItem->setText("");
        } else {
            enabledItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        }
        m_destinationTable->setItem(row, 0, enabledItem);

        QTableWidgetItem *nameItem = new QTableWidgetItem(row == 0 ? "Waste" : name);
        if (row == 0) {
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        }
        m_destinationTable->setItem(row, 1, nameItem);
        m_destinationTable->setItem(row, 2, new QTableWidgetItem(QString::number(processingCost, 'f', 2)));
        for (int col = 3; col < m_destinationTable->columnCount(); ++col) {
            const int valueIndex = col - 3;
            QString value = valueIndex < productValues.size() ? productValues[valueIndex] : QString("0");
            double numericValue = 0.0;
            if (IsNumericText(value, &numericValue)) {
                value = QString::number(numericValue, 'f', 2);
            }
            QTableWidgetItem *valueItem = new QTableWidgetItem(value);
            const QStringList variables = FormulaValueChoices();
            if (!variables.isEmpty()) {
                valueItem->setToolTip(QString("Enter a number or variable name. Variables available: %1").arg(variables.join(", ")));
            }
            m_destinationTable->setItem(row, col, valueItem);
        }
    }

    void SaveCurrentRockTypeState()
    {
        if (!m_rockTypeCombo || !m_destinationTable || m_loadingRockType) {
            return;
        }
        const QString rockType = !m_loadedRockType.isEmpty() ? m_loadedRockType : m_rockTypeCombo->currentText();
        if (rockType.isEmpty()) {
            return;
        }
        RockTypeState state;
        state.dilution = m_dilutionSpin ? m_dilutionSpin->value() : 0.0;
        state.miningRecovery = m_miningRecoverySpin ? m_miningRecoverySpin->value() : 100.0;
        state.miningCost = m_miningCostSpin ? m_miningCostSpin->value() : 0.0;
        for (int row = 0; row < m_destinationTable->rowCount(); ++row) {
            DestinationState destination;
            destination.enabled = row == 0 ? true : (m_destinationTable->item(row, 0)
                ? m_destinationTable->item(row, 0)->checkState() == Qt::Checked
                : true);
            destination.name = m_destinationTable->item(row, 1)
                ? m_destinationTable->item(row, 1)->text().trimmed()
                : QString();
            destination.processingCost = m_destinationTable->item(row, 2)
                ? std::round(m_destinationTable->item(row, 2)->text().toDouble() * 100.0) / 100.0
                : 0.0;
            for (int col = 3; col < m_destinationTable->columnCount(); ++col) {
                QString value = m_destinationTable->item(row, col)
                    ? m_destinationTable->item(row, col)->text()
                    : QString("0");
                double numericValue = 0.0;
                if (IsNumericText(value, &numericValue)) {
                    value = QString::number(numericValue, 'f', 2);
                }
                destination.productValues << value;
            }
            if (row == 0) {
                destination.name = "Waste";
            }
            if (!destination.name.isEmpty()) {
                state.destinations.append(destination);
            }
        }
        if (state.destinations.isEmpty()) {
            state = DefaultRockTypeState();
        }
        m_rockTypeStates.insert(rockType, state);
    }

    void LoadRockTypeState(const QString &rockType)
    {
        if (!m_destinationTable) {
            return;
        }
        m_loadingRockType = true;
        const RockTypeState state = m_rockTypeStates.value(rockType, DefaultRockTypeState());
        if (m_dilutionSpin) {
            m_dilutionSpin->setValue(state.dilution);
        }
        if (m_miningRecoverySpin) {
            m_miningRecoverySpin->setValue(state.miningRecovery);
        }
        if (m_miningCostSpin) {
            m_miningCostSpin->setValue(state.miningCost);
        }
        m_destinationTable->setRowCount(0);
        for (const DestinationState &destination : state.destinations) {
            AddDestinationRow(destination.name, destination.enabled, destination.processingCost, destination.productValues);
        }
        if (m_destinationTable->rowCount() == 0) {
            AddDestinationRow("Waste", true, 0.0);
        }
        m_loadingRockType = false;
        m_loadedRockType = rockType;
    }

    void CopyFromRockType()
    {
        if (!m_rockTypeCombo) {
            return;
        }
        SaveCurrentRockTypeState();
        QStringList candidates;
        for (int i = 0; i < m_rockTypeCombo->count(); ++i) {
            const QString item = m_rockTypeCombo->itemText(i);
            if (item != m_rockTypeCombo->currentText()) {
                candidates << item;
            }
        }
        if (candidates.isEmpty()) {
            return;
        }
        bool ok = false;
        const QString source = QInputDialog::getItem(
            this,
            "Copy From Rock Type",
            "Rock Type",
            candidates,
            0,
            false,
            &ok);
        if (!ok || source.isEmpty()) {
            return;
        }
        m_rockTypeStates.insert(m_rockTypeCombo->currentText(), m_rockTypeStates.value(source, DefaultRockTypeState()));
        LoadRockTypeState(m_rockTypeCombo->currentText());
    }

    QTabWidget *m_tabs = nullptr;
    QComboBox *m_blockModelCombo = nullptr;
    QComboBox *m_economicModelCombo = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QTableWidget *m_fieldsTable = nullptr;
    QCheckBox *m_profitModelCheck = nullptr;
    QWidget *m_profitPanel = nullptr;
    QWidget *m_orePanel = nullptr;
    QComboBox *m_profitFieldCombo = nullptr;
    QComboBox *m_rockTypeCombo = nullptr;
    QDoubleSpinBox *m_dilutionSpin = nullptr;
    QDoubleSpinBox *m_miningRecoverySpin = nullptr;
    QDoubleSpinBox *m_miningCostSpin = nullptr;
    QTableWidget *m_destinationTable = nullptr;
    QFormLayout *m_reviewInputForm = nullptr;
    QTableWidget *m_reviewTable = nullptr;
    QLabel *m_reviewBestDestinationLabel = nullptr;
    QLabel *m_reviewRevenueLabel = nullptr;
    QLabel *m_reviewMiningCostLabel = nullptr;
    QLabel *m_reviewProcessingCostLabel = nullptr;
    QLabel *m_reviewBlockValueLabel = nullptr;
    QLabel *m_reviewDilutionLabel = nullptr;
    QLabel *m_reviewMiningRecoveryLabel = nullptr;
    QLabel *m_reviewRockTypeLabel = nullptr;
    QHash<QString, QWidget *> m_reviewInputWidgets;
    QHash<QString, QStringList> m_fieldsByBlockModel;
    QHash<QString, QString> m_internalPathsByBlockModel;
    QHash<QString, QStringList> m_economicModelsByBlockModel;
    QHash<QString, MainWindow::EconomicModelDefinition> m_definitionsByKey;
    QHash<QString, QStringList> m_rockTypeValueCache;
    QHash<QString, RockTypeState> m_rockTypeStates;
    QList<MainWindow::EconomicModelDefinition::Variable> m_variables;
    QString m_loadedRockType;
    bool m_loadingRockType = false;
    bool m_loadingDefinition = false;
    bool m_applyingExclusiveRole = false;
    bool m_editMode = false;
};
