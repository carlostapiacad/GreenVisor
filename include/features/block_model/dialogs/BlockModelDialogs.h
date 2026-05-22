#pragma once

#include "gui/MainWindow.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QWidget;

class CreateBlockModelLegendDialog : public QDialog
{
public:
    CreateBlockModelLegendDialog(
        const QStringList &fields,
        const QString &layerName,
        const MainWindow::BlockModelLegend *existingLegend = nullptr,
        QWidget *parent = nullptr);

    QString name() const;
    QString layerName() const;
    QString fieldName() const;
    QList<MainWindow::BlockModelLegendBin> bins() const;

private:
    void AddBin(const MainWindow::BlockModelLegendBin &bin);

    QString m_layerName;
    QString m_fieldName;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_fieldList = nullptr;
    QLabel *m_selectedFieldLabel = nullptr;
    QTableWidget *m_binsTable = nullptr;
};

class BlockModelPropertiesDialog : public QDialog
{
public:
    BlockModelPropertiesDialog(
        const QString &layerName,
        const visor::datamine::InternalBlockModelInfo &info,
        const MainWindow::BlockModelDisplaySettings &settings,
        const QStringList &legendNames,
        MainWindow::ViewMode viewMode,
        QWidget *parent = nullptr);

    QString layerName() const;
    MainWindow::BlockModelDisplaySettings settings() const;
    QPushButton *legendButton() const;
    QString selectedLegend() const;
    void setLegendNames(const QStringList &legendNames, const QString &current);

private:
    QComboBox *BuildLegendCombo(const QStringList &legendNames, const QString &current);
    static QString SelectedLegend(QComboBox *combo);
    QGroupBox *BuildBlocksGroup();

    MainWindow::BlockModelDisplaySettings m_settings;
    QLineEdit *m_nameEdit = nullptr;
    QCheckBox *m_blocksCheck = nullptr;
    QComboBox *m_blockLegendCombo = nullptr;
    QComboBox *m_renderModeCombo = nullptr;
    QDoubleSpinBox *m_gapSpin = nullptr;
    QPushButton *m_legendButton = nullptr;
};
