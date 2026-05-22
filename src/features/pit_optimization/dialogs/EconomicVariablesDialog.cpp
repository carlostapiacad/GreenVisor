#include "features/pit_optimization/dialogs/EconomicVariablesDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

SetEconomicVariablesDialog::SetEconomicVariablesDialog(
    const QStringList &availableFields,
    const QList<MainWindow::EconomicModelDefinition::Variable> &variables,
    QWidget *parent)
    : QDialog(parent), m_availableFields(availableFields)
{
    setWindowTitle("Set Variables");
    setModal(true);
    resize(900, 560);

    QVBoxLayout *root = new QVBoxLayout(this);
    QLabel *description = new QLabel(
        "Define reusable variables for use in destination formulas. Variables can reference block model fields or other variables.",
        this);
    root->addWidget(description);

    QHBoxLayout *tools = new QHBoxLayout();
    QPushButton *addButton = new QPushButton("Add Variable", this);
    QPushButton *removeButton = new QPushButton("Remove Variable", this);
    QPushButton *checkButton = new QPushButton("Check Syntax", this);
    tools->addWidget(addButton);
    tools->addWidget(removeButton);
    tools->addWidget(checkButton);
    tools->addStretch(1);
    root->addLayout(tools);

    QHBoxLayout *body = new QHBoxLayout();
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({"Variable Name", "Formula", ""});
    m_table->verticalHeader()->setVisible(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    body->addWidget(m_table, 3);

    QVBoxLayout *fieldLayout = new QVBoxLayout();
    fieldLayout->addWidget(new QLabel("Block Model Fields", this));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search fields...");
    fieldLayout->addWidget(m_searchEdit);
    m_fieldList = new QListWidget(this);
    m_fieldList->addItems(m_availableFields);
    fieldLayout->addWidget(m_fieldList, 1);
    QLabel *hint = new QLabel("Double click a field to insert into the selected formula.", this);
    hint->setWordWrap(true);
    fieldLayout->addWidget(hint);
    body->addLayout(fieldLayout, 1);
    root->addLayout(body, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    QPushButton *applyButton = buttons->addButton("Apply", QDialogButtonBox::ApplyRole);
    buttons->addButton("OK", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    root->addWidget(buttons);

    connect(addButton, &QPushButton::clicked, this, [this]() {
        AddVariableRow(QString("VAR_%1").arg(m_table->rowCount() + 1), "=");
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int row = m_table ? m_table->currentRow() : -1;
        if (row >= 0) {
            m_table->removeRow(row);
        }
    });
    connect(checkButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (ValidateVariables(&error)) {
            QMessageBox::information(this, "Set Variables", "Syntax looks valid.");
        } else {
            QMessageBox::warning(this, "Set Variables", error);
        }
    });
    connect(applyButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!ValidateVariables(&error)) {
            QMessageBox::warning(this, "Set Variables", error);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        QString error;
        if (!ValidateVariables(&error)) {
            QMessageBox::warning(this, "Set Variables", error);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row = 0; row < m_fieldList->count(); ++row) {
            QListWidgetItem *item = m_fieldList->item(row);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
        }
    });
    connect(m_fieldList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) {
            InsertReference(item->text());
        }
    });

    for (const auto &variable : variables) {
        AddVariableRow(variable.name, variable.formula);
    }
}

QList<MainWindow::EconomicModelDefinition::Variable> SetEconomicVariablesDialog::variables() const
{
    QList<MainWindow::EconomicModelDefinition::Variable> result;
    if (!m_table) {
        return result;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        MainWindow::EconomicModelDefinition::Variable variable;
        variable.name = m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed() : QString();
        variable.formula = m_table->item(row, 1) ? m_table->item(row, 1)->text().trimmed() : QString();
        if (!variable.name.isEmpty()) {
            result.append(variable);
        }
    }
    return result;
}

void SetEconomicVariablesDialog::AddVariableRow(const QString &name, const QString &formula)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(name));
    m_table->setItem(row, 1, new QTableWidgetItem(formula));
    QPushButton *fxButton = new QPushButton("fx", m_table);
    fxButton->setMaximumWidth(36);
    connect(fxButton, &QPushButton::clicked, this, [this, row]() {
        m_table->setCurrentCell(row, 1);
    });
    m_table->setCellWidget(row, 2, fxButton);
}

void SetEconomicVariablesDialog::InsertReference(const QString &fieldName)
{
    if (!m_table || fieldName.isEmpty()) {
        return;
    }
    int row = m_table->currentRow();
    if (row < 0) {
        if (m_table->rowCount() == 0) {
            AddVariableRow("VAR_1", "=");
        }
        row = std::max(0, m_table->rowCount() - 1);
    }
    QTableWidgetItem *formulaItem = m_table->item(row, 1);
    if (!formulaItem) {
        formulaItem = new QTableWidgetItem("=");
        m_table->setItem(row, 1, formulaItem);
    }
    QString formula = formulaItem->text();
    if (formula.isEmpty()) {
        formula = "=";
    }
    formula += QString("[%1]").arg(fieldName);
    formulaItem->setText(formula);
    m_table->setCurrentCell(row, 1);
}

bool SetEconomicVariablesDialog::ValidateVariables(QString *error) const
{
    QSet<QString> names;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString name = m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed() : QString();
        const QString formula = m_table->item(row, 1) ? m_table->item(row, 1)->text().trimmed() : QString();
        if (name.isEmpty() && formula.isEmpty()) {
            continue;
        }
        if (name.isEmpty()) {
            if (error) {
                *error = QString("Variable name is required on row %1.").arg(row + 1);
            }
            return false;
        }
        if (names.contains(name)) {
            if (error) {
                *error = QString("Duplicate variable name: %1.").arg(name);
            }
            return false;
        }
        names.insert(name);
        if (formula.isEmpty()) {
            if (error) {
                *error = QString("Formula is required for variable %1.").arg(name);
            }
            return false;
        }
        if (!HasBalancedSyntax(formula, error, name)) {
            return false;
        }
    }

    const QRegularExpression referencePattern(R"(\[([^\]]+)\])");
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString name = m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed() : QString();
        const QString formula = m_table->item(row, 1) ? m_table->item(row, 1)->text() : QString();
        QRegularExpressionMatchIterator it = referencePattern.globalMatch(formula);
        while (it.hasNext()) {
            const QString reference = it.next().captured(1).trimmed();
            if (!m_availableFields.contains(reference, Qt::CaseInsensitive) &&
                !names.contains(reference)) {
                if (error) {
                    *error = QString("Unknown reference [%1] in variable %2.").arg(reference, name);
                }
                return false;
            }
        }
    }
    return true;
}

bool SetEconomicVariablesDialog::HasBalancedSyntax(const QString &formula, QString *error, const QString &name) const
{
    int parens = 0;
    int brackets = 0;
    for (const QChar ch : formula) {
        if (ch == '(') {
            ++parens;
        } else if (ch == ')') {
            --parens;
        } else if (ch == '[') {
            ++brackets;
        } else if (ch == ']') {
            --brackets;
        }
        if (parens < 0 || brackets < 0) {
            if (error) {
                *error = QString("Unbalanced formula in variable %1.").arg(name);
            }
            return false;
        }
    }
    if (parens != 0 || brackets != 0) {
        if (error) {
            *error = QString("Unbalanced formula in variable %1.").arg(name);
        }
        return false;
    }
    return true;
}
