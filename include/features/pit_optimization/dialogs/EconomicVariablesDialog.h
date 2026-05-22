#pragma once

#include "gui/MainWindow.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QTableWidget;
class QWidget;

class SetEconomicVariablesDialog : public QDialog
{
public:
    SetEconomicVariablesDialog(
        const QStringList &availableFields,
        const QList<MainWindow::EconomicModelDefinition::Variable> &variables,
        QWidget *parent = nullptr);

    QList<MainWindow::EconomicModelDefinition::Variable> variables() const;

private:
    void AddVariableRow(const QString &name, const QString &formula);
    void InsertReference(const QString &fieldName);
    bool ValidateVariables(QString *error) const;
    bool HasBalancedSyntax(const QString &formula, QString *error, const QString &name) const;

    QTableWidget *m_table = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_fieldList = nullptr;
    QStringList m_availableFields;
};
