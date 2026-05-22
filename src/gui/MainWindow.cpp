#include "gui/MainWindow.h"

#include "gui/AppStyle.h"
#include "gui/GuiUtils.h"
#include "features/block_model/engine/BlockModelFieldUtils.h"
#include "features/block_model/dialogs/BlockModelDialogs.h"
#include "features/general/dialogs/LayerDialogs.h"
#include "features/pit_optimization/dialogs/EconomicModelDialog.h"
#include "features/pit_optimization/dialogs/EconomicVariablesDialog.h"
#include "features/pit_optimization/dialogs/OpenPitOptimizationDialog.h"
#include "features/pit_optimization/dialogs/PitOptimizationReportDialog.h"
#include "features/pit_optimization/engine/EconomicFormula.h"
#include "features/pit_optimization/engine/MineFlowPitOptimizer.h"
#include "features/pit_optimization/widgets/EconomicTableWidgets.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDateEdit>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSaveFile>
#include <QInputDialog>
#include <QFormLayout>
#include <QFrame>
#include <QProgressDialog>
#include <QPointer>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QThread>
#include <QPushButton>
#include <QRubberBand>
#include <QRect>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSize>
#include <QSet>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QSvgRenderer>
#include <QStyle>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <QVTKOpenGLNativeWidget.h>

#include "gui/LayerModel.h"
#include "io/DatamineImporter.h"
#include "io/DxfLoader.h"
#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCubeSource.h>
#include <vtkDoubleArray.h>
#include <vtkExtractPolyDataGeometry.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkGlyph3DMapper.h>
#include <vtkInteractorStyle.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkOBJReader.h>
#include <vtkPlanes.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkProp3D.h>
#include <vtkProp3DCollection.h>
#include <vtkRenderedAreaPicker.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkObjectFactory.h>
#include <vtkUnsignedCharArray.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

namespace
{
struct RoutePersistentEntry
{
    QString filePath;
    QString layerName;
    QString displayName;
    std::vector<DxfPolyline> polylines;
};

struct RoutePersistentSpeed
{
    QString name;
    double minSlope = 0.0;
    double maxSlope = 0.0;
    double speed = 0.0;
};

struct RoutePersistentCalcRow
{
    QString startDisplay;
    QString mainDisplay;
    QString endDisplay;
    QString name;
};

struct RoutePersistentSetup
{
    std::vector<RoutePersistentEntry> starts;
    std::vector<RoutePersistentEntry> mains;
    std::vector<RoutePersistentEntry> ends;
    std::vector<RoutePersistentSpeed> loadedSpeeds;
    std::vector<RoutePersistentSpeed> unloadedSpeeds;
    std::vector<RoutePersistentCalcRow> calcRows;
    double accelLoaded = 0.2;
    double accelUnloaded = 0.3;
    double decelLoaded = 0.5;
    double decelUnloaded = 0.5;
};

RoutePersistentSetup g_routeSetupState;

double LookupSpeedMpsClosest(const std::vector<RoutePersistentSpeed> &bands, double slopePercent)
{
    constexpr double kKmhToMps = 1000.0 / 3600.0;
    const RoutePersistentSpeed *nearest = nullptr;
    double nearestDistance = std::numeric_limits<double>::max();

    for (const auto &band : bands) {
        if (band.speed <= 0.0) {
            continue;
        }
        if (slopePercent >= band.minSlope && slopePercent < band.maxSlope) {
            return band.speed * kKmhToMps;
        }

        double distance = 0.0;
        if (slopePercent < band.minSlope) {
            distance = band.minSlope - slopePercent;
        } else {
            distance = slopePercent - band.maxSlope;
        }
        if (!nearest || distance < nearestDistance) {
            nearest = &band;
            nearestDistance = distance;
        }
    }

    if (nearest) {
        return nearest->speed * kKmhToMps;
    }
    return 0.0;
}

struct RouteCalcInput
{
    QString startDisplay;
    QString mainDisplay;
    QString endDisplay;
    QString name;
};

struct RouteCalcResult
{
    QString name;
    double outMin = 0.0;
    double outKm = 0.0;
    double backMin = 0.0;
    double backKm = 0.0;
};

class RouteSetupDialog : public QDialog
{
public:
    explicit RouteSetupDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Route Setup");
        setModal(true);
        resize(980, 700);

        QVBoxLayout *root = new QVBoxLayout(this);
        m_tabs = new QTabWidget(this);
        root->addWidget(m_tabs, 1);

        BuildRoutesTab();
        BuildSpeedsTab();
        BuildAccelerationTab();
        BuildStopsTab();
        LoadFromState();

        QDialogButtonBox *buttons = new QDialogButtonBox(this);
        buttons->addButton("Accept", QDialogButtonBox::AcceptRole);
        buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            QString error;
            if (!ValidateSlopeTable(m_loadedTable, "Loaded", error) ||
                !ValidateSlopeTable(m_unloadedTable, "Unloaded", error)) {
                QMessageBox::warning(this, "Validation", error);
                return;
            }
            SaveToState();
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }

private:
    struct RouteDxfEntry
    {
        QString filePath;
        QString layerName;
        QString displayName;
        std::vector<DxfPolyline> polylines;
    };

    struct RouteColumn
    {
        QListWidget *list = nullptr;
        std::vector<RouteDxfEntry> entries;
    };

    void BuildRoutesTab()
    {
        QWidget *tab = new QWidget(m_tabs);
        QHBoxLayout *layout = new QHBoxLayout(tab);
        layout->setSpacing(12);
        layout->setContentsMargins(10, 10, 10, 10);

        BuildRouteColumn("Starts", tab, layout, m_starts);
        BuildRouteColumn("Main", tab, layout, m_main);
        BuildRouteColumn("Ends", tab, layout, m_ends);

        m_tabs->addTab(tab, "Routes");
    }

    void BuildRouteColumn(const QString &title, QWidget *parent, QHBoxLayout *host, RouteColumn &column)
    {
        QGroupBox *group = new QGroupBox(title, parent);
        QVBoxLayout *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(8, 8, 8, 8);
        groupLayout->setSpacing(8);

        column.list = new QListWidget(group);
        column.list->setSelectionMode(QAbstractItemView::SingleSelection);
        groupLayout->addWidget(column.list, 1);

        QHBoxLayout *actions = new QHBoxLayout();
        QPushButton *addBtn = new QPushButton("Add", group);
        QPushButton *deleteBtn = new QPushButton("Delete", group);
        actions->addWidget(addBtn);
        actions->addWidget(deleteBtn);
        actions->addStretch(1);
        groupLayout->addLayout(actions);

        connect(addBtn, &QPushButton::clicked, this, [this, &column]() { AddDxfFiles(column); });
        connect(deleteBtn, &QPushButton::clicked, this, [this, &column]() { DeleteSelected(column); });

        host->addWidget(group, 1);
    }

    void BuildSpeedsTab()
    {
        QWidget *tab = new QWidget(m_tabs);
        QVBoxLayout *layout = new QVBoxLayout(tab);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(10);

        QGroupBox *loadedGroup = new QGroupBox("Loaded", tab);
        QVBoxLayout *loadedLayout = new QVBoxLayout(loadedGroup);
        m_loadedTable = CreateSpeedTable(loadedGroup);
        loadedLayout->addWidget(m_loadedTable);
        layout->addWidget(loadedGroup, 1);

        QGroupBox *unloadedGroup = new QGroupBox("Unloaded", tab);
        QVBoxLayout *unloadedLayout = new QVBoxLayout(unloadedGroup);
        m_unloadedTable = CreateSpeedTable(unloadedGroup);
        unloadedLayout->addWidget(m_unloadedTable);
        layout->addWidget(unloadedGroup, 1);

        m_tabs->addTab(tab, "Speeds");
    }

    QTableWidget *CreateSpeedTable(QWidget *parent)
    {
        QTableWidget *table = new QTableWidget(6, 4, parent);
        table->setHorizontalHeaderLabels({"Name", "Min Slope (%)", "Max Slope (%)", "Speed (km/h)"});
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

        table->setItem(0, 0, new QTableWidgetItem("Flat"));
        table->setItem(1, 0, new QTableWidgetItem("Uphill"));
        table->setItem(2, 0, new QTableWidgetItem("Downhill"));
        return table;
    }

    void BuildAccelerationTab()
    {
        QWidget *tab = new QWidget(m_tabs);
        QVBoxLayout *layout = new QVBoxLayout(tab);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(12);

        auto buildSection = [this, tab](const QString &title, QDoubleSpinBox **loadedOut, QDoubleSpinBox **unloadedOut, double loadedDefault, double unloadedDefault) -> QGroupBox * {
            QGroupBox *group = new QGroupBox(title, tab);
            QGridLayout *grid = new QGridLayout(group);
            grid->setContentsMargins(8, 8, 8, 8);
            grid->setHorizontalSpacing(8);
            grid->setVerticalSpacing(6);

            QLabel *loadedLabel = new QLabel("Loaded", group);
            QLabel *unloadedLabel = new QLabel("Unloaded", group);

            QDoubleSpinBox *loadedSpin = new QDoubleSpinBox(group);
            loadedSpin->setDecimals(3);
            loadedSpin->setRange(0.0, 20.0);
            loadedSpin->setValue(loadedDefault);

            QDoubleSpinBox *unloadedSpin = new QDoubleSpinBox(group);
            unloadedSpin->setDecimals(3);
            unloadedSpin->setRange(0.0, 20.0);
            unloadedSpin->setValue(unloadedDefault);

            QLabel *loadedUnits = new QLabel("m/s2", group);
            QLabel *unloadedUnits = new QLabel("m/s2", group);

            grid->addWidget(loadedLabel, 0, 0);
            grid->addWidget(loadedSpin, 0, 1);
            grid->addWidget(loadedUnits, 0, 2);
            grid->addWidget(unloadedLabel, 1, 0);
            grid->addWidget(unloadedSpin, 1, 1);
            grid->addWidget(unloadedUnits, 1, 2);
            *loadedOut = loadedSpin;
            *unloadedOut = unloadedSpin;
            return group;
        };

        layout->addWidget(buildSection("Aceleration", &m_accelLoaded, &m_accelUnloaded, 0.2, 0.3));
        layout->addWidget(buildSection("Desaceleration", &m_decelLoaded, &m_decelUnloaded, 0.5, 0.5));
        layout->addStretch(1);

        m_tabs->addTab(tab, "Aceleration");
    }

    void BuildStopsTab()
    {
        QWidget *tab = new QWidget(m_tabs);
        QVBoxLayout *layout = new QVBoxLayout(tab);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);
        QLabel *placeholder = new QLabel("Stops configuration will be added later.", tab);
        placeholder->setStyleSheet("color: #9a9a9a;");
        layout->addWidget(placeholder);
        layout->addStretch(1);
        m_tabs->addTab(tab, "Stops");
    }

    void AddDxfFiles(RouteColumn &column)
    {
        const QStringList files = QFileDialog::getOpenFileNames(
            this,
            "Select DXF polylines",
            QString(),
            "DXF (*.dxf)");
        if (files.isEmpty()) {
            return;
        }

        QStringList rejected;
        for (const QString &path : files) {
            const auto layered = LoadDxfLayeredPolylines(path.toStdString(), {});
            if (layered.empty()) {
                rejected.append(QFileInfo(path).fileName());
                continue;
            }

            const QString normalized = QDir::toNativeSeparators(path);
            std::vector<std::string> layerNames;
            layerNames.reserve(layered.size());
            for (const auto &pair : layered) {
                layerNames.push_back(pair.first);
            }
            std::sort(layerNames.begin(), layerNames.end());

            for (const auto &layerStd : layerNames) {
                const QString layerName = QString::fromStdString(layerStd);
                const auto layerIt = layered.find(layerStd);
                if (layerIt == layered.end() || layerIt->second.empty()) {
                    continue;
                }

                bool exists = false;
                for (const auto &entry : column.entries) {
                    if (QDir::toNativeSeparators(entry.filePath).compare(normalized, Qt::CaseInsensitive) == 0 &&
                        entry.layerName.compare(layerName, Qt::CaseInsensitive) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (exists) {
                    continue;
                }

                RouteDxfEntry entry;
                entry.filePath = path;
                entry.layerName = layerName;
                entry.displayName = QString("%1 - %2").arg(QFileInfo(path).fileName(), layerName);
                entry.polylines = layerIt->second;
                column.entries.push_back(entry);

                QListWidgetItem *item = new QListWidgetItem(entry.displayName, column.list);
                item->setData(Qt::UserRole, entry.filePath);
                item->setData(Qt::UserRole + 1, entry.layerName);
            }
        }

        if (!rejected.isEmpty()) {
            QMessageBox::warning(
                this,
                "DXF Polyline",
                QString("These files were skipped because they do not contain polylines:\n%1").arg(rejected.join("\n")));
        }
    }

    void DeleteSelected(RouteColumn &column)
    {
        if (!column.list) {
            return;
        }
        QListWidgetItem *item = column.list->currentItem();
        if (!item) {
            return;
        }
        const QString path = item->data(Qt::UserRole).toString();
        const QString layerName = item->data(Qt::UserRole + 1).toString();
        delete item;

        column.entries.erase(
            std::remove_if(
                column.entries.begin(),
                column.entries.end(),
                [&path, &layerName](const RouteDxfEntry &entry) {
                    return QDir::toNativeSeparators(entry.filePath).compare(
                               QDir::toNativeSeparators(path), Qt::CaseInsensitive) == 0 &&
                        entry.layerName.compare(layerName, Qt::CaseInsensitive) == 0;
                }),
            column.entries.end());
    }

    bool ValidateSlopeTable(QTableWidget *table, const QString &tableName, QString &error) const
    {
        if (!table) {
            return true;
        }
        struct Range
        {
            double minSlope;
            double maxSlope;
            int row;
        };
        std::vector<Range> ranges;
        for (int row = 0; row < table->rowCount(); ++row) {
            const QString name = table->item(row, 0) ? table->item(row, 0)->text().trimmed() : QString();
            const QString minText = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : QString();
            const QString maxText = table->item(row, 2) ? table->item(row, 2)->text().trimmed() : QString();
            const QString speedText = table->item(row, 3) ? table->item(row, 3)->text().trimmed() : QString();

            if (name.isEmpty() && minText.isEmpty() && maxText.isEmpty() && speedText.isEmpty()) {
                continue;
            }

            bool okMin = false;
            bool okMax = false;
            bool okSpeed = false;
            const double minSlope = minText.toDouble(&okMin);
            const double maxSlope = maxText.toDouble(&okMax);
            const double speed = speedText.toDouble(&okSpeed);
            if (name.isEmpty() || !okMin || !okMax || !okSpeed) {
                error = QString("%1 row %2 must have Name, Min Slope, Max Slope and Speed.")
                            .arg(tableName)
                            .arg(row + 1);
                return false;
            }
            if (maxSlope <= minSlope) {
                error = QString("%1 row %2 has an invalid slope range. Max Slope must be greater than Min Slope.")
                            .arg(tableName)
                            .arg(row + 1);
                return false;
            }
            if (speed < 0.0) {
                error = QString("%1 row %2 has an invalid Speed.").arg(tableName).arg(row + 1);
                return false;
            }
            ranges.push_back({minSlope, maxSlope, row});
        }

        std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) {
            return a.minSlope < b.minSlope;
        });
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].minSlope < ranges[i - 1].maxSlope) {
                error = QString("%1 has overlapping slope ranges between rows %2 and %3.")
                            .arg(tableName)
                            .arg(ranges[i - 1].row + 1)
                            .arg(ranges[i].row + 1);
                return false;
            }
        }
        return true;
    }

    void WriteSpeedTable(QTableWidget *table, const std::vector<RoutePersistentSpeed> &values)
    {
        if (!table) {
            return;
        }
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 0; col < table->columnCount(); ++col) {
                if (!table->item(row, col)) {
                    table->setItem(row, col, new QTableWidgetItem());
                }
            }
            if (row < static_cast<int>(values.size())) {
                const auto &v = values[static_cast<std::size_t>(row)];
                table->item(row, 0)->setText(v.name);
                table->item(row, 1)->setText(QString::number(v.minSlope));
                table->item(row, 2)->setText(QString::number(v.maxSlope));
                table->item(row, 3)->setText(QString::number(v.speed));
            } else {
                table->item(row, 0)->setText({});
                table->item(row, 1)->setText({});
                table->item(row, 2)->setText({});
                table->item(row, 3)->setText({});
            }
        }
    }

    std::vector<RoutePersistentSpeed> ReadSpeedTable(QTableWidget *table) const
    {
        std::vector<RoutePersistentSpeed> values;
        if (!table) {
            return values;
        }
        for (int row = 0; row < table->rowCount(); ++row) {
            const QString name = table->item(row, 0) ? table->item(row, 0)->text().trimmed() : QString();
            const QString minText = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : QString();
            const QString maxText = table->item(row, 2) ? table->item(row, 2)->text().trimmed() : QString();
            const QString speedText = table->item(row, 3) ? table->item(row, 3)->text().trimmed() : QString();
            if (name.isEmpty() && minText.isEmpty() && maxText.isEmpty() && speedText.isEmpty()) {
                continue;
            }
            bool okMin = false;
            bool okMax = false;
            bool okSpeed = false;
            const double minSlope = minText.toDouble(&okMin);
            const double maxSlope = maxText.toDouble(&okMax);
            const double speed = speedText.toDouble(&okSpeed);
            if (!okMin || !okMax || !okSpeed) {
                continue;
            }
            values.push_back({name, minSlope, maxSlope, speed});
        }
        return values;
    }

    void RebuildRouteList(RouteColumn &column)
    {
        if (!column.list) {
            return;
        }
        column.list->clear();
        for (const auto &entry : column.entries) {
            QListWidgetItem *item = new QListWidgetItem(entry.displayName, column.list);
            item->setData(Qt::UserRole, entry.filePath);
            item->setData(Qt::UserRole + 1, entry.layerName);
        }
    }

    static std::vector<RouteDxfEntry> ConvertFromPersistent(const std::vector<RoutePersistentEntry> &source)
    {
        std::vector<RouteDxfEntry> out;
        out.reserve(source.size());
        for (const auto &entry : source) {
            out.push_back({entry.filePath, entry.layerName, entry.displayName, entry.polylines});
        }
        return out;
    }

    static std::vector<RoutePersistentEntry> ConvertToPersistent(const std::vector<RouteDxfEntry> &source)
    {
        std::vector<RoutePersistentEntry> out;
        out.reserve(source.size());
        for (const auto &entry : source) {
            out.push_back({entry.filePath, entry.layerName, entry.displayName, entry.polylines});
        }
        return out;
    }

    void LoadFromState()
    {
        m_starts.entries = ConvertFromPersistent(g_routeSetupState.starts);
        m_main.entries = ConvertFromPersistent(g_routeSetupState.mains);
        m_ends.entries = ConvertFromPersistent(g_routeSetupState.ends);
        RebuildRouteList(m_starts);
        RebuildRouteList(m_main);
        RebuildRouteList(m_ends);

        std::vector<RoutePersistentSpeed> loaded = g_routeSetupState.loadedSpeeds;
        std::vector<RoutePersistentSpeed> unloaded = g_routeSetupState.unloadedSpeeds;
        if (loaded.empty()) {
            loaded.push_back({"Flat", 0.0, 0.0, 0.0});
            loaded.push_back({"Uphill", 0.0, 0.0, 0.0});
            loaded.push_back({"Downhill", 0.0, 0.0, 0.0});
        }
        if (unloaded.empty()) {
            unloaded.push_back({"Flat", 0.0, 0.0, 0.0});
            unloaded.push_back({"Uphill", 0.0, 0.0, 0.0});
            unloaded.push_back({"Downhill", 0.0, 0.0, 0.0});
        }
        WriteSpeedTable(m_loadedTable, loaded);
        WriteSpeedTable(m_unloadedTable, unloaded);

        if (m_accelLoaded) {
            m_accelLoaded->setValue(g_routeSetupState.accelLoaded);
        }
        if (m_accelUnloaded) {
            m_accelUnloaded->setValue(g_routeSetupState.accelUnloaded);
        }
        if (m_decelLoaded) {
            m_decelLoaded->setValue(g_routeSetupState.decelLoaded);
        }
        if (m_decelUnloaded) {
            m_decelUnloaded->setValue(g_routeSetupState.decelUnloaded);
        }
    }

    void SaveToState()
    {
        g_routeSetupState.starts = ConvertToPersistent(m_starts.entries);
        g_routeSetupState.mains = ConvertToPersistent(m_main.entries);
        g_routeSetupState.ends = ConvertToPersistent(m_ends.entries);
        g_routeSetupState.loadedSpeeds = ReadSpeedTable(m_loadedTable);
        g_routeSetupState.unloadedSpeeds = ReadSpeedTable(m_unloadedTable);
        g_routeSetupState.accelLoaded = m_accelLoaded ? m_accelLoaded->value() : g_routeSetupState.accelLoaded;
        g_routeSetupState.accelUnloaded = m_accelUnloaded ? m_accelUnloaded->value() : g_routeSetupState.accelUnloaded;
        g_routeSetupState.decelLoaded = m_decelLoaded ? m_decelLoaded->value() : g_routeSetupState.decelLoaded;
        g_routeSetupState.decelUnloaded = m_decelUnloaded ? m_decelUnloaded->value() : g_routeSetupState.decelUnloaded;
    }

    QTabWidget *m_tabs = nullptr;
    RouteColumn m_starts;
    RouteColumn m_main;
    RouteColumn m_ends;
    QTableWidget *m_loadedTable = nullptr;
    QTableWidget *m_unloadedTable = nullptr;
    QDoubleSpinBox *m_accelLoaded = nullptr;
    QDoubleSpinBox *m_accelUnloaded = nullptr;
    QDoubleSpinBox *m_decelLoaded = nullptr;
    QDoubleSpinBox *m_decelUnloaded = nullptr;
};

class ShiftRotateStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static ShiftRotateStyle *New();
    vtkTypeMacro(ShiftRotateStyle, vtkInteractorStyleTrackballCamera);

    void SetCameraChangedCallback(std::function<void()> callback)
    {
        this->CameraChangedCallback = std::move(callback);
    }

    ShiftRotateStyle()
    {
        this->SetMotionFactor(0.25);
        this->WorldUp[0] = 0.0;
        this->WorldUp[1] = 0.0;
        this->WorldUp[2] = 1.0;
    }

    void OnLeftButtonDown() override
    {
        if (!this->Interactor) {
            return;
        }
        if (this->Interactor->GetShiftKey()) {
            int *pos = this->Interactor->GetEventPosition();
            this->FindPokedRenderer(pos[0], pos[1]);
            this->StartRotate();
        }
    }

    void OnLeftButtonUp() override
    {
        if (!this->Interactor) {
            return;
        }
        if (this->State == VTKIS_ROTATE) {
            this->EndRotate();
        }
    }

    void OnMouseMove() override
    {
        if (this->State == VTKIS_ROTATE) {
            this->Rotate();
            return;
        }
        const int previousState = this->State;
        vtkInteractorStyleTrackballCamera::OnMouseMove();
        if (previousState != VTKIS_NONE) {
            this->StabilizeCameraAfterChange();
        }
    }

    void Rotate() override
    {
        if (!this->Interactor || !this->CurrentRenderer) {
            return;
        }

        vtkCamera *camera = this->CurrentRenderer->GetActiveCamera();
        if (!camera) {
            return;
        }
        camera->ParallelProjectionOn();

        int *pos = this->Interactor->GetEventPosition();
        int *last = this->Interactor->GetLastEventPosition();

        const double dx = static_cast<double>(pos[0] - last[0]);
        const double dy = static_cast<double>(pos[1] - last[1]);
        if (dx == 0.0 && dy == 0.0) {
            return;
        }

        double camPos[3];
        double focal[3];
        camera->GetPosition(camPos);
        camera->GetFocalPoint(focal);

        double dir[3] = {focal[0] - camPos[0], focal[1] - camPos[1], focal[2] - camPos[2]};
        double radius = vtkMath::Norm(dir);
        if (radius <= 1e-6) {
            return;
        }
        vtkMath::Normalize(dir);

        double yaw = std::atan2(dir[1], dir[0]);
        double pitch = std::asin(dir[2]);
        const double step = vtkMath::RadiansFromDegrees(this->GetMotionFactor());
        yaw -= dx * step;
        pitch += dy * step;

        const double limit = vtkMath::RadiansFromDegrees(89.0);
        if (pitch > limit) {
            pitch = limit;
        } else if (pitch < -limit) {
            pitch = -limit;
        }

        const double cosPitch = std::cos(pitch);
        double newDir[3] = {
            cosPitch * std::cos(yaw),
            cosPitch * std::sin(yaw),
            std::sin(pitch)};

        double newPos[3] = {
            focal[0] - newDir[0] * radius,
            focal[1] - newDir[1] * radius,
            focal[2] - newDir[2] * radius};

        camera->SetPosition(newPos);
        camera->SetViewUp(this->WorldUp);
        camera->OrthogonalizeViewUp();

        this->StabilizeCameraAfterChange();
    }

    void OnMouseWheelForward() override
    {
        if (!this->CurrentRenderer || !this->Interactor) {
            return;
        }
        constexpr double kWheelStep = 1.12;
        ApplyDolly(kWheelStep);
    }

    void OnMouseWheelBackward() override
    {
        if (!this->CurrentRenderer || !this->Interactor) {
            return;
        }
        constexpr double kWheelStep = 1.12;
        ApplyDolly(1.0 / kWheelStep);
    }

private:
    void ApplyDolly(double factor)
    {
        vtkCamera *camera = this->CurrentRenderer ? this->CurrentRenderer->GetActiveCamera() : nullptr;
        if (!camera) {
            return;
        }
        camera->ParallelProjectionOn();

        double bounds[6];
        this->CurrentRenderer->ComputeVisiblePropBounds(bounds);
        const bool hasBounds = (bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5]);
        double radius = 1.0;
        if (hasBounds) {
            const double dx = bounds[1] - bounds[0];
            const double dy = bounds[3] - bounds[2];
            const double dz = bounds[5] - bounds[4];
            radius = 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
            if (radius < 1e-6) {
                radius = 1.0;
            }
        }

        const double currentScale = std::max(camera->GetParallelScale(), 1e-12);
        const double minScale = std::max(radius * 1e-10, 1e-9);
        const double maxScale = std::max(radius * 1e8, minScale * 10.0);
        double newScale = currentScale / factor;
        newScale = std::clamp(newScale, minScale, maxScale);
        camera->SetParallelScale(newScale);

        this->StabilizeCameraAfterChange();
    }

    void NotifyCameraChanged()
    {
        if (this->CameraChangedCallback) {
            this->CameraChangedCallback();
        }
    }

    void StabilizeCameraAfterChange()
    {
        if (!this->Interactor || !this->CurrentRenderer) {
            return;
        }
        if (this->AutoAdjustCameraClippingRange) {
            double bounds[6];
            this->CurrentRenderer->ComputeVisiblePropBounds(bounds);
            const bool hasBounds = (bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5]);
            if (hasBounds) {
                this->CurrentRenderer->ResetCameraClippingRange(bounds);
            } else {
                this->CurrentRenderer->ResetCameraClippingRange();
            }
        }
        if (this->Interactor->GetLightFollowCamera()) {
            this->CurrentRenderer->UpdateLightsGeometryToFollowCamera();
        }
        this->Interactor->Render();
        this->NotifyCameraChanged();
    }

    double WorldUp[3];
    std::function<void()> CameraChangedCallback;
};

vtkStandardNewMacro(ShiftRotateStyle);

class RouteProfileChartWidget : public QWidget
{
public:
    explicit RouteProfileChartWidget(const std::vector<std::pair<double, double>> &segments, QWidget *parent = nullptr)
        : QWidget(parent), m_segments(segments)
    {
        setMinimumSize(720, 360);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(35, 35, 35));

        const QRectF plot = rect().adjusted(58, 18, -22, -44);
        p.setPen(QPen(QColor(70, 70, 70), 1));
        p.drawRect(plot);

        const auto mapY = [&plot](double slopePct) {
            const double clamped = std::clamp(slopePct, -20.0, 20.0);
            const double t = (20.0 - clamped) / 40.0;
            return plot.top() + t * plot.height();
        };

        const double yNeg12 = mapY(-12.0);
        const double yPos12 = mapY(12.0);
        const double yZero = mapY(0.0);

        QPen dashed(QColor(180, 150, 70), 1);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawLine(QPointF(plot.left(), yNeg12), QPointF(plot.right(), yNeg12));
        p.drawLine(QPointF(plot.left(), yPos12), QPointF(plot.right(), yPos12));

        p.setPen(QPen(QColor(125, 125, 125), 1));
        p.drawLine(QPointF(plot.left(), yZero), QPointF(plot.right(), yZero));

        p.setPen(QColor(200, 200, 200));
        p.drawText(QRectF(4, yPos12 - 10, 48, 20), Qt::AlignRight | Qt::AlignVCenter, "12%");
        p.drawText(QRectF(4, yZero - 10, 48, 20), Qt::AlignRight | Qt::AlignVCenter, "0%");
        p.drawText(QRectF(4, yNeg12 - 10, 48, 20), Qt::AlignRight | Qt::AlignVCenter, "-12%");
        p.drawText(QRectF(4, plot.top() - 10, 48, 20), Qt::AlignRight | Qt::AlignVCenter, "20%");
        p.drawText(QRectF(4, plot.bottom() - 10, 48, 20), Qt::AlignRight | Qt::AlignVCenter, "-20%");

        double totalDistance = 0.0;
        for (const auto &segment : m_segments) {
            totalDistance += std::max(segment.first, 0.0);
        }
        if (totalDistance <= 1e-9) {
            totalDistance = 1.0;
        }

        const auto mapX = [&plot, totalDistance](double distance) {
            const double t = std::clamp(distance / totalDistance, 0.0, 1.0);
            return plot.left() + t * plot.width();
        };

        p.setPen(QColor(200, 200, 200));
        p.drawText(QRectF(plot.left(), plot.bottom() + 8, 80, 20), Qt::AlignLeft | Qt::AlignTop, "0 m");
        p.drawText(QRectF(plot.right() - 120, plot.bottom() + 8, 120, 20), Qt::AlignRight | Qt::AlignTop,
            QString::number(totalDistance, 'f', 1) + " m");

        p.drawText(QRectF(plot.center().x() - 80, height() - 24, 160, 20), Qt::AlignCenter, "Distance");
        p.save();
        p.translate(18, plot.center().y());
        p.rotate(-90.0);
        p.drawText(QRectF(-80, -10, 160, 20), Qt::AlignCenter, "Slope (%)");
        p.restore();

        QPen profilePen(QColor(90, 220, 255), 2.0);
        p.setPen(profilePen);

        double cumulative = 0.0;
        bool first = true;
        double prevY = 0.0;
        for (const auto &segment : m_segments) {
            const double x0 = mapX(cumulative);
            const double x1 = mapX(cumulative + std::max(segment.first, 0.0));
            const double y = mapY(segment.second);
            if (!first) {
                p.drawLine(QPointF(x0, prevY), QPointF(x0, y));
            }
            p.drawLine(QPointF(x0, y), QPointF(x1, y));
            cumulative += std::max(segment.first, 0.0);
            prevY = y;
            first = false;
        }
    }

private:
    std::vector<std::pair<double, double>> m_segments;
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("GreenVisor");
    setDockNestingEnabled(true);

    BuildViewport();
    BuildMenuBar();
    BuildRibbon();
    BuildDockWidgets();
    ApplyDockStyle();
}

MainWindow::~MainWindow()
{
    DetachVtkCallbacksForShutdown();
}

bool MainWindow::LoadProject(const QString &path)
{
    return LoadProjectFrom(path);
}

bool MainWindow::NewProject(const QString &path)
{
    ResetProjectState();
    if (!path.isEmpty()) {
        m_projectPath = EnsureProjectExtension(path);
        SaveProjectTo(m_projectPath);
    }
    return true;
}

void MainWindow::DetachVtkCallbacksForShutdown()
{
    if (m_routeTimer) {
        m_routeTimer->stop();
    }
    if (m_vtkWidget) {
        m_vtkWidget->removeEventFilter(this);
    }
    if (m_renderWindow && m_renderWindow->GetInteractor()) {
        if (auto *style = ShiftRotateStyle::SafeDownCast(m_renderWindow->GetInteractor()->GetInteractorStyle())) {
            style->SetCameraChangedCallback({});
        }
    }
}

void MainWindow::BuildViewport()
{
    QWidget *center = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(center);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_vtkWidget = new QVTKOpenGLNativeWidget(center);
    m_vtkWidget->setFocusPolicy(Qt::StrongFocus);
    m_vtkWidget->installEventFilter(this);
    layout->addWidget(m_vtkWidget, 1);

    setCentralWidget(center);

    m_ribbon = new QTabWidget(this);
    m_ribbon->setObjectName("ribbon");
    m_ribbon->setDocumentMode(true);
    m_ribbon->setMovable(false);
    m_ribbon->setTabPosition(QTabWidget::North);
    m_ribbon->setFixedHeight(140);
    m_ribbon->tabBar()->setObjectName("ribbonTabBar");
    m_ribbon->tabBar()->setExpanding(false);

    QDockWidget *ribbonDock = new QDockWidget(this);
    ribbonDock->setObjectName("ribbonDock");
    ribbonDock->setAllowedAreas(Qt::TopDockWidgetArea);
    ribbonDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    ribbonDock->setTitleBarWidget(new QWidget(ribbonDock));
    ribbonDock->setWidget(m_ribbon);
    addDockWidget(Qt::TopDockWidgetArea, ribbonDock);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderWindow->AddRenderer(m_renderer);
    m_vtkWidget->setRenderWindow(m_renderWindow);

    m_renderer->SetGradientBackground(true);
    m_renderer->SetBackground(0.01, 0.01, 0.01);
    m_renderer->SetBackground2(0.14, 0.14, 0.14);
    m_renderer->SetNearClippingPlaneTolerance(0.0001);

    vtkNew<vtkAxesActor> axes;
    axes->SetTotalLength(60.0, 60.0, 60.0);
    axes->SetShaftTypeToCylinder();
    axes->SetCylinderRadius(0.015);

    m_axesWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_axesWidget->SetOrientationMarker(axes);
    m_axesWidget->SetInteractor(m_renderWindow->GetInteractor());
    m_axesWidget->SetViewport(0.02, 0.02, 0.18, 0.18);
    m_axesWidget->SetEnabled(1);
    m_axesWidget->InteractiveOff();

    vtkNew<ShiftRotateStyle> style;
    style->SetCameraChangedCallback([this]() {
        this->UpdateDynamicLineWidths();
    });
    m_renderWindow->GetInteractor()->SetInteractorStyle(style);

    QPixmap cursorPixmap(33, 33);
    cursorPixmap.fill(Qt::transparent);
    QPainter painter(&cursorPixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(230, 230, 230), 1));
    painter.drawLine(16, 0, 16, 32);
    painter.drawLine(0, 16, 32, 16);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(14, 14, 4, 4);
    painter.end();
    m_vtkWidget->setCursor(QCursor(cursorPixmap, 16, 16));

    m_renderer->ResetCamera();
    vtkCamera *camera = m_renderer->GetActiveCamera();
    camera->ParallelProjectionOn();
    camera->Azimuth(45.0);
    camera->Elevation(30.0);
    camera->SetParallelScale(std::max(camera->GetParallelScale() * 0.9, 1e-6));
    m_renderer->ResetCameraClippingRange();
    UpdateDynamicLineWidths();
}

void MainWindow::BuildRibbon()
{
    QWidget *general = new QWidget(m_ribbon);
    general->setObjectName("ribbonPage");
    QGridLayout *generalLayout = new QGridLayout(general);
    generalLayout->setContentsMargins(6, 5, 6, 5);
    generalLayout->setHorizontalSpacing(12);
    generalLayout->setVerticalSpacing(0);

    QGroupBox *importGroup = new QGroupBox("Import", general);
    QVBoxLayout *importLayout = new QVBoxLayout(importGroup);
    importLayout->setContentsMargins(3, 5, 3, 3);
    importLayout->setSpacing(4);

    QToolButton *btnImportDxf = new QToolButton(importGroup);
    btnImportDxf->setText("DXF");
    btnImportDxf->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btnImportDxf->setIconSize(QSize(16, 16));
    QString iconPath = "assets/Icons/dxf-file-format-document-extension-file-format-svgrepo-com.svg";
    if (!QFile::exists(iconPath)) {
        iconPath = "assets/ICON/dxf-file-format-document-extension-file-format-svgrepo-com.svg";
    }
    if (!QFile::exists(iconPath)) {
        const QString base = QCoreApplication::applicationDirPath();
        QString candidate = QDir(base).filePath("../assets/Icons/dxf-file-format-document-extension-file-format-svgrepo-com.svg");
        if (QFile::exists(candidate)) {
            iconPath = candidate;
        } else {
            iconPath = QDir(base).filePath("../assets/ICON/dxf-file-format-document-extension-file-format-svgrepo-com.svg");
        }
    }
    QIcon dxfIcon;
    if (QFile::exists(iconPath)) {
        QSvgRenderer renderer(iconPath);
        if (renderer.isValid()) {
            QPixmap pix(btnImportDxf->iconSize());
            pix.fill(Qt::transparent);
            {
                QPainter p(&pix);
                renderer.render(&p);
            }
            QPixmap tinted = pix;
            {
                QPainter tint(&tinted);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(tinted.rect(), QColor(220, 220, 220));
            }
            dxfIcon = QIcon(tinted);
        }
    }
    if (dxfIcon.isNull()) {
        dxfIcon = QIcon(iconPath);
    }
    btnImportDxf->setIcon(dxfIcon);
    connect(btnImportDxf, &QToolButton::clicked, this, &MainWindow::LoadDxfFile);

    const int gridUnit = std::max(1, btnImportDxf->sizeHint().height());
    importGroup->setMinimumWidth(gridUnit * 5);
    btnImportDxf->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnImportDxf->setMinimumWidth(importGroup->minimumWidth() - (gridUnit / 4));

    importLayout->addWidget(btnImportDxf, 0, Qt::AlignLeft);

    QToolButton *btnImportBlockModel = new QToolButton(importGroup);
    btnImportBlockModel->setText("Block Model");
    btnImportBlockModel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btnImportBlockModel->setIconSize(QSize(16, 16));
    btnImportBlockModel->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
    btnImportBlockModel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnImportBlockModel->setMinimumWidth(importGroup->minimumWidth() - (gridUnit / 4));
    connect(btnImportBlockModel, &QToolButton::clicked, this, &MainWindow::LoadBlockModelFile);
    importLayout->addWidget(btnImportBlockModel, 0, Qt::AlignLeft);

    QToolButton *btnImportObj = new QToolButton(importGroup);
    btnImportObj->setText("OBJ");
    btnImportObj->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btnImportObj->setIconSize(QSize(16, 16));
    btnImportObj->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    btnImportObj->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnImportObj->setMinimumWidth(importGroup->minimumWidth() - (gridUnit / 4));
    connect(btnImportObj, &QToolButton::clicked, this, &MainWindow::LoadObjFile);
    importLayout->addWidget(btnImportObj, 0, Qt::AlignLeft);
    importLayout->addStretch(1);

    generalLayout->addWidget(importGroup, 0, 0, Qt::AlignTop);

    QGroupBox *dateGroup = new QGroupBox("Date Control", general);
    generalLayout->setContentsMargins(gridUnit / 6, gridUnit / 6, gridUnit / 6, gridUnit / 6);
    generalLayout->setHorizontalSpacing(gridUnit / 3);

    QGridLayout *dateLayout = new QGridLayout(dateGroup);
    dateLayout->setContentsMargins(gridUnit / 6, gridUnit / 6, gridUnit / 6, gridUnit / 6);
    dateLayout->setHorizontalSpacing(gridUnit / 6);
    dateLayout->setVerticalSpacing(6);
    dateLayout->setRowMinimumHeight(0, gridUnit / 4);

    QLabel *dateLabel = new QLabel("Layer Date", dateGroup);
    dateLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_layerDateCombo = new QComboBox(dateGroup);
    m_layerDateCombo->setEnabled(false);
    m_layerDateCombo->setMinimumWidth(140);
    connect(m_layerDateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::OnLayerDateChanged);

    QPushButton *addDateButton = new QPushButton("Add Date", dateGroup);
    connect(addDateButton, &QPushButton::clicked, this, &MainWindow::OnAddDateClicked);

    dateLayout->addWidget(dateLabel, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    dateLayout->addWidget(m_layerDateCombo, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
    dateLayout->addWidget(addDateButton, 3, 0, Qt::AlignLeft | Qt::AlignVCenter);
    dateLayout->setRowMinimumHeight(1, dateLabel->sizeHint().height());
    dateLayout->setRowMinimumHeight(2, m_layerDateCombo->sizeHint().height());
    dateLayout->setRowMinimumHeight(3, addDateButton->sizeHint().height());

    generalLayout->addWidget(dateGroup, 0, 1, Qt::AlignTop);

    QGroupBox *viewGroup = new QGroupBox("View", general);
    QVBoxLayout *viewLayout = new QVBoxLayout(viewGroup);
    viewLayout->setContentsMargins(6, 6, 6, 6);
    QHBoxLayout *viewButtons = new QHBoxLayout();
    QToolButton *view3DButton = new QToolButton(viewGroup);
    view3DButton->setText("3D");
    view3DButton->setCheckable(true);
    view3DButton->setChecked(true);
    view3DButton->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    view3DButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QToolButton *view2DButton = new QToolButton(viewGroup);
    view2DButton->setText("2D");
    view2DButton->setCheckable(true);
    view2DButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    view2DButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QButtonGroup *viewModeGroup = new QButtonGroup(viewGroup);
    viewModeGroup->setExclusive(true);
    viewModeGroup->addButton(view3DButton);
    viewModeGroup->addButton(view2DButton);
    connect(view3DButton, &QToolButton::clicked, this, &MainWindow::SetViewMode3D);
    connect(view2DButton, &QToolButton::clicked, this, &MainWindow::SetViewMode2D);
    viewButtons->addWidget(view3DButton);
    viewButtons->addWidget(view2DButton);
    QToolButton *twoPointsButton = new QToolButton(viewGroup);
    twoPointsButton->setText("2 points");
    twoPointsButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    twoPointsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(twoPointsButton, &QToolButton::clicked, this, &MainWindow::OnPickBlockSectionTwoPoints);
    viewButtons->addWidget(twoPointsButton);
    viewLayout->addLayout(viewButtons);
    generalLayout->addWidget(viewGroup, 0, 2, Qt::AlignTop);

    generalLayout->setColumnStretch(3, 1);

    m_ribbon->addTab(general, "General");

    QWidget *routes = new QWidget(m_ribbon);
    routes->setObjectName("ribbonPage");
    QHBoxLayout *routesLayout = new QHBoxLayout(routes);
    routesLayout->setContentsMargins(6, 5, 6, 5);
    routesLayout->setSpacing(12);

    QGroupBox *routesGroup = new QGroupBox("Routes", routes);
    QVBoxLayout *routesGroupLayout = new QVBoxLayout(routesGroup);
    routesGroupLayout->setContentsMargins(8, 8, 8, 8);
    routesGroupLayout->setSpacing(6);

    QToolButton *routeSetupButton = new QToolButton(routesGroup);
    routeSetupButton->setText("Setup");
    routeSetupButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    routeSetupButton->setIconSize(QSize(36, 36));
    routeSetupButton->setMinimumSize(QSize(72, 70));
    routeSetupButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    routeSetupButton->setStyleSheet(
        "QToolButton { border: 1px solid #5a5a5a; border-radius: 4px; padding: 4px; }"
        "QToolButton:hover { border: 1px solid #7a7a7a; background: #3a3a3a; }");

    QString routeIconPath = FindAssetPath("assets/Icons/routes_setup.png");
    if (routeIconPath.isEmpty()) {
        routeIconPath = FindAssetPath("assets/icons/routes_setup.png");
    }
    const QIcon routeIcon = MakeTrimmedIcon(routeIconPath, routeSetupButton->iconSize());
    if (!routeIcon.isNull()) {
        routeSetupButton->setIcon(routeIcon);
    }
    connect(routeSetupButton, &QToolButton::clicked, this, &MainWindow::OnOpenRouteSetup);

    QToolButton *routeCalculateButton = new QToolButton(routesGroup);
    routeCalculateButton->setText("Calculate");
    routeCalculateButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    routeCalculateButton->setIconSize(QSize(28, 28));
    routeCalculateButton->setMinimumSize(QSize(90, 70));
    routeCalculateButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    routeCalculateButton->setStyleSheet(
        "QToolButton { border: 1px solid #5a5a5a; border-radius: 4px; padding: 4px; }"
        "QToolButton:hover { border: 1px solid #7a7a7a; background: #3a3a3a; }");
    connect(routeCalculateButton, &QToolButton::clicked, this, &MainWindow::OnCalculateRoutes);

    QToolButton *routeVisualizeButton = new QToolButton(routesGroup);
    routeVisualizeButton->setText("Visualize");
    routeVisualizeButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    routeVisualizeButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    routeVisualizeButton->setIconSize(QSize(24, 24));
    routeVisualizeButton->setMinimumSize(QSize(90, 70));
    routeVisualizeButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    routeVisualizeButton->setStyleSheet(
        "QToolButton { border: 1px solid #5a5a5a; border-radius: 4px; padding: 4px; }"
        "QToolButton:hover { border: 1px solid #7a7a7a; background: #3a3a3a; }");
    connect(routeVisualizeButton, &QToolButton::clicked, this, &MainWindow::OnVisualizeRoutes);

    QHBoxLayout *routeButtonsLayout = new QHBoxLayout();
    routeButtonsLayout->setContentsMargins(0, 0, 0, 0);
    routeButtonsLayout->setSpacing(8);
    routeButtonsLayout->addWidget(routeSetupButton);
    routeButtonsLayout->addWidget(routeCalculateButton);
    routeButtonsLayout->addWidget(routeVisualizeButton);
    routeButtonsLayout->addStretch(1);

    routesGroupLayout->addLayout(routeButtonsLayout);
    routesGroupLayout->addStretch(1);

    routesLayout->addWidget(routesGroup, 0, Qt::AlignTop);
    routesLayout->addStretch(1);

    m_ribbon->addTab(routes, "Routes");

    QWidget *opOptimization = new QWidget(m_ribbon);
    opOptimization->setObjectName("ribbonPage");
    QHBoxLayout *opLayout = new QHBoxLayout(opOptimization);
    opLayout->setContentsMargins(6, 5, 6, 5);
    opLayout->setSpacing(12);

    QGroupBox *economicGroup = new QGroupBox("Economic Model", opOptimization);
    QHBoxLayout *economicLayout = new QHBoxLayout(economicGroup);
    economicLayout->setContentsMargins(8, 8, 8, 8);
    economicLayout->setSpacing(8);

    auto makeEconomicButton = [this, economicGroup](const QString &text, QStyle::StandardPixmap icon) {
        QToolButton *button = new QToolButton(economicGroup);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(style()->standardIcon(icon));
        button->setIconSize(QSize(24, 24));
        button->setMinimumSize(QSize(82, 70));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    };

    QToolButton *createEconomicButton = makeEconomicButton("Create", QStyle::SP_FileIcon);
    QToolButton *modifyEconomicButton = makeEconomicButton("Modify", QStyle::SP_FileDialogDetailedView);
    connect(createEconomicButton, &QToolButton::clicked, this, &MainWindow::OnCreateEconomicModel);
    connect(modifyEconomicButton, &QToolButton::clicked, this, &MainWindow::OnModifyEconomicModel);

    economicLayout->addWidget(createEconomicButton);
    economicLayout->addWidget(modifyEconomicButton);
    economicLayout->addStretch(1);

    opLayout->addWidget(economicGroup, 0, Qt::AlignTop);

    QGroupBox *pitGroup = new QGroupBox("Pit Optimization", opOptimization);
    QHBoxLayout *pitLayout = new QHBoxLayout(pitGroup);
    pitLayout->setContentsMargins(8, 8, 8, 8);
    pitLayout->setSpacing(8);

    QToolButton *optimizationSettingsButton = new QToolButton(pitGroup);
    optimizationSettingsButton->setText("Optimization Settings");
    optimizationSettingsButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    optimizationSettingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogInfoView));
    optimizationSettingsButton->setIconSize(QSize(24, 24));
    optimizationSettingsButton->setMinimumSize(QSize(132, 70));
    optimizationSettingsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(optimizationSettingsButton, &QToolButton::clicked, this, &MainWindow::OnOptimizationSettings);
    pitLayout->addWidget(optimizationSettingsButton);
    pitLayout->addStretch(1);

    opLayout->addWidget(pitGroup, 0, Qt::AlignTop);
    opLayout->addStretch(1);

    m_ribbon->addTab(opOptimization, "OP Optimization");
}

void MainWindow::BuildMenuBar()
{
    QMenuBar *bar = menuBar();
    bar->clear();

    QMenu *fileMenu = bar->addMenu("File");
    QAction *actNew = fileMenu->addAction("New");
    QAction *actOpen = fileMenu->addAction("Open...");
    QAction *actSave = fileMenu->addAction("Save");
    QAction *actSaveAs = fileMenu->addAction("Save As...");

    connect(actNew, &QAction::triggered, this, &MainWindow::OnFileNew);
    connect(actOpen, &QAction::triggered, this, &MainWindow::OnFileOpen);
    connect(actSave, &QAction::triggered, this, &MainWindow::OnFileSave);
    connect(actSaveAs, &QAction::triggered, this, &MainWindow::OnFileSaveAs);
}

void MainWindow::BuildDockWidgets()
{
    QDockWidget *layersDock = new QDockWidget("Layer Manager", this);
    layersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_layersView = new QTreeView(layersDock);
    m_layerModel = new LayerModel(m_layersView);
    m_layersView->setModel(m_layerModel);
    m_layersView->setIndentation(16);
    m_layersView->setAlternatingRowColors(false);
    m_layersView->setHeaderHidden(true);
    m_layersView->setRootIsDecorated(true);
    m_layersView->setUniformRowHeights(true);
    m_layersView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_layersView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_layersView->setColumnHidden(1, true);
    m_layersView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_layersView->selectionModel(), &QItemSelectionModel::currentChanged,
        this, &MainWindow::OnLayerSelectionChanged, Qt::UniqueConnection);
    connect(m_layersView, &QTreeView::doubleClicked, this, &MainWindow::OnLayerDoubleClicked, Qt::UniqueConnection);
    connect(m_layersView, &QTreeView::customContextMenuRequested, this, &MainWindow::OnLayerContextMenu, Qt::UniqueConnection);
    connect(m_layerModel, &LayerModel::visibilityChanged, this, &MainWindow::OnLayerVisibilityChanged, Qt::UniqueConnection);

    layersDock->setWidget(m_layersView);
    addDockWidget(Qt::LeftDockWidgetArea, layersDock);

    m_blockPropertiesOverlay = new QWidget(m_vtkWidget);
    m_blockPropertiesOverlay->setObjectName("blockPropertiesOverlay");
    m_blockPropertiesOverlay->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *propertiesLayout = new QVBoxLayout(m_blockPropertiesOverlay);
    propertiesLayout->setContentsMargins(8, 8, 8, 8);
    propertiesLayout->setSpacing(6);

    m_blockPropertiesTitle = new QLabel(m_blockPropertiesOverlay);
    m_blockPropertiesTitle->setObjectName("blockPropertiesTitle");

    m_blockPropertiesTable = new QTableWidget(m_blockPropertiesOverlay);
    m_blockPropertiesTable->setColumnCount(2);
    m_blockPropertiesTable->setHorizontalHeaderLabels({"Field", "Value"});
    m_blockPropertiesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_blockPropertiesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_blockPropertiesTable->verticalHeader()->setVisible(false);
    m_blockPropertiesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_blockPropertiesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_blockPropertiesTable->setSelectionMode(QAbstractItemView::SingleSelection);

    propertiesLayout->addWidget(m_blockPropertiesTitle);
    propertiesLayout->addWidget(m_blockPropertiesTable, 1);
    m_blockPropertiesOverlay->hide();

    QDockWidget *consoleDock = new QDockWidget("Command Console", this);
    consoleDock->setAllowedAreas(Qt::BottomDockWidgetArea);

    QWidget *consoleWidget = new QWidget(consoleDock);
    QVBoxLayout *consoleLayout = new QVBoxLayout(consoleWidget);
    consoleLayout->setContentsMargins(8, 8, 8, 8);
    consoleLayout->setSpacing(6);

    m_consoleLog = new QTextEdit(consoleWidget);
    m_consoleLog->setReadOnly(true);
    m_consoleLog->setPlaceholderText(">> System log");

    m_consoleInput = new QLineEdit(consoleWidget);
    m_consoleInput->setPlaceholderText("Enter command and press Enter");

    consoleLayout->addWidget(m_consoleLog, 1);
    consoleLayout->addWidget(m_consoleInput);

    consoleDock->setWidget(consoleWidget);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
    consoleDock->setMaximumHeight(150);

    connect(m_consoleInput, &QLineEdit::returnPressed, this, [this]() {
        const QString cmd = m_consoleInput->text().trimmed();
        if (cmd.isEmpty()) {
            return;
        }
        m_consoleLog->append(QString("> %1").arg(cmd));
        m_consoleInput->clear();
    });
}

void MainWindow::LoadDxfFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open DXF",
        QString(),
        "DXF (*.dxf)");
    if (path.isEmpty()) {
        return;
    }

    const std::unordered_set<std::string> layers = ListDxfLayers(path.toStdString());
    QStringList layerList;
    layerList.reserve(static_cast<int>(layers.size()));
    for (const auto &layer : layers) {
        layerList << QString::fromStdString(layer);
    }
    layerList.sort(Qt::CaseInsensitive);

    QStringList existingLayers;
    if (m_layerModel) {
        const QList<LayerNode *> nodes = m_layerModel->geometryLayers();
        for (LayerNode *node : nodes) {
            if (node) {
                existingLayers << node->name();
            }
        }
    }
    existingLayers.sort(Qt::CaseInsensitive);

    DxfLayerDialog dialog(layerList, existingLayers, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QList<DxfLayerDialog::Selection> selections = dialog.selections();
    if (selections.isEmpty()) {
        QMessageBox::warning(this, "DXF", "Select at least one layer.");
        return;
    }

    std::unordered_set<std::string> includeLayers;
    includeLayers.reserve(static_cast<std::size_t>(selections.size()));
    for (const auto &sel : selections) {
        includeLayers.insert(sel.sourceLayer.toStdString());
    }

    const auto layered = LoadDxfLayeredPolyData(path.toStdString(), includeLayers);
    if (layered.empty()) {
        QMessageBox::warning(this, "DXF", "No compatible entities found.");
        return;
    }

    for (const auto &sel : selections) {
        const std::string sourceName = sel.sourceLayer.toStdString();
        auto it = layered.find(sourceName);
        if (it == layered.end()) {
            continue;
        }
        vtkSmartPointer<vtkPolyData> poly = it->second;
        if (!poly || poly->GetNumberOfPoints() == 0) {
            continue;
        }

        LayerRenderData &render = m_layerRenders[sel.targetLayer];
        if (!render.actor) {
            vtkNew<vtkPolyDataMapper> mapper;
            vtkNew<vtkActor> actor;
            actor->SetMapper(mapper);
            actor->GetProperty()->SetColor(0.85, 0.9, 0.95);
            actor->GetProperty()->SetLineWidth(1.2);
            actor->GetProperty()->SetRepresentationToWireframe();
            m_renderer->AddActor(actor);

            render.mapper = mapper;
            render.actor = actor;

            SceneItem item;
            item.actor = actor;
            item.data = poly;
            item.highlightActor = nullptr;
            item.layerName = sel.targetLayer;
            m_sceneItems.push_back(item);
            m_actorIndex[actor] = m_sceneItems.size() - 1;
        }

        const qint64 key = LayerModel::DateKey(sel.date);
        render.versions.insert(key, poly);

        LayerNode *layerNode = nullptr;
        if (m_layerModel) {
            if (sel.createNew) {
                layerNode = m_layerModel->addGeometryLayer(sel.targetLayer);
            } else {
                layerNode = m_layerModel->findGeometryLayerByName(sel.targetLayer);
                if (!layerNode) {
                    layerNode = m_layerModel->addGeometryLayer(sel.targetLayer);
                }
            }
            m_layerModel->addVersion(layerNode, key);
            if (layerNode) {
                OnLayerVisibilityChanged(layerNode, layerNode->isVisible());
                UpdateLayerRender(sel.targetLayer, layerNode->activeDate());
            }
        } else {
            UpdateLayerRender(sel.targetLayer, key);
        }
    }

    if (m_layersView) {
        m_layersView->expandAll();
    }

    if (!m_hasScene) {
        m_renderer->ResetCamera();
        if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
            camera->ParallelProjectionOn();
        }
        m_hasScene = true;
    } else {
        m_renderer->ResetCameraClippingRange();
    }
    m_renderWindow->Render();

    if (m_consoleLog) {
        m_consoleLog->append(QString(">> DXF loaded: %1").arg(path));
    }
}

void MainWindow::LoadBlockModelFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open Block Model",
        QString(),
        "Datamine Block Model (*.dm)");
    if (path.isEmpty()) {
        return;
    }

    const QString defaultName = QFileInfo(path).completeBaseName().isEmpty()
        ? QStringLiteral("Block Model")
        : QFileInfo(path).completeBaseName();

    bool ok = false;
    QString layerName = QInputDialog::getText(
        this,
        "Block Model Layer",
        "Layer name",
        QLineEdit::Normal,
        UniqueChildName(m_layerModel ? m_layerModel->root() : nullptr, defaultName),
        &ok).trimmed();
    if (!ok || layerName.isEmpty()) {
        return;
    }
    if (m_layerModel) {
        layerName = UniqueChildName(m_layerModel->root(), layerName);
    }

    const QString cacheDir = BlockModelCacheDir(m_projectPath);
    if (!QDir().mkpath(cacheDir)) {
        QMessageBox::warning(this, "Block Model", "Unable to create block model cache directory.");
        return;
    }
    QString internalPath = QDir(cacheDir).filePath(QString("%1.gvbm").arg(SanitizeName(layerName)));
    int suffix = 1;
    while (QFileInfo::exists(internalPath)) {
        internalPath = QDir(cacheDir).filePath(QString("%1_%2.gvbm").arg(SanitizeName(layerName)).arg(suffix++));
    }

    QProgressDialog progress("Importing block model...", "Cancel", 0, 100, this);
    progress.setWindowTitle("Block Model Import");
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto result = std::make_shared<std::optional<visor::datamine::InternalBlockModelInfo>>();
    auto error = std::make_shared<QString>();
    QPointer<QProgressDialog> progressPtr(&progress);

    QObject::connect(&progress, &QProgressDialog::canceled, this, [cancelled]() {
        cancelled->store(true);
    });

    QThread *worker = QThread::create([path, internalPath, cancelled, result, error, progressPtr]() {
        try {
            *result = visor::datamine::DmBlockModelImporter::importToInternalFile(
                path.toStdString(),
                internalPath.toStdString(),
                [cancelled, progressPtr](std::int64_t row, std::int64_t total) {
                    if (cancelled->load()) {
                        return false;
                    }
                    if (progressPtr && total > 0) {
                        const int value = static_cast<int>(
                            std::clamp((row * 100) / total, static_cast<std::int64_t>(0), static_cast<std::int64_t>(100)));
                        QMetaObject::invokeMethod(progressPtr, [progressPtr, value]() {
                            if (progressPtr) {
                                progressPtr->setValue(value);
                            }
                        }, Qt::QueuedConnection);
                    }
                    return true;
                });
        } catch (const std::exception &ex) {
            *error = QString::fromLocal8Bit(ex.what());
        }
    });

    QObject::connect(worker, &QThread::finished, &progress, &QProgressDialog::close);
    worker->start();
    progress.exec();
    if (worker->isRunning()) {
        cancelled->store(true);
        worker->wait();
    } else {
        worker->wait();
    }
    worker->deleteLater();

    if (!error->isEmpty()) {
        QFile::remove(internalPath);
        if (error->contains("cancelled", Qt::CaseInsensitive)) {
            return;
        }
        QMessageBox::warning(
            this,
            "Block Model",
            QString("Unable to import block model:\n%1").arg(*error));
        return;
    }
    if (!result->has_value()) {
        QFile::remove(internalPath);
        return;
    }

    LayerNode *node = m_layerModel ? m_layerModel->addBlockModelLayer(layerName, m_layerModel->root()) : nullptr;
    if (node) {
        node->setVisible(true);
        EnsureBlockModelSystemFolders(node);
    }

    BlockModelLayerData data;
    data.info = std::move(**result);
    data.internalPath = internalPath;
    data.description = QString::fromStdString(data.info.sourceHeader.description);
    m_blockModelLayers.insert(layerName, std::move(data));
    m_blockModelSettings.insert(layerName, BlockModelDisplaySettings{});
    ApplyBlockModelRender(layerName);

    if (m_layersView && node) {
        m_layersView->expandAll();
        const QModelIndex idx = m_layerModel->indexFromNode(node, 0);
        if (idx.isValid()) {
            m_layersView->setCurrentIndex(idx);
            m_layersView->scrollTo(idx);
        }
    }

    if (m_layerDateCombo) {
        m_layerDateCombo->clear();
        m_layerDateCombo->setEnabled(false);
    }

    if (m_consoleLog) {
        const auto &imported = m_blockModelLayers[layerName].info;
        m_consoleLog->append(QString(">> Block model imported: %1 (%2 cells)")
                                 .arg(path)
                                 .arg(static_cast<qlonglong>(imported.cellCount)));
    }
}

void MainWindow::LoadObjFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open OBJ",
        QString(),
        "OBJ (*.obj)");
    if (path.isEmpty()) {
        return;
    }

    vtkNew<vtkOBJReader> reader;
    reader->SetFileName(path.toStdString().c_str());
    reader->Update();

    vtkPolyData *output = reader->GetOutput();
    if (!output || output->GetNumberOfPoints() == 0) {
        QMessageBox::warning(this, "OBJ", "No geometry found in this OBJ file.");
        return;
    }

    vtkSmartPointer<vtkPolyData> poly = vtkSmartPointer<vtkPolyData>::New();
    poly->ShallowCopy(output);

    QString baseName = QFileInfo(path).completeBaseName();
    if (baseName.isEmpty()) {
        baseName = "OBJ Mesh";
    }

    QString layerName = baseName;
    LayerNode *layerNode = nullptr;
    if (m_layerModel) {
        layerName = UniqueChildName(m_layerModel->root(), baseName);
        layerNode = m_layerModel->addGeometryLayer(layerName, m_layerModel->root());
    }

    LayerRenderData &render = m_layerRenders[layerName];
    if (!render.actor) {
        vtkNew<vtkPolyDataMapper> mapper;
        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(0.82, 0.86, 0.92);
        actor->GetProperty()->SetRepresentationToSurface();
        m_renderer->AddActor(actor);

        render.mapper = mapper;
        render.actor = actor;

        SceneItem item;
        item.actor = actor;
        item.data = poly;
        item.highlightActor = nullptr;
        item.layerName = layerName;
        m_sceneItems.push_back(item);
        m_actorIndex[actor] = m_sceneItems.size() - 1;
    }

    const qint64 key = LayerModel::DateKey(QDate::currentDate());
    render.versions.insert(key, poly);

    if (layerNode) {
        m_layerModel->addVersion(layerNode, key);
        OnLayerVisibilityChanged(layerNode, layerNode->isVisible());
        UpdateLayerRender(layerName, layerNode->activeDate());
        const QModelIndex idx = m_layerModel->indexFromNode(layerNode, 0);
        if (idx.isValid() && m_layersView) {
            m_layersView->setCurrentIndex(idx);
            m_layersView->scrollTo(idx);
        }
    } else {
        UpdateLayerRender(layerName, key);
    }

    if (m_layersView) {
        m_layersView->expandAll();
    }

    if (!m_hasScene) {
        m_renderer->ResetCamera();
        if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
            camera->ParallelProjectionOn();
        }
        m_hasScene = true;
    } else {
        m_renderer->ResetCameraClippingRange();
    }
    m_renderWindow->Render();

    if (m_consoleLog) {
        m_consoleLog->append(QString(">> OBJ loaded: %1").arg(path));
    }
}

void MainWindow::OnFileNew()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        "Create Project",
        QString(),
        "GreenVisor Project (*.gvs)");
    if (path.isEmpty()) {
        return;
    }
    NewProject(path);
}

void MainWindow::OnFileOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open Project",
        QString(),
        "GreenVisor Project (*.gvs)");
    if (path.isEmpty()) {
        return;
    }
    LoadProjectFrom(path);
}

void MainWindow::OnFileSave()
{
    if (m_projectPath.isEmpty()) {
        OnFileSaveAs();
        return;
    }
    SaveProjectTo(m_projectPath);
}

void MainWindow::OnFileSaveAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        "Save Project As",
        m_projectPath.isEmpty() ? QString() : m_projectPath,
        "GreenVisor Project (*.gvs)");
    if (path.isEmpty()) {
        return;
    }
    SaveProjectTo(path);
}

bool MainWindow::SaveProjectTo(const QString &path)
{
    if (!m_layerModel) {
        return false;
    }

    const QString projectPath = EnsureProjectExtension(path);
    QFileInfo info(projectPath);
    const QString dataDirPath = ProjectDataDir(projectPath);
    const QString dataDirName = QFileInfo(dataDirPath).fileName();
    QDir().mkpath(dataDirPath);

    QSet<QString> usedFiles;
    const QDir projectDir(info.absoluteDir());

    std::function<QJsonObject(LayerNode *)> serializeNode = [&](LayerNode *node) -> QJsonObject {
        QJsonObject obj;
        if (!node) {
            return obj;
        }
        obj["name"] = node->name();
        if (node->isGeometry()) {
            obj["type"] = "geometry";
        } else if (node->isBlockModel()) {
            obj["type"] = "blockModel";
        } else if (node->isEconomicModel()) {
            obj["type"] = "economicModel";
        } else if (node->isLegend()) {
            obj["type"] = "legend";
        } else {
            obj["type"] = "folder";
            obj["system"] = node->isSystemNode();
        }

        if (node->isGeometry()) {
            obj["activeDate"] = static_cast<qint64>(node->activeDate());
            QJsonArray versionsArray;

            const QString layerKey = node->name();
            const LayerRenderData *render = nullptr;
            auto renderIt = m_layerRenders.constFind(layerKey);
            if (renderIt != m_layerRenders.constEnd()) {
                render = &renderIt.value();
            }
            const QMap<qint64, LayerVersion> &history = node->history();

            for (auto it = history.constBegin(); it != history.constEnd(); ++it) {
                const qint64 key = it.key();
                vtkSmartPointer<vtkPolyData> poly;
                if (render) {
                    auto polyIt = render->versions.find(key);
                    if (polyIt != render->versions.end()) {
                        poly = polyIt.value();
                    }
                }
                if (!poly) {
                    poly = vtkSmartPointer<vtkPolyData>::New();
                }

                QString fileName = QString("%1_%2.vtp").arg(SanitizeName(layerKey)).arg(key);
                if (usedFiles.contains(fileName)) {
                    int suffix = 1;
                    QString candidate;
                    do {
                        candidate = QString("%1_%2_%3.vtp").arg(SanitizeName(layerKey)).arg(key).arg(suffix++);
                    } while (usedFiles.contains(candidate));
                    fileName = candidate;
                }
                usedFiles.insert(fileName);

                const QString relPath = QDir(dataDirName).filePath(fileName);
                const QString fullPath = projectDir.filePath(relPath);

                vtkNew<vtkXMLPolyDataWriter> writer;
                writer->SetFileName(fullPath.toStdString().c_str());
                writer->SetInputData(poly);
                writer->Write();

                QJsonObject verObj;
                verObj["key"] = static_cast<qint64>(key);
                verObj["date"] = LayerModel::DateFromKey(key).toString("yyyy-MM-dd");
                verObj["data"] = relPath;
                versionsArray.append(verObj);
            }
            obj["versions"] = versionsArray;
        } else if (node->isBlockModel()) {
            auto blockIt = m_blockModelLayers.constFind(node->name());
            if (blockIt != m_blockModelLayers.constEnd()) {
                QString fileName = QString("%1.gvbm").arg(SanitizeName(node->name()));
                if (usedFiles.contains(fileName)) {
                    int suffix = 1;
                    QString candidate;
                    do {
                        candidate = QString("%1_%2.gvbm").arg(SanitizeName(node->name())).arg(suffix++);
                    } while (usedFiles.contains(candidate));
                    fileName = candidate;
                }
                usedFiles.insert(fileName);

                const QString relPath = QDir(dataDirName).filePath(fileName);
                const QString fullPath = projectDir.filePath(relPath);
                QString sourcePath = blockIt.value().internalPath;
                if (!QFileInfo::exists(sourcePath) && !blockIt.value().storedRelativePath.isEmpty()) {
                    sourcePath = projectDir.filePath(blockIt.value().storedRelativePath);
                }
                if (!QFileInfo::exists(sourcePath)) {
                    QMessageBox::warning(this, "Save Project", QString("Unable to find block model source file: %1").arg(node->name()));
                } else if (QFileInfo(sourcePath).absoluteFilePath() != QFileInfo(fullPath).absoluteFilePath()) {
                    QFile::remove(fullPath);
                    if (!QFile::copy(sourcePath, fullPath)) {
                        QMessageBox::warning(this, "Save Project", QString("Unable to save block model file: %1").arg(node->name()));
                    }
                }

                obj["data"] = relPath;
                obj["description"] = blockIt.value().description;
                {
                    const auto &model = blockIt.value().info;
                    obj["cellCount"] = static_cast<qint64>(model.cellCount);
                    obj["hasSubcells"] = model.hasSubcells;
                    obj["sortedByIJK"] = model.isSortedByIJK;
                    QJsonObject prototypeObj;
                    prototypeObj["xOrigin"] = model.prototype.xOrigin;
                    prototypeObj["yOrigin"] = model.prototype.yOrigin;
                    prototypeObj["zOrigin"] = model.prototype.zOrigin;
                    prototypeObj["nx"] = model.prototype.nx;
                    prototypeObj["ny"] = model.prototype.ny;
                    prototypeObj["nz"] = model.prototype.nz;
                    prototypeObj["parentX"] = model.prototype.parentX;
                    prototypeObj["parentY"] = model.prototype.parentY;
                    prototypeObj["parentZ"] = model.prototype.parentZ;
                    obj["prototype"] = prototypeObj;
                }
                const BlockModelDisplaySettings settings = m_blockModelSettings.value(node->name());
                QJsonObject displayObj;
                displayObj["blocksEnabled"] = settings.blocksEnabled;
                displayObj["linesEnabled"] = settings.linesEnabled;
                displayObj["labelsEnabled"] = settings.labelsEnabled;
                displayObj["blockColor"] = settings.blockColor.name(QColor::HexRgb);
                displayObj["lineColor"] = settings.lineColor.name(QColor::HexRgb);
                displayObj["blockLegend"] = settings.blockLegend;
                displayObj["lineLegend"] = settings.lineLegend;
                displayObj["labelField"] = settings.labelField;
                displayObj["labelFontSize"] = settings.labelFontSize;
                displayObj["blockGapPercent"] = settings.blockGapPercent;
                displayObj["renderMode"] = settings.renderMode == BlockModelRenderMode::Solid3D ? "3D" : "2D";
                obj["display"] = displayObj;
            }
        } else if (node->isEconomicModel()) {
            LayerNode *ancestorBlockModel = FindBlockModelAncestor(node);
            const QString blockModelName = ancestorBlockModel ? ancestorBlockModel->name() : QString();
            const EconomicModelDefinition definition = m_economicModels.value(
                EconomicModelKey(blockModelName, node->name()));
            obj["blockModelLayerName"] = definition.blockModelLayerName.isEmpty()
                ? blockModelName
                : definition.blockModelLayerName;

            QJsonArray fieldRolesArray;
            QStringList fieldNames = definition.fieldRoles.keys();
            fieldNames.sort(Qt::CaseInsensitive);
            for (const QString &fieldName : fieldNames) {
                QJsonObject roleObj;
                roleObj["fieldName"] = fieldName;
                roleObj["role"] = definition.fieldRoles.value(fieldName);
                roleObj["unit"] = definition.fieldUnits.value(fieldName);
                fieldRolesArray.append(roleObj);
            }
            obj["fieldRoles"] = fieldRolesArray;
            QJsonArray uniqueValuesArray;
            QStringList uniqueFields = definition.uniqueFieldValues.keys();
            uniqueFields.sort(Qt::CaseInsensitive);
            for (const QString &fieldName : uniqueFields) {
                QJsonObject uniqueObj;
                uniqueObj["fieldName"] = fieldName;
                QJsonArray valuesArray;
                for (const QString &value : definition.uniqueFieldValues.value(fieldName)) {
                    valuesArray.append(value);
                }
                uniqueObj["values"] = valuesArray;
                uniqueValuesArray.append(uniqueObj);
            }
            obj["uniqueFieldValues"] = uniqueValuesArray;
            QJsonArray variablesArray;
            for (const auto &variable : definition.variables) {
                QJsonObject variableObj;
                variableObj["name"] = variable.name;
                variableObj["formula"] = variable.formula;
                variablesArray.append(variableObj);
            }
            obj["variables"] = variablesArray;
            QJsonArray rockTypesArray;
            for (const auto &rockType : definition.rockTypeSettings) {
                QJsonObject rockTypeObj;
                rockTypeObj["rockType"] = rockType.rockType;
                rockTypeObj["dilution"] = rockType.dilution;
                rockTypeObj["miningRecovery"] = rockType.miningRecovery;
                rockTypeObj["miningCost"] = rockType.miningCost;
                QJsonArray destinationsArray;
                for (const auto &destination : rockType.destinations) {
                    QJsonObject destinationObj;
                    destinationObj["enabled"] = destination.enabled;
                    destinationObj["name"] = destination.name;
                    destinationObj["processingCost"] = destination.processingCost;
                    QJsonArray productValuesArray;
                    for (const QString &value : destination.productValues) {
                        productValuesArray.append(value);
                    }
                    destinationObj["productValues"] = productValuesArray;
                    destinationsArray.append(destinationObj);
                }
                rockTypeObj["destinations"] = destinationsArray;
                rockTypesArray.append(rockTypeObj);
            }
            obj["rockTypes"] = rockTypesArray;
            obj["profitModel"] = definition.profitModel;
            obj["profitField"] = definition.profitField;
            obj["generatedCellCount"] = definition.generatedCellCount;
            if (!definition.generatedInternalPath.isEmpty()) {
                QString fileName = QString("%1_%2.gvbm")
                    .arg(SanitizeName(obj["blockModelLayerName"].toString()), SanitizeName(node->name()));
                if (usedFiles.contains(fileName)) {
                    int suffix = 1;
                    QString candidate;
                    do {
                        candidate = QString("%1_%2_%3.gvbm")
                            .arg(SanitizeName(obj["blockModelLayerName"].toString()), SanitizeName(node->name()))
                            .arg(suffix++);
                    } while (usedFiles.contains(candidate));
                    fileName = candidate;
                }
                usedFiles.insert(fileName);

                const QString relPath = QDir(dataDirName).filePath(QDir("economicmodels").filePath(fileName));
                const QString fullPath = projectDir.filePath(relPath);
                QDir().mkpath(QFileInfo(fullPath).absolutePath());
                QString sourcePath = definition.generatedInternalPath;
                if (!QFileInfo::exists(sourcePath) && !definition.generatedStoredRelativePath.isEmpty()) {
                    sourcePath = projectDir.filePath(definition.generatedStoredRelativePath);
                }
                if (QFileInfo::exists(sourcePath)) {
                    if (QFileInfo(sourcePath).absoluteFilePath() != QFileInfo(fullPath).absoluteFilePath()) {
                        QFile::remove(fullPath);
                        if (!QFile::copy(sourcePath, fullPath)) {
                            QMessageBox::warning(this, "Save Project", QString("Unable to save economic model file: %1").arg(node->name()));
                        }
                    }
                    obj["generatedData"] = relPath;
                }
            }
        }

        QJsonArray childrenArray;
        for (LayerNode *child : node->children()) {
            childrenArray.append(serializeNode(child));
        }
        obj["children"] = childrenArray;

        return obj;
    };

    QJsonArray rootChildren;
    for (LayerNode *child : m_layerModel->root()->children()) {
        rootChildren.append(serializeNode(child));
    }

    QJsonObject rootObj;
    rootObj["version"] = 1;
    rootObj["dataDir"] = dataDirName;
    rootObj["layers"] = rootChildren;
    QJsonArray legendArray;
    for (const BlockModelLegend &legend : m_blockModelLegends) {
        QJsonObject legendObj;
        legendObj["name"] = legend.name;
        legendObj["layerName"] = legend.layerName;
        legendObj["fieldName"] = legend.fieldName;
        QJsonArray binsArray;
        for (const BlockModelLegendBin &bin : legend.bins) {
            QJsonObject binObj;
            binObj["visible"] = bin.visible;
            binObj["min"] = bin.minValue;
            if (std::isinf(bin.maxValue)) {
                binObj["max"] = "+oo";
            } else {
                binObj["max"] = bin.maxValue;
            }
            binObj["color"] = bin.color.name(QColor::HexRgb);
            binObj["label"] = bin.label;
            binsArray.append(binObj);
        }
        legendObj["bins"] = binsArray;
        legendArray.append(legendObj);
    }
    rootObj["blockModelLegends"] = legendArray;

    auto serializeRouteEntries = [](const std::vector<RoutePersistentEntry> &entries) {
        QJsonArray arr;
        for (const auto &entry : entries) {
            QJsonObject obj;
            obj["filePath"] = entry.filePath;
            obj["layerName"] = entry.layerName;
            obj["displayName"] = entry.displayName;

            QJsonArray polylinesArr;
            for (const auto &polyline : entry.polylines) {
                QJsonArray polylineArr;
                for (const auto &point : polyline) {
                    QJsonArray pointArr{point[0], point[1], point[2]};
                    polylineArr.append(pointArr);
                }
                polylinesArr.append(polylineArr);
            }
            obj["polylines"] = polylinesArr;
            arr.append(obj);
        }
        return arr;
    };

    auto serializeSpeedBands = [](const std::vector<RoutePersistentSpeed> &bands) {
        QJsonArray arr;
        for (const auto &band : bands) {
            QJsonObject obj;
            obj["name"] = band.name;
            obj["minSlope"] = band.minSlope;
            obj["maxSlope"] = band.maxSlope;
            obj["speed"] = band.speed;
            arr.append(obj);
        }
        return arr;
    };

    QJsonObject routeObj;
    routeObj["starts"] = serializeRouteEntries(g_routeSetupState.starts);
    routeObj["mains"] = serializeRouteEntries(g_routeSetupState.mains);
    routeObj["ends"] = serializeRouteEntries(g_routeSetupState.ends);
    routeObj["loadedSpeeds"] = serializeSpeedBands(g_routeSetupState.loadedSpeeds);
    routeObj["unloadedSpeeds"] = serializeSpeedBands(g_routeSetupState.unloadedSpeeds);
    routeObj["accelLoaded"] = g_routeSetupState.accelLoaded;
    routeObj["accelUnloaded"] = g_routeSetupState.accelUnloaded;
    routeObj["decelLoaded"] = g_routeSetupState.decelLoaded;
    routeObj["decelUnloaded"] = g_routeSetupState.decelUnloaded;
    QJsonArray calcRowsArr;
    for (const auto &row : g_routeSetupState.calcRows) {
        QJsonObject rowObj;
        rowObj["start"] = row.startDisplay;
        rowObj["main"] = row.mainDisplay;
        rowObj["end"] = row.endDisplay;
        rowObj["name"] = row.name;
        calcRowsArr.append(rowObj);
    }
    routeObj["calculateRows"] = calcRowsArr;
    rootObj["routeSetup"] = routeObj;

    if (m_renderer && m_renderer->GetActiveCamera()) {
        vtkCamera *camera = m_renderer->GetActiveCamera();
        double pos[3];
        double focal[3];
        double up[3];
        camera->GetPosition(pos);
        camera->GetFocalPoint(focal);
        camera->GetViewUp(up);
        QJsonArray posArr{pos[0], pos[1], pos[2]};
        QJsonArray focalArr{focal[0], focal[1], focal[2]};
        QJsonArray upArr{up[0], up[1], up[2]};
        rootObj["cameraPosition"] = posArr;
        rootObj["cameraFocal"] = focalArr;
        rootObj["cameraUp"] = upArr;
        rootObj["cameraParallelScale"] = camera->GetParallelScale();
    }

    QSaveFile file(projectPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Save Project", "Unable to write the project file.");
        return false;
    }
    file.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(this, "Save Project", "Unable to save the project file.");
        return false;
    }

    m_projectPath = projectPath;
    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Project saved: %1").arg(projectPath));
    }
    return true;
}

bool MainWindow::LoadProjectFrom(const QString &path)
{
    const QString projectPath = EnsureProjectExtension(path);
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Open Project", "Unable to open the project file.");
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        QMessageBox::warning(this, "Open Project", "Invalid project file.");
        return false;
    }

    ResetProjectState();

    const QJsonObject rootObj = doc.object();
    const QString dataDirName = rootObj.value("dataDir").toString(QFileInfo(ProjectDataDir(projectPath)).fileName());
    const QDir projectDir(QFileInfo(projectPath).absoluteDir());

    m_blockModelLegends.clear();
    const QJsonArray legendArray = rootObj.value("blockModelLegends").toArray();
    for (const QJsonValue &legendVal : legendArray) {
        if (!legendVal.isObject()) {
            continue;
        }
        const QJsonObject legendObj = legendVal.toObject();
        BlockModelLegend legend;
        legend.name = legendObj.value("name").toString();
        legend.layerName = legendObj.value("layerName").toString();
        legend.fieldName = legendObj.value("fieldName").toString();
        const QJsonArray binsArray = legendObj.value("bins").toArray();
        for (const QJsonValue &binVal : binsArray) {
            if (!binVal.isObject()) {
                continue;
            }
            const QJsonObject binObj = binVal.toObject();
            BlockModelLegendBin bin;
            bin.visible = binObj.value("visible").toBool(true);
            bin.minValue = binObj.value("min").toDouble();
            const QJsonValue maxVal = binObj.value("max");
            if (maxVal.isString()) {
                const QString maxText = maxVal.toString();
                bin.maxValue = (maxText == "+oo" || maxText == "+inf" || maxText.compare("inf", Qt::CaseInsensitive) == 0)
                    ? std::numeric_limits<double>::infinity()
                    : maxText.toDouble();
            } else {
                bin.maxValue = maxVal.toDouble();
            }
            const QColor color(binObj.value("color").toString(QColor(80, 190, 190).name(QColor::HexRgb)));
            bin.color = color.isValid() ? color : QColor(80, 190, 190);
            bin.label = binObj.value("label").toString();
            legend.bins.append(bin);
        }
        if (!legend.name.isEmpty() && !legend.layerName.isEmpty() && !legend.fieldName.isEmpty()) {
            m_blockModelLegends.append(legend);
        }
    }

    g_routeSetupState = RoutePersistentSetup{};
    if (rootObj.contains("routeSetup") && rootObj.value("routeSetup").isObject()) {
        const QJsonObject routeObj = rootObj.value("routeSetup").toObject();

        auto parseRouteEntries = [](const QJsonArray &arr) {
            std::vector<RoutePersistentEntry> entries;
            entries.reserve(static_cast<std::size_t>(arr.size()));
            for (const QJsonValue &entryVal : arr) {
                if (!entryVal.isObject()) {
                    continue;
                }
                const QJsonObject entryObj = entryVal.toObject();
                RoutePersistentEntry entry;
                entry.filePath = entryObj.value("filePath").toString();
                entry.layerName = entryObj.value("layerName").toString();
                entry.displayName = entryObj.value("displayName").toString();
                const QJsonArray polyArr = entryObj.value("polylines").toArray();
                entry.polylines.reserve(static_cast<std::size_t>(polyArr.size()));
                for (const QJsonValue &polyVal : polyArr) {
                    if (!polyVal.isArray()) {
                        continue;
                    }
                    DxfPolyline polyline;
                    const QJsonArray polylineArr = polyVal.toArray();
                    polyline.reserve(static_cast<std::size_t>(polylineArr.size()));
                    for (const QJsonValue &pointVal : polylineArr) {
                        if (!pointVal.isArray()) {
                            continue;
                        }
                        const QJsonArray pointArr = pointVal.toArray();
                        if (pointArr.size() < 3) {
                            continue;
                        }
                        polyline.push_back({pointArr[0].toDouble(), pointArr[1].toDouble(), pointArr[2].toDouble()});
                    }
                    if (polyline.size() >= 2) {
                        entry.polylines.push_back(std::move(polyline));
                    }
                }
                if (entry.displayName.isEmpty()) {
                    QString fileName = QFileInfo(entry.filePath).fileName();
                    if (fileName.isEmpty()) {
                        fileName = "DXF";
                    }
                    entry.displayName = entry.layerName.isEmpty()
                        ? fileName
                        : QString("%1 - %2").arg(fileName, entry.layerName);
                }
                entries.push_back(std::move(entry));
            }
            return entries;
        };

        auto parseSpeedBands = [](const QJsonArray &arr) {
            std::vector<RoutePersistentSpeed> bands;
            bands.reserve(static_cast<std::size_t>(arr.size()));
            for (const QJsonValue &val : arr) {
                if (!val.isObject()) {
                    continue;
                }
                const QJsonObject obj = val.toObject();
                RoutePersistentSpeed band;
                band.name = obj.value("name").toString();
                band.minSlope = obj.value("minSlope").toDouble();
                band.maxSlope = obj.value("maxSlope").toDouble();
                band.speed = obj.value("speed").toDouble();
                bands.push_back(band);
            }
            return bands;
        };

        g_routeSetupState.starts = parseRouteEntries(routeObj.value("starts").toArray());
        g_routeSetupState.mains = parseRouteEntries(routeObj.value("mains").toArray());
        g_routeSetupState.ends = parseRouteEntries(routeObj.value("ends").toArray());
        g_routeSetupState.loadedSpeeds = parseSpeedBands(routeObj.value("loadedSpeeds").toArray());
        g_routeSetupState.unloadedSpeeds = parseSpeedBands(routeObj.value("unloadedSpeeds").toArray());
        g_routeSetupState.accelLoaded = routeObj.value("accelLoaded").toDouble(g_routeSetupState.accelLoaded);
        g_routeSetupState.accelUnloaded = routeObj.value("accelUnloaded").toDouble(g_routeSetupState.accelUnloaded);
        g_routeSetupState.decelLoaded = routeObj.value("decelLoaded").toDouble(g_routeSetupState.decelLoaded);
        g_routeSetupState.decelUnloaded = routeObj.value("decelUnloaded").toDouble(g_routeSetupState.decelUnloaded);

        const QJsonArray calcRows = routeObj.value("calculateRows").toArray();
        g_routeSetupState.calcRows.clear();
        g_routeSetupState.calcRows.reserve(static_cast<std::size_t>(calcRows.size()));
        for (const QJsonValue &rowVal : calcRows) {
            if (!rowVal.isObject()) {
                continue;
            }
            const QJsonObject rowObj = rowVal.toObject();
            RoutePersistentCalcRow row;
            row.startDisplay = rowObj.value("start").toString();
            row.mainDisplay = rowObj.value("main").toString();
            row.endDisplay = rowObj.value("end").toString();
            row.name = rowObj.value("name").toString();
            g_routeSetupState.calcRows.push_back(row);
        }
    }

    auto ensureRender = [&](const QString &layerName) -> LayerRenderData & {
        LayerRenderData &render = m_layerRenders[layerName];
        if (!render.actor) {
            vtkNew<vtkPolyDataMapper> mapper;
            vtkNew<vtkActor> actor;
            actor->SetMapper(mapper);
            actor->GetProperty()->SetColor(0.85, 0.9, 0.95);
            actor->GetProperty()->SetLineWidth(1.2);
            actor->GetProperty()->SetRepresentationToWireframe();
            m_renderer->AddActor(actor);
            render.mapper = mapper;
            render.actor = actor;

            SceneItem item;
            item.actor = actor;
            item.data = nullptr;
            item.highlightActor = nullptr;
            item.layerName = layerName;
            m_sceneItems.push_back(item);
            m_actorIndex[actor] = m_sceneItems.size() - 1;
        }
        return render;
    };

    std::function<void(const QJsonObject &, LayerNode *)> loadNode = [&](const QJsonObject &obj, LayerNode *parent) {
        const QString type = obj.value("type").toString();
        const QString name = obj.value("name").toString();
        LayerNode *node = nullptr;

        if (type == "geometry") {
            node = m_layerModel->addGeometryLayer(name, parent);
            LayerRenderData &render = ensureRender(name);

            const QJsonArray versions = obj.value("versions").toArray();
            for (const QJsonValue &verVal : versions) {
                const QJsonObject verObj = verVal.toObject();
                const qint64 key = static_cast<qint64>(verObj.value("key").toInteger());
                QString relPath = verObj.value("data").toString();
                if (relPath.isEmpty()) {
                    continue;
                }
                const QString fullPath = projectDir.filePath(relPath);
                vtkSmartPointer<vtkPolyData> poly = vtkSmartPointer<vtkPolyData>::New();
                if (QFileInfo::exists(fullPath)) {
                    vtkNew<vtkXMLPolyDataReader> reader;
                    reader->SetFileName(fullPath.toStdString().c_str());
                    reader->Update();
                    if (reader->GetOutput()) {
                        poly->ShallowCopy(reader->GetOutput());
                    }
                }
                render.versions.insert(key, poly);
                m_layerModel->addVersion(node, key);
            }

            const qint64 active = static_cast<qint64>(obj.value("activeDate").toInteger());
            if (active > 0) {
                m_layerModel->setActiveDate(node, active);
            }
            OnLayerVisibilityChanged(node, node->isVisible());
            if (node->activeDate() > 0) {
                UpdateLayerRender(name, node->activeDate());
            }
        } else if (type == "blockModel") {
            node = m_layerModel->addBlockModelLayer(name, parent);
            if (node) {
                node->setVisible(false);
            }
            const QString relPath = obj.value("data").toString();
            const QString fullPath = projectDir.filePath(relPath);
            BlockModelLayerData data;
            data.internalPath = fullPath;
            data.storedRelativePath = relPath;
            data.description = obj.value("description").toString();
            if (QFileInfo::exists(fullPath)) {
                try {
                    data.info = visor::datamine::DmBlockModelImporter::readInternalInfo(fullPath.toStdString());
                    if (data.description.isEmpty()) {
                        data.description = QString::fromStdString(data.info.sourceHeader.description);
                    }
                } catch (const std::exception &ex) {
                    QMessageBox::warning(
                        this,
                        "Open Project",
                        QString("Unable to load block model '%1':\n%2")
                            .arg(name, QString::fromLocal8Bit(ex.what())));
                }
            }
            m_blockModelLayers.insert(name, std::move(data));
            BlockModelDisplaySettings settings;
            const QJsonObject displayObj = obj.value("display").toObject();
            if (!displayObj.isEmpty()) {
                settings.blocksEnabled = displayObj.value("blocksEnabled").toBool(settings.blocksEnabled);
                settings.linesEnabled = displayObj.value("linesEnabled").toBool(settings.linesEnabled);
                settings.labelsEnabled = displayObj.value("labelsEnabled").toBool(settings.labelsEnabled);
                const QColor blockColor(displayObj.value("blockColor").toString(settings.blockColor.name()));
                if (blockColor.isValid()) {
                    settings.blockColor = blockColor;
                }
                const QColor lineColor(displayObj.value("lineColor").toString(settings.lineColor.name()));
                if (lineColor.isValid()) {
                    settings.lineColor = lineColor;
                }
                settings.blockLegend = displayObj.value("blockLegend").toString();
                settings.lineLegend = displayObj.value("lineLegend").toString();
                settings.labelField = displayObj.value("labelField").toString();
                settings.labelFontSize = displayObj.value("labelFontSize").toInt(settings.labelFontSize);
                settings.blockGapPercent = displayObj.value("blockGapPercent").toDouble(settings.blockGapPercent);
                const QString renderMode = displayObj.value("renderMode").toString("2D");
                settings.renderMode = renderMode.compare("3D", Qt::CaseInsensitive) == 0
                    ? BlockModelRenderMode::Solid3D
                    : BlockModelRenderMode::Section2D;
            }
            if (parent && parent->isEconomicModel() && data.description.startsWith("MineFlow pit shell", Qt::CaseInsensitive)) {
                settings.renderMode = BlockModelRenderMode::Solid3D;
                settings.blocksEnabled = true;
            }
            m_blockModelSettings.insert(name, settings);
            const QJsonArray children = obj.value("children").toArray();
            for (const QJsonValue &childVal : children) {
                loadNode(childVal.toObject(), node);
            }
        } else if (type == "economicModel") {
            node = m_layerModel->addEconomicModelLayer(name, parent);
            if (!node) {
                return;
            }
            EconomicModelDefinition definition;
            definition.name = name;
            LayerNode *ancestorBlockModel = FindBlockModelAncestor(node);
            definition.blockModelLayerName = obj.value("blockModelLayerName").toString(
                ancestorBlockModel ? ancestorBlockModel->name() : QString());
            definition.profitModel = obj.value("profitModel").toBool(false);
            definition.profitField = obj.value("profitField").toString();
            definition.generatedCellCount = static_cast<qint64>(obj.value("generatedCellCount").toDouble(0.0));
            definition.generatedStoredRelativePath = obj.value("generatedData").toString();
            if (!definition.generatedStoredRelativePath.isEmpty()) {
                definition.generatedInternalPath = projectDir.filePath(definition.generatedStoredRelativePath);
            }
            const QJsonArray fieldRolesArray = obj.value("fieldRoles").toArray();
            for (const QJsonValue &roleVal : fieldRolesArray) {
                if (!roleVal.isObject()) {
                    continue;
                }
                const QJsonObject roleObj = roleVal.toObject();
                const QString fieldName = roleObj.value("fieldName").toString();
                const QString role = roleObj.value("role").toString("None");
                if (!fieldName.isEmpty()) {
                    definition.fieldRoles.insert(fieldName, role);
                    const QString unit = roleObj.value("unit").toString();
                    if (!unit.isEmpty()) {
                        definition.fieldUnits.insert(fieldName, unit);
                    }
                }
            }
            const QJsonArray uniqueValuesArray = obj.value("uniqueFieldValues").toArray();
            for (const QJsonValue &uniqueVal : uniqueValuesArray) {
                if (!uniqueVal.isObject()) {
                    continue;
                }
                const QJsonObject uniqueObj = uniqueVal.toObject();
                const QString fieldName = uniqueObj.value("fieldName").toString();
                QStringList values;
                const QJsonArray valuesArray = uniqueObj.value("values").toArray();
                for (const QJsonValue &value : valuesArray) {
                    values << value.toString();
                }
                values.removeDuplicates();
                values.sort(Qt::CaseInsensitive);
                if (!fieldName.isEmpty() && !values.isEmpty()) {
                    definition.uniqueFieldValues.insert(fieldName, values);
                }
            }
            const QJsonArray variablesArray = obj.value("variables").toArray();
            for (const QJsonValue &variableVal : variablesArray) {
                if (!variableVal.isObject()) {
                    continue;
                }
                const QJsonObject variableObj = variableVal.toObject();
                EconomicModelDefinition::Variable variable;
                variable.name = variableObj.value("name").toString();
                variable.formula = variableObj.value("formula").toString();
                if (!variable.name.isEmpty()) {
                    definition.variables.append(variable);
                }
            }
            const QJsonArray rockTypesArray = obj.value("rockTypes").toArray();
            for (const QJsonValue &rockTypeVal : rockTypesArray) {
                if (!rockTypeVal.isObject()) {
                    continue;
                }
                const QJsonObject rockTypeObj = rockTypeVal.toObject();
                EconomicModelDefinition::RockTypeSettings rockType;
                rockType.rockType = rockTypeObj.value("rockType").toString();
                rockType.dilution = rockTypeObj.value("dilution").toDouble();
                rockType.miningRecovery = rockTypeObj.value("miningRecovery").toDouble(100.0);
                rockType.miningCost = rockTypeObj.value("miningCost").toDouble();
                const QJsonArray destinationsArray = rockTypeObj.value("destinations").toArray();
                for (const QJsonValue &destinationVal : destinationsArray) {
                    if (!destinationVal.isObject()) {
                        continue;
                    }
                    const QJsonObject destinationObj = destinationVal.toObject();
                    EconomicModelDefinition::Destination destination;
                    destination.enabled = destinationObj.value("enabled").toBool(true);
                    destination.name = destinationObj.value("name").toString();
                    destination.processingCost = destinationObj.value("processingCost").toDouble();
                    const QJsonArray productValuesArray = destinationObj.value("productValues").toArray();
                    for (const QJsonValue &productValue : productValuesArray) {
                        destination.productValues << productValue.toString();
                    }
                    if (!destination.name.isEmpty()) {
                        rockType.destinations.append(destination);
                    }
                }
                if (!rockType.rockType.isEmpty()) {
                    definition.rockTypeSettings.append(rockType);
                }
            }
            if (!definition.blockModelLayerName.isEmpty()) {
                m_economicModels.insert(EconomicModelKey(definition.blockModelLayerName, name), definition);
            }
            const QJsonArray children = obj.value("children").toArray();
            for (const QJsonValue &childVal : children) {
                loadNode(childVal.toObject(), node);
            }
        } else {
            node = m_layerModel->addFolder(name, parent);
            if (node) {
                node->setSystemNode(obj.value("system").toBool(false));
            }
            if (type == "legend" && parent && parent->type() == LayerNode::Type::Folder) {
                if (node) {
                    m_layerModel->removeNode(node);
                }
                node = m_layerModel->addLegendLayer(name, parent);
            }
            const QJsonArray children = obj.value("children").toArray();
            for (const QJsonValue &childVal : children) {
                loadNode(childVal.toObject(), node);
            }
        }
    };

    const QJsonArray layers = rootObj.value("layers").toArray();
    for (const QJsonValue &layerVal : layers) {
        loadNode(layerVal.toObject(), m_layerModel->root());
    }
    for (LayerNode *blockNode : m_layerModel->blockModelLayers()) {
        if (blockNode && !(blockNode->parent() && blockNode->parent()->isEconomicModel())) {
            EnsureBlockModelSystemFolders(blockNode);
        }
    }
    SyncLegendLayerNodes();

    if (rootObj.contains("cameraPosition") && rootObj.contains("cameraFocal")) {
        QJsonArray posArr = rootObj.value("cameraPosition").toArray();
        QJsonArray focalArr = rootObj.value("cameraFocal").toArray();
        QJsonArray upArr = rootObj.value("cameraUp").toArray();
        if (posArr.size() == 3 && focalArr.size() == 3 && m_renderer) {
            vtkCamera *camera = m_renderer->GetActiveCamera();
            if (camera) {
                camera->ParallelProjectionOn();
                camera->SetPosition(posArr[0].toDouble(), posArr[1].toDouble(), posArr[2].toDouble());
                camera->SetFocalPoint(focalArr[0].toDouble(), focalArr[1].toDouble(), focalArr[2].toDouble());
                if (upArr.size() == 3) {
                    camera->SetViewUp(upArr[0].toDouble(), upArr[1].toDouble(), upArr[2].toDouble());
                }
                if (rootObj.contains("cameraParallelScale")) {
                    camera->SetParallelScale(std::max(rootObj.value("cameraParallelScale").toDouble(), 1e-9));
                }
                camera->OrthogonalizeViewUp();
            }
        }
    }

    m_layersView->collapseAll();
    m_projectPath = projectPath;
    m_renderer->ResetCameraClippingRange();
    m_renderWindow->Render();

    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Project loaded: %1").arg(projectPath));
    }
    return true;
}

void MainWindow::ResetProjectState()
{
    g_routeSetupState = RoutePersistentSetup{};
    ClearSelection();
    for (auto &item : m_sceneItems) {
        if (item.highlightActor) {
            m_renderer->RemoveActor(item.highlightActor);
        }
        if (item.actor) {
            m_renderer->RemoveActor(item.actor);
        }
    }
    m_sceneItems.clear();
    m_actorIndex.clear();
    m_layerRenders.clear();
    for (auto it = m_blockModelLayers.begin(); it != m_blockModelLayers.end(); ++it) {
        if (it->actor) {
            m_renderer->RemoveActor(it->actor);
        }
        if (it->selectionActor) {
            m_renderer->RemoveActor(it->selectionActor);
        }
        if (it->sectionActor) {
            m_renderer->RemoveActor(it->sectionActor);
        }
    }
    m_blockModelLayers.clear();
    m_blockModelSettings.clear();
    m_blockModelLegends.clear();
    m_economicModels.clear();
    HideSelectedBlockProperties();

    if (m_layerModel) {
        m_layerModel->deleteLater();
        m_layerModel = nullptr;
    }
    m_layerModel = new LayerModel(m_layersView);
    m_layersView->setModel(m_layerModel);
    m_layersView->setIndentation(16);
    m_layersView->setAlternatingRowColors(false);
    m_layersView->setHeaderHidden(true);
    m_layersView->setRootIsDecorated(true);
    m_layersView->setUniformRowHeights(true);
    m_layersView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_layersView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_layersView->setColumnHidden(1, true);
    m_layersView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_layersView->selectionModel(), &QItemSelectionModel::currentChanged,
        this, &MainWindow::OnLayerSelectionChanged, Qt::UniqueConnection);
    connect(m_layersView, &QTreeView::doubleClicked, this, &MainWindow::OnLayerDoubleClicked, Qt::UniqueConnection);
    connect(m_layersView, &QTreeView::customContextMenuRequested, this, &MainWindow::OnLayerContextMenu, Qt::UniqueConnection);
    connect(m_layerModel, &LayerModel::visibilityChanged, this, &MainWindow::OnLayerVisibilityChanged, Qt::UniqueConnection);

    if (m_layerDateCombo) {
        m_layerDateCombo->blockSignals(true);
        m_layerDateCombo->clear();
        m_layerDateCombo->setEnabled(false);
        m_layerDateCombo->blockSignals(false);
    }
    m_currentLayerIndex = QModelIndex();
    m_hasScene = false;
    m_lineWidthReference = -1.0;
    StopRouteVisualization(true);
}

void MainWindow::ClearSelection()
{
    for (auto &item : m_sceneItems) {
        if (item.highlightActor) {
            m_renderer->RemoveActor(item.highlightActor);
            item.highlightActor = nullptr;
        }
    }
    for (auto it = m_blockModelLayers.begin(); it != m_blockModelLayers.end(); ++it) {
        if (it->selectionActor) {
            m_renderer->RemoveActor(it->selectionActor);
            it->selectionActor = nullptr;
        }
    }
    HideSelectedBlockProperties();
    m_blockSectionPickActive = false;
    m_blockSectionPoints.clear();
}

void MainWindow::SelectArea(const QPoint &start, const QPoint &end, bool crossing)
{
    if (!m_vtkWidget || !m_renderer) {
        return;
    }

    const qreal dpr = m_vtkWidget->devicePixelRatioF();
    const int height = static_cast<int>(std::lround(m_vtkWidget->height() * dpr));
    const int x1 = static_cast<int>(std::lround(start.x() * dpr));
    const int y1 = height - 1 - static_cast<int>(std::lround(start.y() * dpr));
    const int x2 = static_cast<int>(std::lround(end.x() * dpr));
    const int y2 = height - 1 - static_cast<int>(std::lround(end.y() * dpr));
    const int vx1 = std::min(x1, x2);
    const int vx2 = std::max(x1, x2);
    const int vy1 = std::min(y1, y2);
    const int vy2 = std::max(y1, y2);

    ClearSelection();
    SelectBlockModelCell(start, end);

    if (m_sceneItems.empty()) {
        if (m_renderWindow) {
            m_renderWindow->Render();
        }
        return;
    }

    vtkNew<vtkRenderedAreaPicker> picker;
    picker->AreaPick(vx1, vy1, vx2, vy2, m_renderer);
    vtkPlanes *frustum = picker->GetFrustum();
    vtkProp3DCollection *props = picker->GetProp3Ds();
    if (!frustum || !props) {
        return;
    }

    props->InitTraversal();
    vtkProp3D *prop = nullptr;
    while ((prop = props->GetNextProp3D()) != nullptr) {
        vtkActor *actor = vtkActor::SafeDownCast(prop);
        if (!actor) {
            continue;
        }
        auto it = m_actorIndex.find(actor);
        if (it == m_actorIndex.end()) {
            continue;
        }

        SceneItem &item = m_sceneItems[it->second];
        if (!item.data) {
            continue;
        }

        vtkNew<vtkExtractPolyDataGeometry> extract;
        extract->SetInputData(item.data);
        extract->SetImplicitFunction(frustum);
        extract->SetExtractInside(1);
        extract->SetExtractBoundaryCells(crossing ? 1 : 0);
        extract->SetPassPoints(false);
        extract->Update();

        vtkPolyData *selected = extract->GetOutput();
        if (!selected || selected->GetNumberOfCells() == 0) {
            continue;
        }

        vtkNew<vtkPolyDataMapper> selMapper;
        selMapper->SetInputData(selected);

        vtkNew<vtkActor> selActor;
        selActor->SetMapper(selMapper);
        selActor->GetProperty()->SetColor(0.2, 0.9, 1.0);
        selActor->GetProperty()->SetLineWidth(2.2);
        selActor->GetProperty()->SetPointSize(6.0);
        selActor->GetProperty()->SetRepresentationToWireframe();
        selActor->GetProperty()->LightingOff();

        m_renderer->AddActor(selActor);
        item.highlightActor = selActor;
    }

    m_renderWindow->Render();
}

void MainWindow::SelectBlockModelCell(const QPoint &start, const QPoint &end)
{
    if (!m_vtkWidget || !m_renderer) {
        return;
    }

    const qreal dpr = m_vtkWidget->devicePixelRatioF();
    const int height = static_cast<int>(std::lround(m_vtkWidget->height() * dpr));
    const int x1 = static_cast<int>(std::lround(start.x() * dpr));
    const int y1 = height - 1 - static_cast<int>(std::lround(start.y() * dpr));
    const int x2 = static_cast<int>(std::lround(end.x() * dpr));
    const int y2 = height - 1 - static_cast<int>(std::lround(end.y() * dpr));
    const int vx1 = std::min(x1, x2);
    const int vx2 = std::max(x1, x2);
    const int vy1 = std::min(y1, y2);
    const int vy2 = std::max(y1, y2);
    const double centerX = 0.5 * static_cast<double>(vx1 + vx2);
    const double centerY = 0.5 * static_cast<double>(vy1 + vy2);

    auto displayToWorld = [this](double x, double y, double z) {
        std::array<double, 3> world{0.0, 0.0, 0.0};
        m_renderer->SetDisplayPoint(x, y, z);
        m_renderer->DisplayToWorld();
        double *homogeneous = m_renderer->GetWorldPoint();
        if (!homogeneous) {
            return world;
        }
        const double w = std::abs(homogeneous[3]) > 1e-12 ? homogeneous[3] : 1.0;
        world = {homogeneous[0] / w, homogeneous[1] / w, homogeneous[2] / w};
        return world;
    };
    const std::array<double, 3> rayStart = displayToWorld(centerX, centerY, 0.0);
    const std::array<double, 3> rayEnd = displayToWorld(centerX, centerY, 1.0);
    std::array<double, 3> rayDir{
        rayEnd[0] - rayStart[0],
        rayEnd[1] - rayStart[1],
        rayEnd[2] - rayStart[2]};
    const double rayLength = std::sqrt(
        rayDir[0] * rayDir[0] +
        rayDir[1] * rayDir[1] +
        rayDir[2] * rayDir[2]);
    if (rayLength <= 1e-12) {
        return;
    }
    rayDir[0] /= rayLength;
    rayDir[1] /= rayLength;
    rayDir[2] /= rayLength;

    auto rayBoxHit = [](const std::array<double, 3> &origin,
                        const std::array<double, 3> &dir,
                        const double bounds[6],
                        double &hitDistance) {
        double tMin = 0.0;
        double tMax = std::numeric_limits<double>::max();
        for (int axis = 0; axis < 3; ++axis) {
            const double minValue = bounds[axis * 2];
            const double maxValue = bounds[axis * 2 + 1];
            if (std::abs(dir[axis]) < 1e-12) {
                if (origin[axis] < minValue || origin[axis] > maxValue) {
                    return false;
                }
                continue;
            }
            double t1 = (minValue - origin[axis]) / dir[axis];
            double t2 = (maxValue - origin[axis]) / dir[axis];
            if (t1 > t2) {
                std::swap(t1, t2);
            }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) {
                return false;
            }
        }
        hitDistance = tMin;
        return true;
    };

    struct Candidate
    {
        QString layerName;
        std::size_t index = 0;
        double hitDistance = std::numeric_limits<double>::max();
        bool section = false;
    };
    std::optional<Candidate> selected;

    for (auto it = m_blockModelLayers.begin(); it != m_blockModelLayers.end(); ++it) {
        if (it->sectionActor && it->sectionActor->GetVisibility() && !it->sectionPolygons.empty()) {
            for (std::size_t i = 0; i < it->sectionPolygons.size(); ++i) {
                const auto &polygon = it->sectionPolygons[i];
                if (polygon.empty()) {
                    continue;
                }
                double minX = std::numeric_limits<double>::max();
                double maxX = std::numeric_limits<double>::lowest();
                double minY = std::numeric_limits<double>::max();
                double maxY = std::numeric_limits<double>::lowest();
                double avgDepth = 0.0;
                for (const auto &p : polygon) {
                    m_renderer->SetWorldPoint(p[0], p[1], p[2], 1.0);
                    m_renderer->WorldToDisplay();
                    const double *display = m_renderer->GetDisplayPoint();
                    if (!display) {
                        continue;
                    }
                    minX = std::min(minX, display[0]);
                    maxX = std::max(maxX, display[0]);
                    minY = std::min(minY, display[1]);
                    maxY = std::max(maxY, display[1]);
                    avgDepth += display[2];
                }
                avgDepth /= static_cast<double>(polygon.size());
                constexpr double pickPad = 4.0;
                if (centerX < minX - pickPad || centerX > maxX + pickPad ||
                    centerY < minY - pickPad || centerY > maxY + pickPad) {
                    continue;
                }
                if (!selected || avgDepth < selected->hitDistance) {
                    selected = Candidate{it.key(), i, avgDepth, true};
                }
            }
        }

        if (!it->actor || !it->actor->GetVisibility() || it->previewCells.empty()) {
            continue;
        }
        const BlockModelDisplaySettings settings = m_blockModelSettings.value(it.key());
        const double scaleFactor = std::clamp(1.0 - (settings.blockGapPercent / 100.0), 0.05, 1.0);
        for (std::size_t i = 0; i < it->previewCells.size(); ++i) {
            const auto &cell = it->previewCells[i];
            const double halfX = std::max(std::abs(cell.xinc) * scaleFactor * 0.5, 1e-9);
            const double halfY = std::max(std::abs(cell.yinc) * scaleFactor * 0.5, 1e-9);
            const double halfZ = std::max(std::abs(cell.zinc) * scaleFactor * 0.5, 1e-9);
            const double bounds[6] = {
                cell.xc - halfX, cell.xc + halfX,
                cell.yc - halfY, cell.yc + halfY,
                cell.zc - halfZ, cell.zc + halfZ};
            double hitDistance = std::numeric_limits<double>::max();
            if (!rayBoxHit(rayStart, rayDir, bounds, hitDistance)) {
                continue;
            }
            if (!selected || hitDistance < selected->hitDistance) {
                selected = Candidate{it.key(), i, hitDistance, false};
            }
        }
    }

    if (!selected) {
        return;
    }

    auto layerIt = m_blockModelLayers.find(selected->layerName);
    if (layerIt == m_blockModelLayers.end()) {
        return;
    }
    if (selected->section && selected->index >= layerIt->sectionCells.size()) {
        return;
    }
    if (!selected->section && selected->index >= layerIt->previewCells.size()) {
        return;
    }

    const BlockModelDisplaySettings settings = m_blockModelSettings.value(selected->layerName);
    const double scaleFactor = std::clamp(1.0 - (settings.blockGapPercent / 100.0), 0.05, 1.0);
    const auto &cell = selected->section ? layerIt->sectionCells[selected->index] : layerIt->previewCells[selected->index];

    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    vtkNew<vtkPolyDataMapper> mapper;
    if (selected->section && selected->index < layerIt->sectionPolygons.size()) {
        vtkNew<vtkPoints> points;
        vtkNew<vtkCellArray> poly;
        std::vector<vtkIdType> ids;
        for (const auto &p : layerIt->sectionPolygons[selected->index]) {
            ids.push_back(points->InsertNextPoint(p[0], p[1], p[2]));
        }
        poly->InsertNextCell(static_cast<vtkIdType>(ids.size()), ids.data());
        vtkNew<vtkPolyData> selectedPoly;
        selectedPoly->SetPoints(points);
        selectedPoly->SetPolys(poly);
        mapper->SetInputData(selectedPoly);
    } else {
        vtkNew<vtkCubeSource> cube;
        cube->SetXLength(std::max(cell.xinc * scaleFactor, 1e-9));
        cube->SetYLength(std::max(cell.yinc * scaleFactor, 1e-9));
        cube->SetZLength(std::max(cell.zinc * scaleFactor, 1e-9));
        cube->Update();
        mapper->SetInputData(cube->GetOutput());
        actor->SetPosition(cell.xc, cell.yc, cell.zc);
    }
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(0.1, 0.9, 1.0);
    actor->GetProperty()->SetRepresentationToWireframe();
    actor->GetProperty()->SetLineWidth(3.0);
    actor->GetProperty()->LightingOff();
    m_renderer->AddActor(actor);
    layerIt->selectionActor = actor;

    visor::datamine::BlockCell propertyCell = cell;
    try {
        const std::int64_t row = (selected->section && selected->index < layerIt->sectionRows.size())
            ? layerIt->sectionRows[selected->index]
            : ((selected->index < layerIt->previewRows.size())
                ? layerIt->previewRows[selected->index]
                : static_cast<std::int64_t>(selected->index));
        auto cacheIt = layerIt->fullCellCache.find(row);
        if (cacheIt != layerIt->fullCellCache.end()) {
            propertyCell = cacheIt->second;
        } else {
            const auto fullCell = visor::datamine::DmBlockModelImporter::readInternalCellAt(
                layerIt->internalPath.toStdString(),
                row);
            if (fullCell) {
                propertyCell = *fullCell;
                layerIt->fullCellCache[row] = propertyCell;
            }
        }
    } catch (const std::exception &ex) {
        if (m_consoleLog) {
            m_consoleLog->append(QString(">> Unable to read selected block properties: %1")
                                     .arg(QString::fromLocal8Bit(ex.what())));
        }
    }
    ShowSelectedBlockProperties(selected->layerName, selected->index, propertyCell);
}

void MainWindow::ShowSelectedBlockProperties(
    const QString &layerName,
    std::size_t previewIndex,
    const visor::datamine::BlockCell &cell)
{
    if (!m_blockPropertiesOverlay || !m_blockPropertiesTable || !m_blockPropertiesTitle) {
        return;
    }

    auto addRow = [this](const QString &field, const QString &value) {
        const int row = m_blockPropertiesTable->rowCount();
        m_blockPropertiesTable->insertRow(row);
        m_blockPropertiesTable->setItem(row, 0, new QTableWidgetItem(field));
        m_blockPropertiesTable->setItem(row, 1, new QTableWidgetItem(value));
    };
    auto numberText = [](double value) {
        return QString::number(value, 'g', 15);
    };

    m_blockPropertiesTitle->setText(QString("%1 - block %2").arg(layerName).arg(static_cast<qulonglong>(previewIndex + 1)));
    m_blockPropertiesTable->setRowCount(0);

    addRow("IJK", QString::number(static_cast<qlonglong>(cell.ijk)));
    addRow("I", QString::number(cell.i));
    addRow("J", QString::number(cell.j));
    addRow("K", QString::number(cell.k));
    addRow("XC", numberText(cell.xc));
    addRow("YC", numberText(cell.yc));
    addRow("ZC", numberText(cell.zc));
    addRow("XINC", numberText(cell.xinc));
    addRow("YINC", numberText(cell.yinc));
    addRow("ZINC", numberText(cell.zinc));
    addRow("VOLUME", numberText(cell.volume));
    addRow("SUBCELL", cell.isSubcell ? "true" : "false");

    auto blockIt = m_blockModelLayers.constFind(layerName);
    if (blockIt != m_blockModelLayers.constEnd()) {
        for (const auto &field : blockIt->info.sourceHeader.fields) {
            const QString name = QString::fromStdString(field.name);
            if (field.isNumeric()) {
                auto valueIt = cell.numericAttributes.find(field.name);
                addRow(name, valueIt != cell.numericAttributes.end() ? numberText(valueIt->second) : QString());
            } else {
                auto valueIt = cell.alphaAttributes.find(field.name);
                addRow(name, valueIt != cell.alphaAttributes.end() ? QString::fromStdString(valueIt->second) : QString());
            }
        }
    } else {
        std::vector<std::string> numericNames;
        numericNames.reserve(cell.numericAttributes.size());
        for (const auto &entry : cell.numericAttributes) {
            numericNames.push_back(entry.first);
        }
        std::sort(numericNames.begin(), numericNames.end());
        for (const std::string &name : numericNames) {
            addRow(QString::fromStdString(name), numberText(cell.numericAttributes.at(name)));
        }

        std::vector<std::string> alphaNames;
        alphaNames.reserve(cell.alphaAttributes.size());
        for (const auto &entry : cell.alphaAttributes) {
            alphaNames.push_back(entry.first);
        }
        std::sort(alphaNames.begin(), alphaNames.end());
        for (const std::string &name : alphaNames) {
            addRow(QString::fromStdString(name), QString::fromStdString(cell.alphaAttributes.at(name)));
        }
    }

    m_blockPropertiesTable->resizeColumnToContents(0);
    PositionBlockPropertiesOverlay();
    m_blockPropertiesOverlay->show();
    m_blockPropertiesOverlay->raise();
}

void MainWindow::HideSelectedBlockProperties()
{
    if (m_blockPropertiesTable) {
        m_blockPropertiesTable->setRowCount(0);
    }
    if (m_blockPropertiesTitle) {
        m_blockPropertiesTitle->clear();
    }
    if (m_blockPropertiesOverlay) {
        m_blockPropertiesOverlay->hide();
    }
}

void MainWindow::PositionBlockPropertiesOverlay()
{
    if (!m_blockPropertiesOverlay || !m_vtkWidget) {
        return;
    }

    constexpr int margin = 12;
    const int viewportWidth = m_vtkWidget->width();
    const int viewportHeight = m_vtkWidget->height();
    if (viewportWidth <= margin * 2 || viewportHeight <= margin * 2) {
        return;
    }

    const int width = std::clamp(viewportWidth / 3, 300, 380);
    const int height = std::clamp(viewportHeight - margin * 2, 220, 540);
    const int x = std::max(margin, viewportWidth - width - margin);
    m_blockPropertiesOverlay->setGeometry(x, margin, width, height);
    m_blockPropertiesOverlay->raise();
}

void MainWindow::UpdateSelectionBand()
{
    if (!m_selectionBand) {
        return;
    }
    const QRect rect = QRect(m_selectStart, m_selectEnd).normalized();
    m_selectionBand->setGeometry(rect);

    m_crossingSelect = m_selectEnd.x() < m_selectStart.x();
    if (m_crossingSelect) {
        m_selectionBand->setStyleSheet(
            "background-color: rgba(0, 255, 120, 70);"
            "border: 2px solid #00c46a;");
    } else {
        m_selectionBand->setStyleSheet(
            "background-color: rgba(0, 120, 255, 70);"
            "border: 2px solid #2b7bff;");
    }
}

void MainWindow::OnLayerSelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);
    m_currentLayerIndex = current;

    if (!m_layerModel || !m_layerDateCombo) {
        return;
    }

    LayerNode *node = m_layerModel->nodeFromIndex(current);
    if (!node || !node->isGeometry()) {
        m_layerDateCombo->clear();
        m_layerDateCombo->setEnabled(false);
        return;
    }

    m_layerDateCombo->blockSignals(true);
    m_layerDateCombo->clear();
    const QMap<qint64, LayerVersion> &history = node->history();
    for (auto it = history.constBegin(); it != history.constEnd(); ++it) {
        const QDate date = LayerModel::DateFromKey(it.key());
        m_layerDateCombo->addItem(date.toString("yyyy-MM-dd"), QVariant::fromValue(it.key()));
        if (it.key() == node->activeDate()) {
            m_layerDateCombo->setCurrentIndex(m_layerDateCombo->count() - 1);
        }
    }
    m_layerDateCombo->setEnabled(m_layerDateCombo->count() > 0);
    m_layerDateCombo->blockSignals(false);

    if (node->activeDate() > 0) {
        UpdateLayerRender(node->name(), node->activeDate());
    }
}

void MainWindow::OnLayerDoubleClicked(const QModelIndex &index)
{
    if (!m_layerModel || !index.isValid()) {
        return;
    }
    LayerNode *node = m_layerModel->nodeFromIndex(index);
    if (!node) {
        return;
    }
    m_layersView->setCurrentIndex(index);
    if (node->isLegend()) {
        ModifyLegendFromLayerNode(node);
        return;
    }
    if (node->type() == LayerNode::Type::Folder) {
        m_layersView->setExpanded(index, !m_layersView->isExpanded(index));
    }
    if (!node->isVisible()) {
        m_layerModel->setData(index.siblingAtColumn(0), Qt::Checked, Qt::CheckStateRole);
    }
    if (node->isGeometry()) {
        m_layerModel->setActiveLayer(node);
        OnLayerSelectionChanged(index, QModelIndex());
    } else if (node->isBlockModel()) {
        ApplyBlockModelRender(node->name());
    }
}

void MainWindow::OnLayerContextMenu(const QPoint &pos)
{
    if (!m_layersView || !m_layerModel) {
        return;
    }

    const QModelIndex index = m_layersView->indexAt(pos);
    LayerNode *node = index.isValid() ? m_layerModel->nodeFromIndex(index) : nullptr;
    LayerNode *parent = m_layerModel->root();
    if (node) {
        if (node->type() == LayerNode::Type::Folder) {
            parent = node;
        } else {
            parent = node->parent() ? node->parent() : m_layerModel->root();
        }
    }

    QMenu menu(this);
    QAction *openCloseAction = nullptr;
    if (node && !node->isLegend()) {
        openCloseAction = menu.addAction(node->isVisible() ? "Close" : "Open");
    }
    QAction *closeChildrenAction = nullptr;
    if (node && !node->children().empty()) {
        closeChildrenAction = menu.addAction("Close Child Layers");
    }
    QAction *modifyLegendAction = nullptr;
    if (node && node->isLegend()) {
        modifyLegendAction = menu.addAction("Modify");
    }
    QAction *renameAction = nullptr;
    if (node && node != m_layerModel->root() && !node->isSystemNode() && !node->isLegend()) {
        renameAction = menu.addAction("Rename");
    }
    if (node) {
        menu.addSeparator();
    }
    QAction *addLayer = menu.addAction("Add Layer");
    QAction *addFolder = menu.addAction("Add Folder");
    QAction *focusLayer = menu.addAction("Focus");
    if (node && node->isLegend()) {
        addLayer->setEnabled(false);
        addFolder->setEnabled(false);
        focusLayer->setEnabled(false);
    }
    if (node && node->isSystemNode() &&
        (node->name().compare("Legends", Qt::CaseInsensitive) == 0 ||
         node->name().compare("Economic Models", Qt::CaseInsensitive) == 0)) {
        addLayer->setEnabled(false);
        addFolder->setEnabled(false);
    }
    QAction *propertiesAction = nullptr;
    QAction *deleteLayer = nullptr;
    QAction *deleteDate = nullptr;
    if (node && node != m_layerModel->root()) {
        if (node->isBlockModel()) {
            propertiesAction = menu.addAction("Properties");
        }
        if (!node->isSystemNode() && !node->isLegend()) {
            menu.addSeparator();
            deleteLayer = menu.addAction("Delete Layer");
        }
        if (node->isGeometry()) {
            deleteDate = menu.addAction("Delete Date");
        }
    }

    QAction *chosen = menu.exec(m_layersView->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    LayerNode *newNode = nullptr;
    if (chosen == openCloseAction) {
        if (!node) {
            return;
        }
        const QModelIndex idx = m_layerModel->indexFromNode(node, 0);
        if (idx.isValid()) {
            m_layerModel->setData(idx, node->isVisible() ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        }
    } else if (chosen == closeChildrenAction) {
        if (!node) {
            return;
        }
        std::function<void(LayerNode *)> closeChildren = [&](LayerNode *parentNode) {
            if (!parentNode) {
                return;
            }
            for (LayerNode *child : parentNode->children()) {
                if (!child || child->isLegend()) {
                    continue;
                }
                const QModelIndex childIndex = m_layerModel->indexFromNode(child, 0);
                if (childIndex.isValid()) {
                    m_layerModel->setData(childIndex, Qt::Unchecked, Qt::CheckStateRole);
                }
                closeChildren(child);
            }
        };
        closeChildren(node);
    } else if (chosen == modifyLegendAction) {
        ModifyLegendFromLayerNode(node);
    } else if (chosen == renameAction) {
        if (!node) {
            return;
        }
        bool ok = false;
        const QString requested = QInputDialog::getText(
            this,
            "Rename Layer",
            "Name",
            QLineEdit::Normal,
            node->name(),
            &ok).trimmed();
        if (!ok || requested.isEmpty() || requested == node->name()) {
            return;
        }
        const QString oldName = node->name();
        const QString newName = UniqueChildName(node->parent() ? node->parent() : m_layerModel->root(), requested);
        const QModelIndex idx = m_layerModel->indexFromNode(node, 0);
        if (idx.isValid()) {
            m_layerModel->setData(idx, newName, Qt::EditRole);
        } else {
            node->setName(newName);
        }
        if (node->isGeometry()) {
            if (m_layerRenders.contains(oldName)) {
                m_layerRenders.insert(newName, m_layerRenders.take(oldName));
            }
            for (SceneItem &item : m_sceneItems) {
                if (item.layerName.compare(oldName, Qt::CaseInsensitive) == 0) {
                    item.layerName = newName;
                }
            }
        } else if (node->isBlockModel()) {
            if (m_blockModelLayers.contains(oldName)) {
                m_blockModelLayers.insert(newName, m_blockModelLayers.take(oldName));
            }
            if (m_blockModelSettings.contains(oldName)) {
                m_blockModelSettings.insert(newName, m_blockModelSettings.take(oldName));
            }
            for (BlockModelLegend &legend : m_blockModelLegends) {
                if (legend.layerName.compare(oldName, Qt::CaseInsensitive) == 0) {
                    legend.layerName = newName;
                }
            }
            for (auto it = m_economicModels.begin(); it != m_economicModels.end();) {
                if (it->blockModelLayerName.compare(oldName, Qt::CaseInsensitive) == 0) {
                    EconomicModelDefinition definition = it.value();
                    const QString modelName = definition.name;
                    definition.blockModelLayerName = newName;
                    it = m_economicModels.erase(it);
                    m_economicModels.insert(EconomicModelKey(newName, modelName), definition);
                } else {
                    ++it;
                }
            }
        } else if (node->isEconomicModel()) {
            LayerNode *owner = FindLegendOwnerBlockModel(node);
            if (owner) {
                const QString oldKey = EconomicModelKey(owner->name(), oldName);
                if (m_economicModels.contains(oldKey)) {
                    EconomicModelDefinition definition = m_economicModels.take(oldKey);
                    definition.name = newName;
                    m_economicModels.insert(EconomicModelKey(owner->name(), newName), definition);
                }
            }
        }
    } else if (chosen == addLayer) {
        NewGeometryDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        const QString rawName = dlg.name();
        const QDate date = dlg.date();
        if (rawName.isEmpty() || !date.isValid()) {
            return;
        }
        const QString name = UniqueChildName(parent, rawName);
        newNode = m_layerModel->addGeometryLayer(name, parent);
        if (newNode) {
            const qint64 key = LayerModel::DateKey(date);
            m_layerModel->addVersion(newNode, key);
            LayerRenderData &render = m_layerRenders[name];
            if (!render.actor) {
                vtkNew<vtkPolyDataMapper> mapper;
                vtkNew<vtkActor> actor;
                actor->SetMapper(mapper);
                actor->GetProperty()->SetColor(0.85, 0.9, 0.95);
                actor->GetProperty()->SetLineWidth(1.2);
                actor->GetProperty()->SetRepresentationToWireframe();
                m_renderer->AddActor(actor);
                render.mapper = mapper;
                render.actor = actor;

                SceneItem item;
                item.actor = actor;
                item.data = nullptr;
                item.highlightActor = nullptr;
                item.layerName = name;
                m_sceneItems.push_back(item);
                m_actorIndex[actor] = m_sceneItems.size() - 1;
            }
            render.versions.insert(key, vtkSmartPointer<vtkPolyData>::New());
            UpdateLayerRender(name, key);
        }
    } else if (chosen == addFolder) {
        bool ok = false;
        QString name = QInputDialog::getText(
            this,
            "New Folder",
            "Name",
            QLineEdit::Normal,
            "New Folder",
            &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        name = UniqueChildName(parent, name.trimmed());
        newNode = m_layerModel->addFolder(name, parent);
    } else if (chosen == focusLayer) {
        if (!node) {
            return;
        }
        if (node->isGeometry()) {
            auto it = m_layerRenders.find(node->name());
            if (it != m_layerRenders.end() && it->actor) {
                m_renderer->ResetCamera(it->actor->GetBounds());
                if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
                    camera->ParallelProjectionOn();
                }
                m_renderer->ResetCameraClippingRange();
                m_renderWindow->Render();
            }
        } else if (node->isBlockModel()) {
            auto bit = m_blockModelLayers.find(node->name());
            if (bit != m_blockModelLayers.end()) {
                double bounds[6];
                bool hasBounds = false;
                if (bit->actor) {
                    bit->actor->GetBounds(bounds);
                    hasBounds = bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5];
                }
                if (!hasBounds) {
                    hasBounds = BlockModelBounds(bit->info, bounds);
                }
                if (hasBounds) {
                    m_renderer->ResetCamera(bounds);
                    if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
                        camera->ParallelProjectionOn();
                    }
                    m_renderer->ResetCameraClippingRange();
                    m_renderWindow->Render();
                }
            }
        } else {
            bool hasBounds = false;
            double bounds[6] = {0, 0, 0, 0, 0, 0};
            for (LayerNode *child : node->children()) {
                if (!child || (!child->isGeometry() && !child->isBlockModel())) {
                    continue;
                }
                double b[6];
                bool childHasBounds = false;
                if (child->isGeometry()) {
                    auto it = m_layerRenders.find(child->name());
                    if (it != m_layerRenders.end() && it->actor) {
                        it->actor->GetBounds(b);
                        childHasBounds = b[0] <= b[1] && b[2] <= b[3] && b[4] <= b[5];
                    }
                } else {
                    auto bit = m_blockModelLayers.find(child->name());
                    if (bit != m_blockModelLayers.end()) {
                        if (bit->actor) {
                            bit->actor->GetBounds(b);
                            childHasBounds = b[0] <= b[1] && b[2] <= b[3] && b[4] <= b[5];
                        }
                        if (!childHasBounds) {
                            childHasBounds = BlockModelBounds(bit->info, b);
                        }
                    }
                }
                if (!childHasBounds) {
                    continue;
                }
                if (!hasBounds) {
                    std::copy(b, b + 6, bounds);
                    hasBounds = true;
                } else {
                    bounds[0] = std::min(bounds[0], b[0]);
                    bounds[1] = std::max(bounds[1], b[1]);
                    bounds[2] = std::min(bounds[2], b[2]);
                    bounds[3] = std::max(bounds[3], b[3]);
                    bounds[4] = std::min(bounds[4], b[4]);
                    bounds[5] = std::max(bounds[5], b[5]);
                }
            }
            if (hasBounds) {
                m_renderer->ResetCamera(bounds);
                if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
                    camera->ParallelProjectionOn();
                }
                m_renderer->ResetCameraClippingRange();
                m_renderWindow->Render();
            }
        }
    } else if (chosen == propertiesAction) {
        if (node && node->isBlockModel()) {
            ShowBlockModelProperties(node);
        }
    } else if (chosen == deleteLayer) {
        if (!node || node == m_layerModel->root()) {
            return;
        }

        const QString message = node->isGeometry()
            ? QString("Delete layer '%1' and all its dates?").arg(node->name())
            : (node->isBlockModel()
                    ? QString("Delete block model layer '%1'?").arg(node->name())
                    : (node->isEconomicModel()
                            ? QString("Delete economic model '%1'?").arg(node->name())
                            : QString("Delete folder '%1' and all child layers?").arg(node->name())));
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            "Delete Layer",
            message,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }

        QStringList geometryNames;
        QStringList blockModelNames;
        std::function<void(LayerNode *)> collectGeometryNames = [&](LayerNode *target) {
            if (!target) {
                return;
            }
            if (target->isGeometry()) {
                geometryNames.append(target->name());
            } else if (target->isBlockModel()) {
                blockModelNames.append(target->name());
            }
            for (LayerNode *child : target->children()) {
                collectGeometryNames(child);
            }
        };
        collectGeometryNames(node);

        ClearSelection();
        if (!m_layerModel->removeNode(node)) {
            return;
        }

        for (const QString &layerName : geometryNames) {
            for (auto &item : m_sceneItems) {
                if (item.layerName.compare(layerName, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                if (item.highlightActor) {
                    m_renderer->RemoveActor(item.highlightActor);
                    item.highlightActor = nullptr;
                }
                if (item.actor) {
                    m_renderer->RemoveActor(item.actor);
                }
            }
            m_sceneItems.erase(
                std::remove_if(
                    m_sceneItems.begin(),
                    m_sceneItems.end(),
                    [&layerName](const SceneItem &item) {
                        return item.layerName.compare(layerName, Qt::CaseInsensitive) == 0;
                    }),
                m_sceneItems.end());
            m_layerRenders.remove(layerName);
        }
        for (const QString &layerName : blockModelNames) {
            auto bit = m_blockModelLayers.find(layerName);
            if (bit != m_blockModelLayers.end() && bit->actor) {
                m_renderer->RemoveActor(bit->actor);
            }
            if (bit != m_blockModelLayers.end() && bit->sectionActor) {
                m_renderer->RemoveActor(bit->sectionActor);
            }
            m_blockModelLayers.remove(layerName);
            m_blockModelSettings.remove(layerName);
        }

        m_actorIndex.clear();
        for (std::size_t i = 0; i < m_sceneItems.size(); ++i) {
            if (m_sceneItems[i].actor) {
                m_actorIndex[m_sceneItems[i].actor] = i;
            }
        }

        if (m_layersView->selectionModel()) {
            const QModelIndex current = m_layersView->currentIndex();
            OnLayerSelectionChanged(current, QModelIndex());
        } else if (m_layerDateCombo) {
            m_layerDateCombo->clear();
            m_layerDateCombo->setEnabled(false);
        }
        m_currentLayerIndex = QModelIndex();
        if (m_renderWindow) {
            m_renderWindow->Render();
        }
        if (m_consoleLog) {
            m_consoleLog->append(">> Layer deleted.");
        }
    } else if (chosen == deleteDate) {
        if (!node || !node->isGeometry()) {
            return;
        }
        QMap<qint64, LayerVersion> &history = node->history();
        if (history.isEmpty()) {
            return;
        }

        qint64 activeKey = node->activeDate();
        if (!history.contains(activeKey)) {
            activeKey = history.lastKey();
        }

        if (history.size() <= 1) {
            QMessageBox::information(
                this,
                "Delete Date",
                "The layer has only one date. Use Delete Layer to remove it.");
            return;
        }

        const QString dateText = LayerModel::DateFromKey(activeKey).toString("yyyy-MM-dd");
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            "Delete Date",
            QString("Delete date '%1' from layer '%2'?").arg(dateText, node->name()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }

        history.remove(activeKey);
        auto renderIt = m_layerRenders.find(node->name());
        if (renderIt != m_layerRenders.end()) {
            renderIt.value().versions.remove(activeKey);
        }

        const qint64 newActive = history.lastKey();
        m_layerModel->setActiveDate(node, newActive);
        UpdateLayerRender(node->name(), newActive);
        OnLayerSelectionChanged(index, index);
        if (m_consoleLog) {
            m_consoleLog->append(QString(">> Date deleted from layer: %1").arg(node->name()));
        }
    }

    if (newNode) {
        QModelIndex idx = m_layerModel->indexFromNode(newNode, 0);
        if (idx.isValid()) {
            m_layersView->expand(m_layerModel->indexFromNode(parent, 0));
            m_layersView->setCurrentIndex(idx);
            m_layersView->scrollTo(idx);
            m_layersView->edit(idx);
        }
    }
}

void MainWindow::OnLayerVisibilityChanged(LayerNode *node, bool visible)
{
    if (!node || (!node->isGeometry() && !node->isBlockModel())) {
        return;
    }

    if (node->isBlockModel()) {
        auto bit = m_blockModelLayers.find(node->name());
        if (bit != m_blockModelLayers.end()) {
            if (visible && (bit->renderDirty || (!bit->actor && !bit->sectionActor))) {
                ApplyBlockModelRender(node->name());
            }
            const BlockModelDisplaySettings settings = m_blockModelSettings.value(node->name());
            if (bit->actor) {
                const bool showActor = visible &&
                    settings.blocksEnabled &&
                    settings.renderMode == BlockModelRenderMode::Solid3D;
                bit->actor->SetVisibility(showActor ? 1 : 0);
                if (!visible && bit->selectionActor) {
                    m_renderer->RemoveActor(bit->selectionActor);
                    bit->selectionActor = nullptr;
                    HideSelectedBlockProperties();
                }
                if (showActor) {
                    double bounds[6];
                    bit->actor->GetBounds(bounds);
                    if (bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5]) {
                        m_renderer->ResetCamera(bounds);
                        if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
                            camera->ParallelProjectionOn();
                        }
                        m_renderer->ResetCameraClippingRange();
                    }
                }
                if (m_renderWindow) {
                    m_renderWindow->Render();
                }
            }
            if (bit->sectionActor) {
                const bool showSection = visible &&
                    settings.blocksEnabled &&
                    settings.renderMode == BlockModelRenderMode::Section2D;
                bit->sectionActor->SetVisibility(showSection ? 1 : 0);
                if (m_renderWindow) {
                    m_renderWindow->Render();
                }
            }
        }
        return;
    }

    bool changed = false;
    for (auto &item : m_sceneItems) {
        if (item.layerName.compare(node->name(), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (item.actor) {
            item.actor->SetVisibility(visible ? 1 : 0);
            changed = true;
        }
        if (item.highlightActor) {
            item.highlightActor->SetVisibility(visible ? 1 : 0);
            changed = true;
        }
    }

    auto it = m_layerRenders.find(node->name());
    if (it != m_layerRenders.end() && it.value().actor) {
        it.value().actor->SetVisibility(visible ? 1 : 0);
        changed = true;
    } else {
        for (auto rit = m_layerRenders.begin(); rit != m_layerRenders.end(); ++rit) {
            if (rit.key().compare(node->name(), Qt::CaseInsensitive) == 0 && rit.value().actor) {
                rit.value().actor->SetVisibility(visible ? 1 : 0);
                changed = true;
            }
        }
    }

    if (changed && m_renderWindow) {
        m_renderWindow->Render();
    }
}

void MainWindow::OnLayerDateChanged(int index)
{
    if (!m_layerModel || index < 0) {
        return;
    }
    if (!m_currentLayerIndex.isValid()) {
        return;
    }
    QVariant data = m_layerDateCombo ? m_layerDateCombo->itemData(index) : QVariant{};
    if (!data.isValid()) {
        return;
    }
    const qint64 key = data.toLongLong();
    if (m_layerModel->setActiveDate(m_currentLayerIndex, key)) {
        LayerNode *node = m_layerModel->nodeFromIndex(m_currentLayerIndex);
        if (node) {
            UpdateLayerRender(node->name(), key);
        }
    }
}

void MainWindow::OnAddDateClicked()
{
    if (!m_layerModel) {
        return;
    }

    const QList<LayerNode *> layers = m_layerModel->geometryLayers();
    if (layers.isEmpty()) {
        QMessageBox::information(this, "Add Date", "No geometry layers available.");
        return;
    }

    AddDateDialog dialog(layers, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QList<AddDateDialog::Selection> selections = dialog.selections();
    if (selections.isEmpty()) {
        QMessageBox::warning(this, "Add Date", "Select at least one layer.");
        return;
    }

    bool updatedCurrent = false;
    for (const auto &sel : selections) {
        if (!sel.date.isValid()) {
            continue;
        }

        LayerNode *layerNode = m_layerModel->findGeometryLayerByName(sel.layerName);
        if (!layerNode) {
            continue;
        }

        const qint64 newKey = LayerModel::DateKey(sel.date);
        if (newKey == 0) {
            continue;
        }
        if (layerNode->history().contains(newKey)) {
            continue;
        }

        vtkSmartPointer<vtkPolyData> newPoly = vtkSmartPointer<vtkPolyData>::New();
        if (sel.copyExisting && sel.sourceKey > 0) {
            auto renderIt = m_layerRenders.find(sel.layerName);
            if (renderIt != m_layerRenders.end()) {
                auto srcIt = renderIt->versions.find(sel.sourceKey);
                if (srcIt != renderIt->versions.end() && srcIt.value()) {
                    newPoly->DeepCopy(srcIt.value());
                }
            }
        }

        LayerRenderData &render = m_layerRenders[sel.layerName];
        if (!render.actor) {
            vtkNew<vtkPolyDataMapper> mapper;
            vtkNew<vtkActor> actor;
            actor->SetMapper(mapper);
            actor->GetProperty()->SetColor(0.85, 0.9, 0.95);
            actor->GetProperty()->SetLineWidth(1.2);
            actor->GetProperty()->SetRepresentationToWireframe();
            m_renderer->AddActor(actor);

            render.mapper = mapper;
            render.actor = actor;

            SceneItem item;
            item.actor = actor;
            item.data = nullptr;
            item.highlightActor = nullptr;
            item.layerName = sel.layerName;
            m_sceneItems.push_back(item);
            m_actorIndex[actor] = m_sceneItems.size() - 1;
        }

        render.versions.insert(newKey, newPoly);
        m_layerModel->addVersion(layerNode, newKey);
        OnLayerVisibilityChanged(layerNode, layerNode->isVisible());

        if (m_currentLayerIndex.isValid()) {
            LayerNode *currentNode = m_layerModel->nodeFromIndex(m_currentLayerIndex);
            if (currentNode && currentNode->name() == sel.layerName) {
                UpdateLayerRender(sel.layerName, layerNode->activeDate());
                updatedCurrent = true;
            }
        }
    }

    if (updatedCurrent) {
        OnLayerSelectionChanged(m_currentLayerIndex, QModelIndex());
    }

    if (m_consoleLog) {
        m_consoleLog->append(">> Added layer date(s)");
    }
}

bool MainWindow::GenerateEconomicBlockModel(EconomicModelDefinition &definition, QString *errorMessage)
{
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    const QString blockModelName = definition.blockModelLayerName;
    auto sourceIt = m_blockModelLayers.constFind(blockModelName);
    if (sourceIt == m_blockModelLayers.constEnd()) {
        return fail("The selected block model is no longer available.");
    }
    const BlockModelLayerData sourceData = sourceIt.value();
    if (sourceData.internalPath.isEmpty() || !QFileInfo::exists(sourceData.internalPath)) {
        return fail("The selected block model data file is not available.");
    }

    QStringList products;
    QString densityField;
    QString rockTypeField;
    QStringList selectedFields;
    for (auto it = definition.fieldRoles.constBegin(); it != definition.fieldRoles.constEnd(); ++it) {
        const QString role = it.value();
        if (role.isEmpty() || role == "None") {
            continue;
        }
        selectedFields << it.key();
        if (role == "Product") {
            products << it.key();
        } else if (role == "Density" && densityField.isEmpty()) {
            densityField = it.key();
        } else if (role == "Rock Type" && rockTypeField.isEmpty()) {
            rockTypeField = it.key();
        }
    }
    selectedFields.removeDuplicates();
    selectedFields.sort(Qt::CaseInsensitive);

    if (definition.profitModel) {
        if (definition.profitField.isEmpty()) {
            return fail("Select a profit field before generating the economic model.");
        }
        if (!selectedFields.contains(definition.profitField, Qt::CaseInsensitive)) {
            selectedFields << definition.profitField;
        }
    } else if (products.isEmpty() || densityField.isEmpty() || rockTypeField.isEmpty()) {
        return fail("Select one Density field, one Rock Type field, and at least one Product field before generating the economic model.");
    }

    QHash<QString, visor::datamine::DmField> sourceFields;
    for (const auto &field : sourceData.info.sourceHeader.fields) {
        sourceFields.insert(QString::fromStdString(visor::datamine::normalizeFieldName(field.name)), field);
    }

    struct FieldAggregate {
        bool numeric = true;
        bool dominant = false;
        double weightedSum = 0.0;
        double weight = 0.0;
        QHash<QString, double> valueWeights;
    };
    struct ParentAggregate {
        visor::datamine::BlockCell cell;
        double volume = 0.0;
        QHash<QString, FieldAggregate> fields;
    };

    QHash<QString, bool> fieldIsNumeric;
    QHash<QString, bool> fieldUsesDominantValue;
    for (const QString &field : selectedFields) {
        const QString normalized = QString::fromStdString(visor::datamine::normalizeFieldName(field.toStdString()));
        const auto srcIt = sourceFields.constFind(normalized);
        const bool numeric = srcIt == sourceFields.constEnd() || srcIt->isNumeric();
        const QString role = definition.fieldRoles.value(field);
        fieldIsNumeric.insert(field, numeric);
        fieldUsesDominantValue.insert(field, role == "Rock Type" || role == "Category" || role == "Slope Region");
    }

    std::unordered_map<std::int64_t, ParentAggregate> parents;
    QProgressDialog progress("Generating economic model...", "Cancel", 0, 100, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(500);

    const auto &prototype = sourceData.info.prototype;
    const double parentVolume = prototype.parentX * prototype.parentY * prototype.parentZ;
    auto standardCellNumericValue = [](const visor::datamine::BlockCell &cell, const QString &field, bool *hasValue) {
        const QString name = field.trimmed().toUpper();
        if (hasValue) {
            *hasValue = true;
        }
        if (name == "IJK") return static_cast<double>(cell.ijk);
        if (name == "I") return static_cast<double>(cell.i);
        if (name == "J") return static_cast<double>(cell.j);
        if (name == "K") return static_cast<double>(cell.k);
        if (name == "XC") return cell.xc;
        if (name == "YC") return cell.yc;
        if (name == "ZC") return cell.zc;
        if (name == "XINC") return cell.xinc;
        if (name == "YINC") return cell.yinc;
        if (name == "ZINC") return cell.zinc;
        if (name == "VOLUME") return cell.volume;
        if (hasValue) {
            *hasValue = false;
        }
        return 0.0;
    };
    std::int64_t processed = 0;
    try {
        visor::datamine::DmBlockModelImporter::forEachInternalCellValue(
            sourceData.internalPath.toStdString(),
            std::string(),
            [&](const visor::datamine::BlockCell &cell, double, bool, std::int64_t row, std::int64_t) {
                (void)row;
                const double weight = cell.volume > 0.0 ? cell.volume : parentVolume;
                auto [it, inserted] = parents.try_emplace(cell.ijk);
                ParentAggregate &parent = it->second;
                if (inserted) {
                    const auto indices = visor::datamine::ijkToParentIndices(cell.ijk, prototype.nx, prototype.ny, prototype.nz);
                    parent.cell.ijk = cell.ijk;
                    parent.cell.i = indices[0];
                    parent.cell.j = indices[1];
                    parent.cell.k = indices[2];
                    parent.cell.xinc = prototype.parentX;
                    parent.cell.yinc = prototype.parentY;
                    parent.cell.zinc = prototype.parentZ;
                    parent.cell.xc = prototype.xOrigin + (static_cast<double>(indices[0]) + 0.5) * prototype.parentX;
                    parent.cell.yc = prototype.yOrigin + (static_cast<double>(indices[1]) + 0.5) * prototype.parentY;
                    parent.cell.zc = prototype.zOrigin + (static_cast<double>(indices[2]) + 0.5) * prototype.parentZ;
                    parent.cell.volume = parentVolume;
                    parent.cell.isSubcell = false;
                }
                parent.volume += weight;
                for (const QString &field : selectedFields) {
                    const QString normalized = QString::fromStdString(visor::datamine::normalizeFieldName(field.toStdString()));
                    FieldAggregate aggregate = parent.fields.value(field);
                    aggregate.numeric = fieldIsNumeric.value(field, true);
                    aggregate.dominant = fieldUsesDominantValue.value(field, false);
                    if (aggregate.numeric) {
                        const auto valueIt = cell.numericAttributes.find(normalized.toStdString());
                        bool hasStandardValue = false;
                        const double standardValue = standardCellNumericValue(cell, normalized, &hasStandardValue);
                        const double value = valueIt == cell.numericAttributes.end()
                            ? (hasStandardValue ? standardValue : 0.0)
                            : valueIt->second;
                        if (aggregate.dominant) {
                            const QString key = QString::number(value, 'g', 15);
                            aggregate.valueWeights.insert(key, aggregate.valueWeights.value(key) + weight);
                        } else {
                            aggregate.weightedSum += value * weight;
                            aggregate.weight += weight;
                        }
                    } else {
                        const auto valueIt = cell.alphaAttributes.find(normalized.toStdString());
                        const QString value = valueIt == cell.alphaAttributes.end() ? QString() : QString::fromStdString(valueIt->second);
                        aggregate.valueWeights.insert(value, aggregate.valueWeights.value(value) + weight);
                    }
                    parent.fields.insert(field, aggregate);
                }
                ++processed;
                if (processed % 25000 == 0) {
                    const int pct = sourceData.info.cellCount > 0 ? static_cast<int>((processed * 60) / sourceData.info.cellCount) : 0;
                    progress.setValue(std::clamp(pct, 0, 60));
                    QApplication::processEvents();
                    return !progress.wasCanceled();
                }
                return true;
            },
            std::nullopt,
            true);
    } catch (const std::exception &ex) {
        return fail(QString("Unable to aggregate parent cells: %1").arg(ex.what()));
    }
    if (progress.wasCanceled()) {
        return fail("Economic model generation was cancelled.");
    }

    auto dominantValue = [](const QHash<QString, double> &weights) {
        QString best;
        double bestWeight = -1.0;
        for (auto it = weights.constBegin(); it != weights.constEnd(); ++it) {
            if (it.value() > bestWeight) {
                best = it.key();
                bestWeight = it.value();
            }
        }
        return best;
    };
    auto fieldValueString = [&](const ParentAggregate &parent, const QString &field) {
        const FieldAggregate aggregate = parent.fields.value(field);
        if (!aggregate.valueWeights.isEmpty()) {
            return dominantValue(aggregate.valueWeights);
        }
        const double value = aggregate.weight > 0.0 ? aggregate.weightedSum / aggregate.weight : 0.0;
        return QString::number(value, 'g', 15);
    };
    auto fieldValueNumeric = [&](const ParentAggregate &parent, const QString &field) {
        const FieldAggregate aggregate = parent.fields.value(field);
        if (!aggregate.valueWeights.isEmpty()) {
            return dominantValue(aggregate.valueWeights).toDouble();
        }
        return aggregate.weight > 0.0 ? aggregate.weightedSum / aggregate.weight : 0.0;
    };
    auto rockSettingsFor = [&](const QString &rockType) {
        for (const auto &settings : definition.rockTypeSettings) {
            if (settings.rockType.compare(rockType, Qt::CaseInsensitive) == 0) {
                return settings;
            }
        }
        return definition.rockTypeSettings.isEmpty()
            ? EconomicModelDefinition::RockTypeSettings{}
            : definition.rockTypeSettings.first();
    };
    auto destinationProductValue = [&](const EconomicModelDefinition::Destination &destination, int productIndex, int offset,
                                       const QHash<QString, double> &context, bool *ok) {
        const int index = productIndex * 3 + offset;
        if (index < 0 || index >= destination.productValues.size()) {
            if (ok) {
                *ok = false;
            }
            return 0.0;
        }
        double value = 0.0;
        const bool parsed = EvaluateEconomicDestinationValue(destination.productValues[index], context, definition.variables, &value);
        if (ok) {
            *ok = parsed;
        }
        return parsed ? value : 0.0;
    };

    std::vector<visor::datamine::DmField> outputFields;
    QSet<QString> outputFieldNames;
    auto addField = [&](const QString &name, bool numeric) {
        const QString normalized = QString::fromStdString(visor::datamine::normalizeFieldName(name.toStdString()));
        if (outputFieldNames.contains(normalized)) {
            return;
        }
        outputFieldNames.insert(normalized);
        visor::datamine::DmField field;
        field.name = normalized.toStdString();
        field.type = numeric ? visor::datamine::DmFieldType::Numeric : visor::datamine::DmFieldType::Alpha;
        outputFields.push_back(field);
    };
    for (const QString &field : selectedFields) {
        addField(field, fieldIsNumeric.value(field, true));
    }
    addField("DESTINATION", false);
    addField("REVENUE", true);
    addField("MINING_COST", true);
    addField("PROCESSING_COST", true);
    addField("VALUE", true);

    std::vector<visor::datamine::BlockCell> outputCells;
    outputCells.reserve(parents.size());
    std::vector<std::int64_t> sortedIjks;
    sortedIjks.reserve(parents.size());
    for (const auto &entry : parents) {
        sortedIjks.push_back(entry.first);
    }
    std::sort(sortedIjks.begin(), sortedIjks.end());

    std::int64_t calculated = 0;
    for (std::int64_t ijk : sortedIjks) {
        const ParentAggregate &parent = parents.at(ijk);
        visor::datamine::BlockCell outCell = parent.cell;
        QHash<QString, double> context;
        for (const QString &field : selectedFields) {
            const QString normalized = QString::fromStdString(visor::datamine::normalizeFieldName(field.toStdString()));
            if (fieldIsNumeric.value(field, true)) {
                const double value = fieldValueNumeric(parent, field);
                outCell.numericAttributes[normalized.toStdString()] = value;
                context.insert(field, value);
                context.insert(normalized, value);
            } else {
                outCell.alphaAttributes[normalized.toStdString()] = fieldValueString(parent, field).toStdString();
            }
        }

        QString bestDestination = definition.profitModel ? "Profit Model" : "Waste";
        double bestRevenue = 0.0;
        double bestMiningCost = 0.0;
        double bestProcessingCost = 0.0;
        double bestValue = -std::numeric_limits<double>::infinity();
        if (definition.profitModel) {
            bestValue = fieldValueNumeric(parent, definition.profitField);
        } else {
            const QString rockType = fieldValueString(parent, rockTypeField);
            const auto settings = rockSettingsFor(rockType);
            const double density = std::max(0.0, fieldValueNumeric(parent, densityField));
            const double baseTonnes = std::max(0.0, parent.cell.volume) * density;
            const double miningRecovery = settings.miningRecovery / 100.0;
            const double dilution = std::max(0.0, settings.dilution / 100.0);
            const double revenueTonnes = baseTonnes * miningRecovery;
            const double costTonnes = baseTonnes * miningRecovery / (1.0 + dilution);

            for (const auto &destination : settings.destinations) {
                if (!destination.enabled && destination.name != "Waste") {
                    continue;
                }
                double revenue = 0.0;
                double additionalProcessing = 0.0;
                bool destinationOk = true;
                for (int productIndex = 0; productIndex < products.size(); ++productIndex) {
                    bool priceOk = false;
                    bool recoveryOk = false;
                    bool addCostOk = false;
                    const double price = destinationProductValue(destination, productIndex, 0, context, &priceOk);
                    const double recovery = destinationProductValue(destination, productIndex, 1, context, &recoveryOk) / 100.0;
                    const double addCost = destinationProductValue(destination, productIndex, 2, context, &addCostOk);
                    destinationOk = destinationOk && priceOk && recoveryOk && addCostOk;
                    const double grade = fieldValueNumeric(parent, products[productIndex]);
                    const bool productIsBlockMass = definition.fieldUnits.value(products[productIndex]).startsWith("Mass", Qt::CaseInsensitive);
                    const double containedProduct = productIsBlockMass ? grade : grade * revenueTonnes;
                    const double recoveredProduct = containedProduct * recovery;
                    revenue += recoveredProduct * price;
                    additionalProcessing += recoveredProduct * addCost;
                }
                if (!destinationOk) {
                    return fail(QString("Destination values must be numbers or existing variable names. Check destination '%1'.").arg(destination.name));
                }
                const double miningCost = settings.miningCost * costTonnes;
                const double processingCost = destination.processingCost * costTonnes + additionalProcessing;
                const double value = revenue - miningCost - processingCost;
                if (value > bestValue) {
                    bestValue = value;
                    bestDestination = destination.name;
                    bestRevenue = revenue;
                    bestMiningCost = miningCost;
                    bestProcessingCost = processingCost;
                }
            }
            if (!std::isfinite(bestValue)) {
                bestValue = 0.0;
            }
        }

        outCell.alphaAttributes["DESTINATION"] = bestDestination.toStdString();
        outCell.numericAttributes["REVENUE"] = bestRevenue;
        outCell.numericAttributes["MINING_COST"] = bestMiningCost;
        outCell.numericAttributes["PROCESSING_COST"] = bestProcessingCost;
        outCell.numericAttributes["VALUE"] = bestValue;
        outputCells.push_back(std::move(outCell));

        ++calculated;
        if (calculated % 25000 == 0) {
            const int pct = 60 + static_cast<int>((calculated * 30) / std::max<std::int64_t>(1, static_cast<std::int64_t>(sortedIjks.size())));
            progress.setValue(std::clamp(pct, 60, 90));
            QApplication::processEvents();
            if (progress.wasCanceled()) {
                return fail("Economic model generation was cancelled.");
            }
        }
    }

    const QString cacheDir = EconomicModelCacheDir(m_projectPath);
    if (!QDir().mkpath(cacheDir)) {
        return fail("Unable to create the economic model cache folder.");
    }
    const QString baseName = QString("%1_%2").arg(SanitizeName(blockModelName), SanitizeName(definition.name));
    QString outputPath = QDir(cacheDir).filePath(baseName + ".gvbm");
    int suffix = 1;
    while (QFileInfo::exists(outputPath)
           && QFileInfo(outputPath).absoluteFilePath() != QFileInfo(definition.generatedInternalPath).absoluteFilePath()) {
        outputPath = QDir(cacheDir).filePath(QString("%1_%2.gvbm").arg(baseName).arg(suffix++));
    }

    try {
        progress.setValue(92);
        QApplication::processEvents();
        const auto info = visor::datamine::DmBlockModelImporter::writeInternalFile(
            outputPath.toStdString(),
            prototype,
            QString("Economic model: %1").arg(definition.name).toStdString(),
            QFileInfo(outputPath).fileName().toStdString(),
            outputFields,
            outputCells);
        definition.generatedInternalPath = outputPath;
        definition.generatedStoredRelativePath.clear();
        definition.generatedCellCount = static_cast<qint64>(info.cellCount);
    } catch (const std::exception &ex) {
        return fail(QString("Unable to write the economic block model: %1").arg(ex.what()));
    }

    progress.setValue(100);
    return true;
}

void MainWindow::OnCreateEconomicModel()
{
    if (!m_layerModel || !m_layersView) {
        return;
    }

    LayerNode *selectedNode = m_layerModel->nodeFromIndex(m_layersView->currentIndex());
    QString initialBlockModel;
    for (LayerNode *node = selectedNode; node != nullptr; node = node->parent()) {
        if (node->isBlockModel()) {
            initialBlockModel = node->name();
            break;
        }
    }

    const QList<LayerNode *> blockModelNodes = m_layerModel->blockModelLayers();
    if (blockModelNodes.isEmpty()) {
        QMessageBox::information(this, "Economic Model", "Load a block model layer first.");
        return;
    }

    QStringList blockModelNames;
    QHash<QString, QStringList> fieldsByBlockModel;
    QHash<QString, QString> internalPathsByBlockModel;
    for (LayerNode *node : blockModelNodes) {
        if (!node || (node->parent() && node->parent()->isEconomicModel())) {
            continue;
        }
        blockModelNames << node->name();
        auto blockIt = m_blockModelLayers.constFind(node->name());
        if (blockIt != m_blockModelLayers.constEnd()) {
            fieldsByBlockModel.insert(node->name(), BlockModelFieldNames(blockIt.value().info));
            internalPathsByBlockModel.insert(node->name(), blockIt.value().internalPath);
        }
    }
    blockModelNames.sort(Qt::CaseInsensitive);
    if (initialBlockModel.isEmpty() && !blockModelNames.isEmpty()) {
        initialBlockModel = blockModelNames.first();
    }

    CreateEconomicModelDialog dialog(blockModelNames, fieldsByBlockModel, internalPathsByBlockModel, initialBlockModel, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString blockModelName = dialog.blockModelName();
    LayerNode *blockModelNode = m_layerModel->findBlockModelLayerByName(blockModelName);
    if (!blockModelNode) {
        QMessageBox::warning(this, "Economic Model", "The selected block model is no longer available.");
        return;
    }

    QString name = dialog.economicModelName();
    LayerNode *economicFolder = EnsureBlockModelSystemFolder(blockModelNode, "Economic Models");
    name = UniqueChildName(economicFolder ? economicFolder : blockModelNode, name);
    EconomicModelDefinition definition;
    definition.name = name;
    definition.blockModelLayerName = blockModelName;
    definition.fieldRoles = dialog.fieldRoles();
    definition.fieldUnits = dialog.fieldUnits();
    definition.uniqueFieldValues = dialog.economicUniqueFieldValues();
    definition.variables = dialog.economicVariables();
    definition.rockTypeSettings = dialog.economicRockTypeSettings();
    definition.profitModel = dialog.profitModel();
    definition.profitField = dialog.profitField();
    QString generationError;
    if (!GenerateEconomicBlockModel(definition, &generationError)) {
        QMessageBox::warning(this, "Economic Model", generationError);
        return;
    }

    LayerNode *economicNode = m_layerModel->addEconomicModelLayer(name, economicFolder ? economicFolder : blockModelNode);
    if (!economicNode) {
        return;
    }
    m_economicModels.insert(EconomicModelKey(blockModelName, name), definition);

    const QModelIndex blockIndex = m_layerModel->indexFromNode(blockModelNode, 0);
    const QModelIndex parentIndex = m_layerModel->indexFromNode(economicFolder ? economicFolder : blockModelNode, 0);
    const QModelIndex index = m_layerModel->indexFromNode(economicNode, 0);
    if (blockIndex.isValid()) {
        m_layersView->expand(blockIndex);
    }
    if (parentIndex.isValid()) {
        m_layersView->expand(parentIndex);
    }
    if (index.isValid()) {
        m_layersView->setCurrentIndex(index);
        m_layersView->scrollTo(index);
    }
    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Economic model created: %1").arg(name));
    }
}

void MainWindow::OnModifyEconomicModel()
{
    if (!m_layerModel || !m_layersView) {
        return;
    }

    LayerNode *selectedNode = m_layerModel->nodeFromIndex(m_layersView->currentIndex());
    LayerNode *initialBlockNode = nullptr;
    QString initialEconomicModel;
    if (selectedNode && selectedNode->isEconomicModel()) {
        initialEconomicModel = selectedNode->name();
        initialBlockNode = FindBlockModelAncestor(selectedNode);
    } else if (selectedNode && selectedNode->isBlockModel()) {
        initialBlockNode = selectedNode;
    }

    const QList<LayerNode *> blockModelNodes = m_layerModel->blockModelLayers();
    QStringList blockModelNames;
    QHash<QString, QStringList> fieldsByBlockModel;
    QHash<QString, QString> internalPathsByBlockModel;
    QHash<QString, QStringList> economicModelsByBlockModel;
    QHash<QString, EconomicModelDefinition> definitionsByKey;

    auto collectEconomicNodes = [](LayerNode *root) {
        QList<LayerNode *> result;
        std::function<void(LayerNode *)> visit = [&](LayerNode *node) {
            if (!node) {
                return;
            }
            if (node->isEconomicModel()) {
                result.append(node);
            }
            for (LayerNode *child : node->children()) {
                visit(child);
            }
        };
        visit(root);
        return result;
    };

    for (LayerNode *blockNode : blockModelNodes) {
        if (!blockNode || (blockNode->parent() && blockNode->parent()->isEconomicModel())) {
            continue;
        }
        QStringList economicNames;
        for (LayerNode *child : collectEconomicNodes(blockNode)) {
            economicNames << child->name();
            const QString key = EconomicModelKey(blockNode->name(), child->name());
            definitionsByKey.insert(key, m_economicModels.value(key));
        }
        if (economicNames.isEmpty()) {
            continue;
        }
        economicNames.sort(Qt::CaseInsensitive);
        blockModelNames << blockNode->name();
        economicModelsByBlockModel.insert(blockNode->name(), economicNames);
        auto blockIt = m_blockModelLayers.constFind(blockNode->name());
        if (blockIt != m_blockModelLayers.constEnd()) {
            fieldsByBlockModel.insert(blockNode->name(), BlockModelFieldNames(blockIt.value().info));
            internalPathsByBlockModel.insert(blockNode->name(), blockIt.value().internalPath);
        }
    }

    blockModelNames.sort(Qt::CaseInsensitive);
    if (blockModelNames.isEmpty()) {
        QMessageBox::information(this, "Economic Model", "Create an economic model first.");
        return;
    }

    QString initialBlockModel = initialBlockNode && initialBlockNode->isBlockModel()
        ? initialBlockNode->name()
        : blockModelNames.first();
    if (!blockModelNames.contains(initialBlockModel, Qt::CaseInsensitive)) {
        initialBlockModel = blockModelNames.first();
    }
    if (initialEconomicModel.isEmpty()) {
        initialEconomicModel = economicModelsByBlockModel.value(initialBlockModel).value(0);
    }

    CreateEconomicModelDialog dialog(
        blockModelNames,
        fieldsByBlockModel,
        internalPathsByBlockModel,
        initialBlockModel,
        this,
        true,
        economicModelsByBlockModel,
        definitionsByKey,
        initialEconomicModel);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString blockModelName = dialog.blockModelName();
    const QString name = dialog.economicModelName();
    if (blockModelName.isEmpty() || name.isEmpty()) {
        return;
    }

    const QString key = EconomicModelKey(blockModelName, name);
    EconomicModelDefinition definition;
    definition.name = name;
    definition.blockModelLayerName = blockModelName;
    definition.fieldRoles = dialog.fieldRoles();
    definition.fieldUnits = dialog.fieldUnits();
    definition.uniqueFieldValues = dialog.economicUniqueFieldValues();
    definition.variables = dialog.economicVariables();
    definition.rockTypeSettings = dialog.economicRockTypeSettings();
    definition.profitModel = dialog.profitModel();
    definition.profitField = dialog.profitField();
    const EconomicModelDefinition previousDefinition = m_economicModels.value(key);
    definition.generatedInternalPath = previousDefinition.generatedInternalPath;
    definition.generatedStoredRelativePath = previousDefinition.generatedStoredRelativePath;
    definition.generatedCellCount = previousDefinition.generatedCellCount;
    QString generationError;
    if (!GenerateEconomicBlockModel(definition, &generationError)) {
        QMessageBox::warning(this, "Economic Model", generationError);
        return;
    }
    m_economicModels.insert(key, definition);

    LayerNode *blockNode = m_layerModel->findBlockModelLayerByName(blockModelName);
    if (blockNode) {
        for (LayerNode *child : collectEconomicNodes(blockNode)) {
            if (child && child->name() == name) {
                const QModelIndex index = m_layerModel->indexFromNode(child, 0);
                if (index.isValid()) {
                    m_layersView->setCurrentIndex(index);
                    m_layersView->scrollTo(index);
                }
                break;
            }
        }
    }
    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Economic model modified: %1").arg(name));
    }
}

void MainWindow::OnDeleteEconomicModel()
{
    if (!m_layerModel || !m_layersView) {
        return;
    }

    LayerNode *node = m_layerModel->nodeFromIndex(m_layersView->currentIndex());
    if (!node || !node->isEconomicModel()) {
        QMessageBox::information(this, "Economic Model", "Select an economic model layer to delete.");
        return;
    }

    const QString name = node->name();
    LayerNode *blockModelNode = FindBlockModelAncestor(node);
    const QString blockModelName = blockModelNode ? blockModelNode->name() : QString();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        "Delete Economic Model",
        QString("Delete economic model '%1'?").arg(name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!blockModelName.isEmpty()) {
        m_economicModels.remove(EconomicModelKey(blockModelName, name));
    }
    m_layerModel->removeNode(node);
    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Economic model deleted: %1").arg(name));
    }
}

LayerNode *MainWindow::FindBlockModelAncestor(LayerNode *node) const
{
    for (LayerNode *current = node; current; current = current->parent()) {
        if (current->isBlockModel()) {
            return current;
        }
    }
    return nullptr;
}

LayerNode *MainWindow::FindLegendOwnerBlockModel(LayerNode *node) const
{
    LayerNode *best = nullptr;
    for (LayerNode *current = node; current; current = current->parent()) {
        if (current->isBlockModel()) {
            best = current;
        }
    }
    return best;
}

LayerNode *MainWindow::FindEconomicModelsFolder(LayerNode *blockModelNode) const
{
    if (!blockModelNode || !blockModelNode->isBlockModel()) {
        return nullptr;
    }
    for (LayerNode *child : blockModelNode->children()) {
        if (child && child->type() == LayerNode::Type::Folder &&
            child->name().compare("Economic Models", Qt::CaseInsensitive) == 0) {
            return child;
        }
    }
    return nullptr;
}

LayerNode *MainWindow::EnsureBlockModelSystemFolder(LayerNode *blockModelNode, const QString &folderName)
{
    if (!m_layerModel || !blockModelNode || !blockModelNode->isBlockModel()) {
        return nullptr;
    }
    for (LayerNode *child : blockModelNode->children()) {
        if (child && child->type() == LayerNode::Type::Folder &&
            child->name().compare(folderName, Qt::CaseInsensitive) == 0) {
            child->setSystemNode(true);
            return child;
        }
    }
    LayerNode *folder = m_layerModel->addFolder(folderName, blockModelNode);
    if (folder) {
        folder->setSystemNode(true);
        folder->setVisible(true);
    }
    return folder;
}

void MainWindow::EnsureBlockModelSystemFolders(LayerNode *blockModelNode)
{
    if (!blockModelNode || !blockModelNode->isBlockModel()) {
        return;
    }
    if (blockModelNode->parent() && blockModelNode->parent()->isEconomicModel()) {
        return;
    }
    EnsureBlockModelSystemFolder(blockModelNode, "Legends");
    EnsureBlockModelSystemFolder(blockModelNode, "Topography");
    EnsureBlockModelSystemFolder(blockModelNode, "Economic Models");
}

void MainWindow::SyncLegendLayerNodes(const QString &blockModelLayerName)
{
    if (!m_layerModel) {
        return;
    }
    const QList<LayerNode *> blockNodes = m_layerModel->blockModelLayers();
    for (LayerNode *blockNode : blockNodes) {
        if (!blockNode || (blockNode->parent() && blockNode->parent()->isEconomicModel())) {
            continue;
        }
        if (!blockModelLayerName.isEmpty() &&
            blockNode->name().compare(blockModelLayerName, Qt::CaseInsensitive) != 0) {
            continue;
        }
        EnsureBlockModelSystemFolders(blockNode);
        LayerNode *legendFolder = EnsureBlockModelSystemFolder(blockNode, "Legends");
        if (!legendFolder) {
            continue;
        }
        QSet<QString> existing;
        for (LayerNode *child : legendFolder->children()) {
            if (child && child->isLegend()) {
                existing.insert(child->name().toLower());
            }
        }
        for (const BlockModelLegend &legend : m_blockModelLegends) {
            if (legend.layerName.compare(blockNode->name(), Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (existing.contains(legend.name.toLower())) {
                continue;
            }
            m_layerModel->addLegendLayer(legend.name, legendFolder);
            existing.insert(legend.name.toLower());
        }
    }
}

bool MainWindow::ModifyLegendFromLayerNode(LayerNode *legendNode)
{
    if (!legendNode || !legendNode->isLegend()) {
        return false;
    }
    LayerNode *blockNode = FindBlockModelAncestor(legendNode);
    if (!blockNode) {
        return false;
    }
    auto blockIt = m_blockModelLayers.find(blockNode->name());
    if (blockIt == m_blockModelLayers.end()) {
        return false;
    }
    BlockModelLegend *existingLegend = nullptr;
    for (BlockModelLegend &legend : m_blockModelLegends) {
        if (legend.layerName.compare(blockNode->name(), Qt::CaseInsensitive) == 0 &&
            legend.name.compare(legendNode->name(), Qt::CaseInsensitive) == 0) {
            existingLegend = &legend;
            break;
        }
    }
    if (!existingLegend) {
        return false;
    }
    CreateBlockModelLegendDialog dialog(
        BlockModelFieldNames(blockIt->info),
        blockNode->name(),
        existingLegend,
        this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString oldName = existingLegend->name;
    BlockModelLegend updated;
    updated.name = dialog.name();
    updated.layerName = dialog.layerName();
    updated.fieldName = dialog.fieldName();
    updated.bins = dialog.bins();
    *existingLegend = updated;
    if (updated.name.compare(oldName, Qt::CaseInsensitive) != 0) {
        const QModelIndex idx = m_layerModel->indexFromNode(legendNode, 0);
        if (idx.isValid()) {
            m_layerModel->setData(idx, updated.name, Qt::EditRole);
        } else {
            legendNode->setName(updated.name);
        }
    }

    for (LayerNode *candidate : m_layerModel->blockModelLayers()) {
        if (!candidate || FindLegendOwnerBlockModel(candidate) != blockNode) {
            continue;
        }
        auto settingsIt = m_blockModelSettings.find(candidate->name());
        if (settingsIt == m_blockModelSettings.end()) {
            continue;
        }
        const bool usesOldName = settingsIt->blockLegend.compare(oldName, Qt::CaseInsensitive) == 0;
        const bool usesNewName = settingsIt->blockLegend.compare(updated.name, Qt::CaseInsensitive) == 0;
        if (!usesOldName && !usesNewName) {
            continue;
        }
        if (usesOldName && updated.name.compare(oldName, Qt::CaseInsensitive) != 0) {
            settingsIt->blockLegend = updated.name;
        }
        auto dataIt = m_blockModelLayers.find(candidate->name());
        if (dataIt != m_blockModelLayers.end()) {
            dataIt->renderDirty = true;
        }
        ApplyBlockModelRender(candidate->name());
    }
    return true;
}

void MainWindow::OnOptimizationSettings()
{
    if (!m_layerModel) {
        return;
    }

    QList<OpenPitOptimizationDialog::EconomicModelOption> economicOptions;
    const QList<LayerNode *> blockNodes = m_layerModel->blockModelLayers();
    auto collectEconomicNodes = [](LayerNode *root) {
        QList<LayerNode *> result;
        std::function<void(LayerNode *)> visit = [&](LayerNode *node) {
            if (!node) {
                return;
            }
            if (node->isEconomicModel()) {
                result.append(node);
            }
            for (LayerNode *child : node->children()) {
                visit(child);
            }
        };
        visit(root);
        return result;
    };
    for (LayerNode *blockNode : blockNodes) {
        if (!blockNode || (blockNode->parent() && blockNode->parent()->isEconomicModel())) {
            continue;
        }
        for (LayerNode *child : collectEconomicNodes(blockNode)) {
            const QString key = EconomicModelKey(blockNode->name(), child->name());
            if (!m_economicModels.contains(key)) {
                continue;
            }
            const EconomicModelDefinition definition = m_economicModels.value(key);
            OpenPitOptimizationDialog::EconomicModelOption option;
            option.key = key;
            option.blockModelName = blockNode->name();
            option.modelName = child->name();
            option.label = QString("%1 / %2").arg(blockNode->name(), child->name());
            option.internalPath = definition.generatedInternalPath;
            option.definition = definition;
            economicOptions.append(option);
        }
    }
    std::sort(economicOptions.begin(), economicOptions.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.label, b.label, Qt::CaseInsensitive) < 0;
    });
    if (economicOptions.isEmpty()) {
        QMessageBox::information(this, "Open Pit Optimization", "Create an economic model first.");
        return;
    }

    QList<OpenPitOptimizationDialog::SurfaceOption> surfaces;
    for (LayerNode *node : m_layerModel->geometryLayers()) {
        if (!node) {
            continue;
        }
        OpenPitOptimizationDialog::SurfaceOption surface;
        surface.layerName = node->name();
        for (auto it = node->history().constBegin(); it != node->history().constEnd(); ++it) {
            const QDate date = LayerModel::DateFromKey(it.key());
            surface.dates.insert(it.key(), date.isValid() ? date.toString("yyyy-MM-dd") : QString::number(it.key()));
        }
        surfaces.append(surface);
    }
    std::sort(surfaces.begin(), surfaces.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.layerName, b.layerName, Qt::CaseInsensitive) < 0;
    });

    OpenPitOptimizationDialog dialog(economicOptions, surfaces, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto settings = dialog.settings();
    QString error;
    if (!RunMineFlowOptimization(
            settings.economicModelKey,
            settings.startRevenueFactor,
            settings.maxRevenueFactor,
            settings.revenueFactorStep,
            settings.globalSlopeAngleDeg,
            &error)) {
        QMessageBox::warning(this, "Open Pit Optimization", error);
    }
}

bool MainWindow::RunMineFlowOptimization(
    const QString &economicModelKey,
    double startRevenueFactor,
    double maxRevenueFactor,
    double revenueFactorStep,
    double globalSlopeAngleDeg,
    QString *errorMessage)
{
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    if (!m_layerModel) {
        return fail("Layer model is not available.");
    }
    if (!m_economicModels.contains(economicModelKey)) {
        return fail("The selected economic model is no longer available.");
    }
    const EconomicModelDefinition definition = m_economicModels.value(economicModelKey);
    if (definition.generatedInternalPath.isEmpty() || !QFileInfo::exists(definition.generatedInternalPath)) {
        return fail("The selected economic model data file is not available.");
    }

    LayerNode *blockNode = m_layerModel->findBlockModelLayerByName(definition.blockModelLayerName);
    LayerNode *economicNode = nullptr;
    if (blockNode) {
        std::function<void(LayerNode *)> findEconomic = [&](LayerNode *node) {
            if (!node || economicNode) {
                return;
            }
            if (node->isEconomicModel() && node->name() == definition.name) {
                economicNode = node;
                return;
            }
            for (LayerNode *child : node->children()) {
                findEconomic(child);
            }
        };
        findEconomic(blockNode);
    }
    if (!economicNode) {
        return fail("The selected economic model layer is not available.");
    }

    visor::datamine::InternalBlockModelInfo info;
    try {
        info = visor::datamine::DmBlockModelImporter::readInternalInfo(definition.generatedInternalPath.toStdString());
    } catch (const std::exception &ex) {
        return fail(QString("Unable to read the economic model: %1").arg(ex.what()));
    }
    const auto &prototype = info.prototype;
    if (prototype.nx <= 0 || prototype.ny <= 0 || prototype.nz <= 0) {
        return fail("The economic model has invalid grid dimensions.");
    }

    const qint64 numBlocks = static_cast<qint64>(prototype.nx) * prototype.ny * prototype.nz;
    if (numBlocks <= 0 || numBlocks > std::numeric_limits<int>::max() * 32ll) {
        return fail("The economic model grid is too large for the current MineFlow integration.");
    }

    QStringList reportProducts;
    QString densityField;
    QString rockTypeField;
    QHash<QString, QString> reportProductUnits;
    for (auto it = definition.fieldRoles.constBegin(); it != definition.fieldRoles.constEnd(); ++it) {
        if (it.value() == "Product") {
            reportProducts << it.key();
            reportProductUnits.insert(it.key(), definition.fieldUnits.value(it.key()));
        } else if (it.value() == "Density" && densityField.isEmpty()) {
            densityField = it.key();
        } else if (it.value() == "Rock Type" && rockTypeField.isEmpty()) {
            rockTypeField = it.key();
        }
    }
    reportProducts.removeDuplicates();
    reportProducts.sort(Qt::CaseInsensitive);

    std::vector<MineFlowEconomicRecord> records(static_cast<std::size_t>(numBlocks));
    std::unordered_map<std::int64_t, visor::datamine::BlockCell> cellsByMineIndex;
    cellsByMineIndex.reserve(static_cast<std::size_t>(std::min<qint64>(info.cellCount, 1'000'000)));

    auto mineIndexForCell = [&](const visor::datamine::BlockCell &cell) -> std::int64_t {
        if (cell.i < 0 || cell.i >= prototype.nx ||
            cell.j < 0 || cell.j >= prototype.ny ||
            cell.k < 0 || cell.k >= prototype.nz) {
            return -1;
        }
        return static_cast<std::int64_t>(cell.i)
            + static_cast<std::int64_t>(cell.j) * prototype.nx
            + static_cast<std::int64_t>(cell.k) * prototype.nx * prototype.ny;
    };
    auto numericAttribute = [](const visor::datamine::BlockCell &cell, const QString &fieldName) {
        const std::string normalized = visor::datamine::normalizeFieldName(fieldName.toStdString());
        auto it = cell.numericAttributes.find(normalized);
        return it == cell.numericAttributes.end() ? 0.0 : it->second;
    };
    auto alphaAttribute = [](const visor::datamine::BlockCell &cell, const QString &fieldName) {
        const std::string normalized = visor::datamine::normalizeFieldName(fieldName.toStdString());
        auto it = cell.alphaAttributes.find(normalized);
        return it == cell.alphaAttributes.end() ? QString() : QString::fromStdString(it->second);
    };
    auto reportFieldString = [](const visor::datamine::BlockCell &cell, const QString &fieldName) {
        const std::string normalized = visor::datamine::normalizeFieldName(fieldName.toStdString());
        auto alphaIt = cell.alphaAttributes.find(normalized);
        if (alphaIt != cell.alphaAttributes.end()) {
            return QString::fromStdString(alphaIt->second);
        }
        auto numericIt = cell.numericAttributes.find(normalized);
        if (numericIt != cell.numericAttributes.end()) {
            return QString::number(numericIt->second, 'g', 15);
        }
        return QString();
    };
    auto rockSettingsForReport = [&](const QString &rockType) {
        for (const auto &settings : definition.rockTypeSettings) {
            if (settings.rockType.compare(rockType, Qt::CaseInsensitive) == 0) {
                return settings;
            }
        }
        return definition.rockTypeSettings.isEmpty()
            ? EconomicModelDefinition::RockTypeSettings{}
            : definition.rockTypeSettings.first();
    };
    auto reportTonnesForCell = [&](const visor::datamine::BlockCell &cell) {
        double baseTonnes = std::max(0.0, cell.volume);
        if (densityField.isEmpty()) {
            return baseTonnes;
        }
        const double density = numericAttribute(cell, densityField);
        baseTonnes *= std::max(0.0, density);
        if (rockTypeField.isEmpty()) {
            return baseTonnes;
        }
        const QString rockType = reportFieldString(cell, rockTypeField);
        const auto settings = rockSettingsForReport(rockType);
        const double miningRecovery = std::max(0.0, settings.miningRecovery / 100.0);
        const double dilution = std::max(0.0, settings.dilution / 100.0);
        return baseTonnes * miningRecovery / (1.0 + dilution);
    };
    auto reportProductContribution = [&](const visor::datamine::BlockCell &cell, const QString &product, double tonnes) {
        const double productValue = numericAttribute(cell, product);
        const QString unit = reportProductUnits.value(product);
        if (unit.startsWith("Mass", Qt::CaseInsensitive)) {
            return productValue;
        }
        return productValue * tonnes;
    };
    PitOptimizationReport report;
    report.economicModelName = definition.name;
    report.products = reportProducts;
    report.productUnits = reportProductUnits;
    QSet<QString> reportDestinations;
    auto accumulateReportCell = [&](PitOptimizationReportRow &row, const visor::datamine::BlockCell &cell) {
        QString destination = alphaAttribute(cell, "DESTINATION").trimmed();
        if (destination.isEmpty()) {
            destination = "Waste";
        }
        const bool isWaste = destination.compare("Waste", Qt::CaseInsensitive) == 0;
        const double tonnes = reportTonnesForCell(cell);
        row.totalTonnes += tonnes;
        if (isWaste) {
            row.wasteTonnes += tonnes;
        } else {
            row.oreTonnes += tonnes;
            reportDestinations.insert(destination);
        }

        PitOptimizationDestinationReport destinationReport = row.destinations.value(destination);
        destinationReport.tonnes += tonnes;
        for (const QString &product : reportProducts) {
            const double contribution = reportProductContribution(cell, product, tonnes);
            if (!isWaste) {
                row.oreProductWeightedSums.insert(
                    product,
                    row.oreProductWeightedSums.value(product) + contribution);
            }
            destinationReport.productWeightedSums.insert(
                product,
                destinationReport.productWeightedSums.value(product) + contribution);
        }
        row.destinations.insert(destination, destinationReport);
    };

    QProgressDialog progress("Preparing MineFlow model...", "Cancel", 0, 100, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(500);

    PitOptimizationReportRow economicModelRow;
    economicModelRow.label = "Economic Model";
    std::int64_t readCount = 0;
    try {
        const bool completed = visor::datamine::DmBlockModelImporter::forEachInternalCellValue(
            definition.generatedInternalPath.toStdString(),
            std::string(),
            [&](const visor::datamine::BlockCell &cell, double, bool, std::int64_t, std::int64_t total) {
                const std::int64_t mineIndex = mineIndexForCell(cell);
                if (mineIndex >= 0 && mineIndex < numBlocks) {
                    MineFlowEconomicRecord &record = records[static_cast<std::size_t>(mineIndex)];
                    record.exists = true;
                    record.revenue = numericAttribute(cell, "REVENUE");
                    record.miningCost = numericAttribute(cell, "MINING_COST");
                    record.processingCost = numericAttribute(cell, "PROCESSING_COST");
                    cellsByMineIndex[mineIndex] = cell;
                    accumulateReportCell(economicModelRow, cell);
                }
                ++readCount;
                if (readCount % 25000 == 0) {
                    const int pct = total > 0 ? static_cast<int>((readCount * 20) / total) : 0;
                    progress.setValue(std::clamp(pct, 0, 20));
                    QApplication::processEvents();
                    return !progress.wasCanceled();
                }
                return true;
            },
            std::nullopt,
            true);
        if (!completed || progress.wasCanceled()) {
            return fail("MineFlow optimization was cancelled.");
        }
    } catch (const std::exception &ex) {
        return fail(QString("Unable to read economic values: %1").arg(ex.what()));
    }
    if (cellsByMineIndex.empty()) {
        return fail("The selected economic model does not contain any blocks.");
    }
    report.economicModelRows.append(economicModelRow);

    std::vector<double> revenueFactors;
    std::string revenueFactorError;
    if (!BuildMineFlowRevenueFactors(
            startRevenueFactor,
            maxRevenueFactor,
            revenueFactorStep,
            &revenueFactors,
            &revenueFactorError)) {
        return fail(QString::fromStdString(revenueFactorError));
    }

    progress.setLabelText("Building MineFlow precedence graph...");
    progress.setValue(25);
    QApplication::processEvents();
    if (progress.wasCanceled()) {
        return fail("MineFlow optimization was cancelled.");
    }

    std::vector<visor::datamine::DmField> outputFields = info.sourceHeader.fields;
    QSet<QString> fieldNames;
    for (const auto &field : outputFields) {
        fieldNames.insert(QString::fromStdString(visor::datamine::normalizeFieldName(field.name)));
    }
    auto addNumericOutputField = [&](const QString &fieldName) {
        const QString normalized = QString::fromStdString(visor::datamine::normalizeFieldName(fieldName.toStdString()));
        if (fieldNames.contains(normalized)) {
            return;
        }
        fieldNames.insert(normalized);
        visor::datamine::DmField field;
        field.name = normalized.toStdString();
        field.type = visor::datamine::DmFieldType::Numeric;
        outputFields.push_back(field);
    };
    addNumericOutputField("RF");
    addNumericOutputField("RF_VALUE");

    const QString outputDir = QDir(EconomicModelCacheDir(m_projectPath)).filePath("pit_shells");
    if (!QDir().mkpath(outputDir)) {
        return fail("Unable to create the pit shell output folder.");
    }

    MineFlowOptimizationInput optimizationInput;
    optimizationInput.prototype = prototype;
    optimizationInput.numBlocks = numBlocks;
    optimizationInput.records = &records;
    optimizationInput.revenueFactors = revenueFactors;
    optimizationInput.globalSlopeAngleDeg = globalSlopeAngleDeg;
    optimizationInput.beforeSolve = [&](int shellIndex, int shellCount, double rf) {
        progress.setLabelText(QString("Running MineFlow RF %1...").arg(rf, 0, 'f', 2));
        progress.setValue(30 + static_cast<int>((shellIndex * 50) / std::max(1, shellCount)));
        QApplication::processEvents();
        return !progress.wasCanceled();
    };

    std::vector<MineFlowPitShell> optimizedShells;
    std::string optimizationError;
    if (!RunMineFlowPitOptimization(optimizationInput, &optimizedShells, &optimizationError)) {
        return fail(QString::fromStdString(optimizationError));
    }

    QString lastLayerName;
    int shellIndex = 0;
    for (const MineFlowPitShell &shell : optimizedShells) {
        const double rf = shell.revenueFactor;
        progress.setLabelText(QString("Writing pit shell RF %1...").arg(rf, 0, 'f', 2));
        progress.setValue(80 + static_cast<int>((shellIndex * 15) / std::max(1, static_cast<int>(optimizedShells.size()))));
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            return fail("MineFlow optimization was cancelled.");
        }

        std::vector<visor::datamine::BlockCell> pitCells;
        pitCells.reserve(shell.mineIndices.size());
        PitOptimizationReportRow reportRow;
        reportRow.revenueFactor = rf;
        reportRow.label = QString::number(rf, 'f', 2);
        for (const std::int64_t mineIndex : shell.mineIndices) {
            const auto cellIt = cellsByMineIndex.find(mineIndex);
            if (cellIt == cellsByMineIndex.end()) {
                continue;
            }
            visor::datamine::BlockCell cell = cellIt->second;
            const MineFlowEconomicRecord &record = records[static_cast<std::size_t>(mineIndex)];
            cell.numericAttributes["RF"] = rf;
            cell.numericAttributes["RF_VALUE"] = rf * record.revenue - record.miningCost - record.processingCost;
            accumulateReportCell(reportRow, cell);

            pitCells.push_back(std::move(cell));
        }
        std::sort(pitCells.begin(), pitCells.end(), [](const auto &a, const auto &b) {
            return a.ijk < b.ijk;
        });
        report.rows.append(reportRow);

        const QString shellLabel = QString("RF_%1").arg(rf, 0, 'f', 2).replace('.', '_');
        QString layerName = UniqueChildName(economicNode, QString("%1 %2").arg(definition.name, shellLabel));
        while (m_blockModelLayers.contains(layerName)) {
            layerName = UniqueChildName(economicNode, layerName + " 1");
        }
        const QString fileName = QString("%1_%2.gvbm").arg(SanitizeName(definition.name), SanitizeName(shellLabel));
        QString outputPath = QDir(outputDir).filePath(fileName);
        int suffix = 1;
        while (QFileInfo::exists(outputPath)) {
            outputPath = QDir(outputDir).filePath(QString("%1_%2.gvbm")
                .arg(SanitizeName(definition.name + "_" + shellLabel))
                .arg(suffix++));
        }

        visor::datamine::InternalBlockModelInfo pitInfo;
        try {
            pitInfo = visor::datamine::DmBlockModelImporter::writeInternalFile(
                outputPath.toStdString(),
                prototype,
                QString("MineFlow pit shell %1 RF %2").arg(definition.name).arg(rf, 0, 'f', 2).toStdString(),
                QFileInfo(outputPath).fileName().toStdString(),
                outputFields,
                pitCells);
        } catch (const std::exception &ex) {
            return fail(QString("Unable to write pit shell RF %1: %2").arg(rf, 0, 'f', 2).arg(ex.what()));
        }

        LayerNode *pitNode = m_layerModel->addBlockModelLayer(layerName, economicNode);
        if (!pitNode) {
            return fail("Unable to create the pit shell layer.");
        }
        BlockModelLayerData data;
        data.info = pitInfo;
        data.internalPath = outputPath;
        data.description = QString("MineFlow pit shell RF %1").arg(rf, 0, 'f', 2);
        data.renderDirty = true;
        m_blockModelLayers.insert(layerName, std::move(data));
        BlockModelDisplaySettings pitSettings;
        pitSettings.renderMode = BlockModelRenderMode::Solid3D;
        pitSettings.blocksEnabled = true;
        pitSettings.linesEnabled = false;
        m_blockModelSettings.insert(layerName, pitSettings);
        lastLayerName = layerName;

        if (m_consoleLog) {
            m_consoleLog->append(QString(">> MineFlow RF %1: %2 blocks, value %3")
                .arg(rf, 0, 'f', 2)
                .arg(static_cast<qint64>(pitCells.size()))
                .arg(static_cast<qlonglong>(shell.containedValue)));
        }
        ++shellIndex;
    }

    if (m_layersView) {
        if (QModelIndex economicIndex = m_layerModel->indexFromNode(economicNode, 0); economicIndex.isValid()) {
            m_layersView->expand(economicIndex);
        }
    }
    if (!lastLayerName.isEmpty()) {
        ApplyBlockModelRender(lastLayerName);
        if (m_consoleLog) {
            m_consoleLog->append(QString(">> MineFlow optimization completed: %1 shell(s)").arg(revenueFactors.size()));
        }
    }
    for (const QString &destination : reportDestinations) {
        report.destinationNames << destination;
    }
    report.destinationNames.sort(Qt::CaseInsensitive);
    progress.setValue(100);
    if (!report.rows.isEmpty()) {
        PitOptimizationReportDialog dialog(report, this);
        dialog.exec();
    }
    return true;
}

void MainWindow::OnPickBlockSectionTwoPoints()
{
    if (!m_layerModel || !m_layersView) {
        return;
    }

    LayerNode *selectedNode = m_layerModel->nodeFromIndex(m_layersView->currentIndex());
    LayerNode *blockModelNode = nullptr;
    for (LayerNode *node = selectedNode; node != nullptr; node = node->parent()) {
        if (node->isBlockModel()) {
            blockModelNode = node;
            break;
        }
    }
    if (!blockModelNode) {
        QMessageBox::information(this, "2 Points Section", "Select a block model layer first.");
        return;
    }

    auto blockIt = m_blockModelLayers.find(blockModelNode->name());
    if (blockIt == m_blockModelLayers.end()) {
        return;
    }

    m_blockSectionLayerName = blockModelNode->name();
    m_blockSectionPoints.clear();
    m_blockSectionPickActive = true;

    if (m_consoleLog) {
        m_consoleLog->append(">> Pick first section point.");
    }
}

bool MainWindow::HandleBlockSectionPick(const QPoint &pos)
{
    if (!m_blockSectionPickActive || m_blockSectionLayerName.isEmpty()) {
        return false;
    }

    const std::array<double, 3> point = PickWorldPointWithSnap(pos);
    m_blockSectionPoints.push_back(point);
    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Section point %1: %2, %3, %4")
                                 .arg(static_cast<int>(m_blockSectionPoints.size()))
                                 .arg(point[0], 0, 'f', 3)
                                 .arg(point[1], 0, 'f', 3)
                                 .arg(point[2], 0, 'f', 3));
    }

    if (m_blockSectionPoints.size() < 2) {
        if (m_consoleLog) {
            m_consoleLog->append(">> Pick second section point.");
        }
        return true;
    }

    m_blockSectionPickActive = false;
    QMessageBox prompt(this);
    prompt.setWindowTitle("2 Points Section");
    prompt.setText("Create section from the selected points.");
    QPushButton *verticalButton = prompt.addButton("Vertical", QMessageBox::AcceptRole);
    QPushButton *horizontalButton = prompt.addButton("Horizontal", QMessageBox::AcceptRole);
    prompt.addButton(QMessageBox::Cancel);
    prompt.exec();
    if (prompt.clickedButton() != verticalButton && prompt.clickedButton() != horizontalButton) {
        m_blockSectionPoints.clear();
        return true;
    }

    BlockModelDisplaySettings settings = m_blockModelSettings.value(m_blockSectionLayerName);
    settings.renderMode = BlockModelRenderMode::Section2D;
    settings.blocksEnabled = true;
    m_blockModelSettings.insert(m_blockSectionLayerName, settings);
    ApplyBlockModelRender(m_blockSectionLayerName);

    BuildBlockModelSection(
        m_blockSectionLayerName,
        m_blockSectionPoints[0],
        m_blockSectionPoints[1],
        prompt.clickedButton() == verticalButton);
    m_blockSectionPoints.clear();
    return true;
}

std::array<double, 3> MainWindow::PickWorldPointWithSnap(const QPoint &pos) const
{
    std::array<double, 3> fallback{0.0, 0.0, 0.0};
    if (!m_vtkWidget || !m_renderer) {
        return fallback;
    }

    const qreal dpr = m_vtkWidget->devicePixelRatioF();
    const int height = static_cast<int>(std::lround(m_vtkWidget->height() * dpr));
    const double displayX = pos.x() * dpr;
    const double displayY = height - 1 - pos.y() * dpr;

    double focal[4] = {0.0, 0.0, 0.0, 1.0};
    if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
        double fp[3];
        camera->GetFocalPoint(fp);
        m_renderer->SetWorldPoint(fp[0], fp[1], fp[2], 1.0);
        m_renderer->WorldToDisplay();
        const double *display = m_renderer->GetDisplayPoint();
        focal[2] = display ? display[2] : 0.5;
    }

    m_renderer->SetDisplayPoint(displayX, displayY, focal[2]);
    m_renderer->DisplayToWorld();
    double *world = m_renderer->GetWorldPoint();
    if (world) {
        const double w = std::abs(world[3]) > 1e-12 ? world[3] : 1.0;
        fallback = {world[0] / w, world[1] / w, world[2] / w};
    }

    constexpr double snapRadiusPx = 12.0;
    double bestDistance = snapRadiusPx * snapRadiusPx;
    std::optional<std::array<double, 3>> snapped;
    auto considerPoint = [&](double x, double y, double z) {
        m_renderer->SetWorldPoint(x, y, z, 1.0);
        m_renderer->WorldToDisplay();
        const double *display = m_renderer->GetDisplayPoint();
        if (!display) {
            return;
        }
        const double dx = display[0] - displayX;
        const double dy = display[1] - displayY;
        const double distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            snapped = std::array<double, 3>{x, y, z};
        }
    };

    for (const auto &item : m_sceneItems) {
        if (!item.data || (item.actor && !item.actor->GetVisibility())) {
            continue;
        }
        vtkPoints *points = item.data->GetPoints();
        if (!points) {
            continue;
        }
        const vtkIdType count = points->GetNumberOfPoints();
        for (vtkIdType i = 0; i < count; ++i) {
            double p[3];
            points->GetPoint(i, p);
            considerPoint(p[0], p[1], p[2]);
        }
    }
    for (auto it = m_blockModelLayers.constBegin(); it != m_blockModelLayers.constEnd(); ++it) {
        for (const auto &cell : it->previewCells) {
            considerPoint(cell.xc, cell.yc, cell.zc);
        }
    }
    return snapped.value_or(fallback);
}

void MainWindow::BuildBlockModelSection(
    const QString &layerName,
    const std::array<double, 3> &firstPoint,
    const std::array<double, 3> &secondPoint,
    bool verticalSection)
{
    auto blockIt = m_blockModelLayers.find(layerName);
    if (blockIt == m_blockModelLayers.end()) {
        return;
    }
    BlockModelDisplaySettings settings = m_blockModelSettings.value(layerName);

    LayerNode *layerNode = m_layerModel ? m_layerModel->findBlockModelLayerByName(layerName) : nullptr;
    LayerNode *legendOwner = FindLegendOwnerBlockModel(layerNode);
    const QString legendLayerName = legendOwner ? legendOwner->name() : layerName;
    const BlockModelLegend *activeLegend = nullptr;
    if (!settings.blockLegend.isEmpty()) {
        for (const BlockModelLegend &legend : m_blockModelLegends) {
            if (legend.layerName.compare(legendLayerName, Qt::CaseInsensitive) == 0 &&
                legend.name.compare(settings.blockLegend, Qt::CaseInsensitive) == 0) {
                activeLegend = &legend;
                break;
            }
        }
    }
    const bool useLegend = activeLegend && !activeLegend->fieldName.isEmpty() && !activeLegend->bins.isEmpty();
    auto colorForValue = [&](double value, bool hasValue) -> std::optional<QColor> {
        if (!useLegend) {
            return settings.blockColor;
        }
        if (!hasValue) {
            return std::nullopt;
        }
        for (const BlockModelLegendBin &bin : activeLegend->bins) {
            if (!bin.visible) {
                continue;
            }
            const double lo = std::min(bin.minValue, bin.maxValue);
            const double hi = std::max(bin.minValue, bin.maxValue);
            if (value >= lo && value <= hi) {
                return bin.color;
            }
        }
        return std::nullopt;
    };

    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> polys;
    vtkNew<vtkUnsignedCharArray> colors;
    colors->SetName("SectionColor");
    colors->SetNumberOfComponents(3);
    const double gapScale = std::clamp(1.0 - (settings.blockGapPercent / 100.0), 0.05, 1.0);

    std::array<double, 3> sectionAxis{
        secondPoint[0] - firstPoint[0],
        secondPoint[1] - firstPoint[1],
        verticalSection ? 0.0 : secondPoint[2] - firstPoint[2]};
    double axisLength = std::sqrt(
        sectionAxis[0] * sectionAxis[0] +
        sectionAxis[1] * sectionAxis[1] +
        sectionAxis[2] * sectionAxis[2]);
    if (axisLength <= 1e-9) {
        QMessageBox::warning(this, "2 Points Section", "The selected points are too close together.");
        return;
    }
    sectionAxis[0] /= axisLength;
    sectionAxis[1] /= axisLength;
    sectionAxis[2] /= axisLength;

    std::array<double, 3> planeNormal;
    std::array<double, 3> planeU;
    std::array<double, 3> planeV;
    std::array<double, 3> planePoint = firstPoint;
    if (verticalSection) {
        planeNormal = {-sectionAxis[1], sectionAxis[0], 0.0};
        planeU = sectionAxis;
        planeV = {0.0, 0.0, 1.0};
    } else {
        planePoint[2] = 0.5 * (firstPoint[2] + secondPoint[2]);
        planeNormal = {0.0, 0.0, 1.0};
        planeU = {1.0, 0.0, 0.0};
        planeV = {0.0, 1.0, 0.0};
    }

    auto dot = [](const std::array<double, 3> &a, const std::array<double, 3> &b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    auto sub = [](const std::array<double, 3> &a, const std::array<double, 3> &b) {
        return std::array<double, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    };

    auto addColor = [&](const QColor &color) {
        unsigned char tuple[3] = {
            static_cast<unsigned char>(std::clamp(color.red(), 0, 255)),
            static_cast<unsigned char>(std::clamp(color.green(), 0, 255)),
            static_cast<unsigned char>(std::clamp(color.blue(), 0, 255))};
        colors->InsertNextTypedTuple(tuple);
    };

    std::int64_t added = 0;
    blockIt->sectionSourceCells.clear();
    blockIt->sectionSourcePolygons.clear();
    blockIt->sectionSourceRows.clear();
    blockIt->sectionCells.clear();
    blockIt->sectionPolygons.clear();
    blockIt->sectionRows.clear();
    auto appendSectionCell = [&](const visor::datamine::BlockCell &cell, std::int64_t row, double value, bool hasValue) {
        const double hx = 0.5 * cell.xinc;
        const double hy = 0.5 * cell.yinc;
        const double hz = 0.5 * cell.zinc;
        const std::array<double, 3> corners[8] = {
            std::array<double, 3>{cell.xc - hx, cell.yc - hy, cell.zc - hz},
            std::array<double, 3>{cell.xc + hx, cell.yc - hy, cell.zc - hz},
            std::array<double, 3>{cell.xc + hx, cell.yc + hy, cell.zc - hz},
            std::array<double, 3>{cell.xc - hx, cell.yc + hy, cell.zc - hz},
            std::array<double, 3>{cell.xc - hx, cell.yc - hy, cell.zc + hz},
            std::array<double, 3>{cell.xc + hx, cell.yc - hy, cell.zc + hz},
            std::array<double, 3>{cell.xc + hx, cell.yc + hy, cell.zc + hz},
            std::array<double, 3>{cell.xc - hx, cell.yc + hy, cell.zc + hz}};
        constexpr int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}};

        std::vector<std::array<double, 3>> polygon;
        auto addUnique = [&](const std::array<double, 3> &p) {
            for (const auto &existing : polygon) {
                const double dx = existing[0] - p[0];
                const double dy = existing[1] - p[1];
                const double dz = existing[2] - p[2];
                if ((dx * dx + dy * dy + dz * dz) < 1e-12) {
                    return;
                }
            }
            polygon.push_back(p);
        };

        for (const auto &edge : edges) {
            const auto &a = corners[edge[0]];
            const auto &b = corners[edge[1]];
            const double da = dot(sub(a, planePoint), planeNormal);
            const double db = dot(sub(b, planePoint), planeNormal);
            if (std::abs(da) <= 1e-9) {
                addUnique(a);
            }
            if (std::abs(db) <= 1e-9) {
                addUnique(b);
            }
            if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0)) {
                const double t = da / (da - db);
                addUnique({
                    a[0] + (b[0] - a[0]) * t,
                    a[1] + (b[1] - a[1]) * t,
                    a[2] + (b[2] - a[2]) * t});
            }
        }
        if (polygon.size() < 3) {
            return;
        }

        std::array<double, 3> center{0.0, 0.0, 0.0};
        for (const auto &p : polygon) {
            center[0] += p[0];
            center[1] += p[1];
            center[2] += p[2];
        }
        center[0] /= static_cast<double>(polygon.size());
        center[1] /= static_cast<double>(polygon.size());
        center[2] /= static_cast<double>(polygon.size());
        std::sort(polygon.begin(), polygon.end(), [&](const auto &a, const auto &b) {
            const auto ra = sub(a, center);
            const auto rb = sub(b, center);
            const double aa = std::atan2(dot(ra, planeV), dot(ra, planeU));
            const double ab = std::atan2(dot(rb, planeV), dot(rb, planeU));
            return aa < ab;
        });
        if (gapScale < 0.999) {
            for (auto &p : polygon) {
                p[0] = center[0] + (p[0] - center[0]) * gapScale;
                p[1] = center[1] + (p[1] - center[1]) * gapScale;
                p[2] = center[2] + (p[2] - center[2]) * gapScale;
            }
        }

        blockIt->sectionSourceCells.push_back(cell);
        blockIt->sectionSourcePolygons.push_back(polygon);
        blockIt->sectionSourceRows.push_back(row);

        const std::optional<QColor> cellColor = colorForValue(value, hasValue);
        if (!cellColor) {
            return;
        }

        std::vector<vtkIdType> ids;
        ids.reserve(polygon.size());
        for (const auto &p : polygon) {
            ids.push_back(points->InsertNextPoint(p[0], p[1], p[2]));
        }
        polys->InsertNextCell(static_cast<vtkIdType>(ids.size()), ids.data());
        addColor(*cellColor);
        blockIt->sectionCells.push_back(cell);
        blockIt->sectionPolygons.push_back(polygon);
        blockIt->sectionRows.push_back(row);
        if (!cell.numericAttributes.empty() || !cell.alphaAttributes.empty()) {
            blockIt->fullCellCache[row] = cell;
        }
        ++added;
    };

    try {
        if (useLegend) {
            visor::datamine::DmBlockModelImporter::forEachInternalCellValue(
                blockIt->internalPath.toStdString(),
                activeLegend->fieldName.toStdString(),
                [&](const visor::datamine::BlockCell &cell, double value, bool hasValue, std::int64_t row, std::int64_t) {
                    appendSectionCell(cell, row, value, hasValue);
                    return true;
                },
                std::nullopt,
                true);
        } else {
            visor::datamine::DmBlockModelImporter::forEachInternalBaseCell(
                blockIt->internalPath.toStdString(),
                [&](const visor::datamine::BlockCell &cell, std::int64_t row, std::int64_t) {
                    appendSectionCell(cell, row, 0.0, false);
                    return true;
                });
        }
    } catch (const std::exception &ex) {
        QMessageBox::warning(this, "2 Points Section", QString("Unable to build section:\n%1").arg(QString::fromLocal8Bit(ex.what())));
        return;
    }

    vtkSmartPointer<vtkPolyData> sectionData = vtkSmartPointer<vtkPolyData>::New();
    sectionData->SetPoints(points);
    sectionData->SetPolys(polys);
    sectionData->GetCellData()->SetScalars(colors);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(sectionData);
    mapper->ScalarVisibilityOn();
    mapper->SetScalarModeToUseCellData();
    mapper->SetColorModeToDirectScalars();
    mapper->Update();

    if (blockIt->sectionActor) {
        m_renderer->RemoveActor(blockIt->sectionActor);
    }
    blockIt->sectionActor = vtkSmartPointer<vtkActor>::New();
    blockIt->sectionActor->SetMapper(mapper);
    blockIt->sectionActor->GetProperty()->SetRepresentationToWireframe();
    blockIt->sectionActor->GetProperty()->SetLineWidth(1.2);
    blockIt->sectionActor->GetProperty()->LightingOff();
    m_renderer->AddActor(blockIt->sectionActor);
    if (blockIt->actor) {
        blockIt->actor->SetVisibility(0);
    }

    if (added > 0) {
        m_renderer->ResetCamera(blockIt->sectionActor->GetBounds());
    }
    if (added > 0) {
        if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
            camera->ParallelProjectionOn();
            if (verticalSection) {
                const double focal[3] = {
                    0.5 * (firstPoint[0] + secondPoint[0]),
                    0.5 * (firstPoint[1] + secondPoint[1]),
                    blockIt->info.prototype.zOrigin + 0.5 * blockIt->info.prototype.nz * blockIt->info.prototype.parentZ};
                double bounds[6] = {0, 0, 0, 0, 0, 0};
                blockIt->sectionActor->GetBounds(bounds);
                const double spanX = std::max(bounds[1] - bounds[0], 1.0);
                const double spanY = std::max(bounds[3] - bounds[2], 1.0);
                const double spanZ = std::max(bounds[5] - bounds[4], 1.0);
                const double distance = std::max({spanX, spanY, spanZ}) * 2.0;
                camera->SetFocalPoint(focal);
                camera->SetPosition(focal[0] - sectionAxis[1] * distance, focal[1] + sectionAxis[0] * distance, focal[2]);
                camera->SetViewUp(0.0, 0.0, 1.0);
            }
            camera->OrthogonalizeViewUp();
        }
    }
    m_renderer->ResetCameraClippingRange();
    if (m_renderWindow) {
        m_renderWindow->Render();
    }
    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Block model section created: %1 cells.").arg(static_cast<qlonglong>(added)));
    }
}

void MainWindow::ShowBlockModelProperties(LayerNode *node)
{
    if (!node || !node->isBlockModel()) {
        return;
    }
    auto blockIt = m_blockModelLayers.find(node->name());
    if (blockIt == m_blockModelLayers.end()) {
        return;
    }

    LayerNode *legendOwner = FindLegendOwnerBlockModel(node);
    const QString legendLayerName = legendOwner ? legendOwner->name() : node->name();
    auto legendOwnerBlockIt = m_blockModelLayers.find(legendLayerName);
    if (legendOwnerBlockIt == m_blockModelLayers.end()) {
        legendOwnerBlockIt = blockIt;
    }

    QStringList legendNames;
    for (const BlockModelLegend &legend : m_blockModelLegends) {
        if (legend.layerName.compare(legendLayerName, Qt::CaseInsensitive) == 0) {
            legendNames << legend.name;
        }
    }

    BlockModelDisplaySettings settings = m_blockModelSettings.value(node->name());
    BlockModelPropertiesDialog dialog(node->name(), blockIt->info, settings, legendNames, m_viewMode, this);
    QString editedLegendName;
    if (QPushButton *legendButton = dialog.legendButton()) {
        connect(legendButton, &QPushButton::clicked, &dialog, [&]() {
            const QString selectedLegend = dialog.selectedLegend();
            MainWindow::BlockModelLegend *existingLegend = nullptr;
            for (BlockModelLegend &legend : m_blockModelLegends) {
                if (legend.layerName.compare(legendLayerName, Qt::CaseInsensitive) == 0 &&
                    legend.name.compare(selectedLegend, Qt::CaseInsensitive) == 0) {
                    existingLegend = &legend;
                    break;
                }
            }

            CreateBlockModelLegendDialog legendDialog(
                BlockModelFieldNames(legendOwnerBlockIt->info),
                legendLayerName,
                existingLegend,
                &dialog);
            if (legendDialog.exec() != QDialog::Accepted) {
                return;
            }

            BlockModelLegend legend;
            legend.name = legendDialog.name();
            legend.layerName = legendDialog.layerName();
            legend.fieldName = legendDialog.fieldName();
            legend.bins = legendDialog.bins();
            if (existingLegend) {
                *existingLegend = legend;
                LayerNode *legendFolder = EnsureBlockModelSystemFolder(legendOwner, "Legends");
                if (legendFolder) {
                    for (LayerNode *legendNode : legendFolder->children()) {
                        if (legendNode && legendNode->isLegend() &&
                            legendNode->name().compare(selectedLegend, Qt::CaseInsensitive) == 0) {
                            const QModelIndex idx = m_layerModel->indexFromNode(legendNode, 0);
                            if (idx.isValid()) {
                                m_layerModel->setData(idx, legend.name, Qt::EditRole);
                            } else {
                                legendNode->setName(legend.name);
                            }
                            break;
                        }
                    }
                }
            } else {
                m_blockModelLegends.append(legend);
            }
            editedLegendName = legend.name;
            SyncLegendLayerNodes(legendLayerName);

            QStringList updatedLegendNames;
            for (const BlockModelLegend &item : m_blockModelLegends) {
                if (item.layerName.compare(legendLayerName, Qt::CaseInsensitive) == 0) {
                    updatedLegendNames << item.name;
                }
            }
            dialog.setLegendNames(updatedLegendNames, editedLegendName);
        });
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString oldName = node->name();
    QString newName = dialog.layerName();
    if (newName.isEmpty()) {
        newName = oldName;
    }
    if (newName.compare(oldName, Qt::CaseInsensitive) != 0) {
        newName = UniqueChildName(node->parent() ? node->parent() : m_layerModel->root(), newName);
        const QModelIndex idx = m_layerModel->indexFromNode(node, 0);
        if (idx.isValid()) {
            m_layerModel->setData(idx, newName, Qt::EditRole);
        } else {
            node->setName(newName);
        }
        m_blockModelLayers.insert(newName, m_blockModelLayers.take(oldName));
        m_blockModelSettings.insert(newName, m_blockModelSettings.take(oldName));
        for (BlockModelLegend &legend : m_blockModelLegends) {
            if (legend.layerName.compare(oldName, Qt::CaseInsensitive) == 0) {
                legend.layerName = newName;
            }
        }
    }

    BlockModelDisplaySettings updated = dialog.settings();
    if (!editedLegendName.isEmpty()) {
        updated.blockLegend = editedLegendName;
    }
    m_blockModelSettings.insert(newName, updated);
    auto updatedIt = m_blockModelLayers.find(newName);
    if (updatedIt != m_blockModelLayers.end()) {
        updatedIt->renderDirty = true;
    }
    ApplyBlockModelRender(newName);
}

void MainWindow::ApplyBlockModelRender(const QString &layerName)
{
    auto blockIt = m_blockModelLayers.find(layerName);
    if (blockIt == m_blockModelLayers.end()) {
        return;
    }
    BlockModelDisplaySettings settings = m_blockModelSettings.value(layerName);
    LayerNode *layerNode = m_layerModel ? m_layerModel->findBlockModelLayerByName(layerName) : nullptr;
    const bool layerVisible = layerNode ? layerNode->isVisible() : true;
    if (!layerVisible) {
        if (blockIt->actor) {
            blockIt->actor->SetVisibility(0);
        }
        if (blockIt->sectionActor) {
            blockIt->sectionActor->SetVisibility(0);
        }
        if (blockIt->selectionActor) {
            blockIt->selectionActor->SetVisibility(0);
        }
        if (m_renderWindow) {
            m_renderWindow->Render();
        }
        return;
    }
    LayerNode *legendOwner = FindLegendOwnerBlockModel(layerNode);
    const QString legendLayerName = legendOwner ? legendOwner->name() : layerName;
    const BlockModelLegend *activeLegend = nullptr;
    if (!settings.blockLegend.isEmpty()) {
        for (const BlockModelLegend &legend : m_blockModelLegends) {
            if (legend.layerName.compare(legendLayerName, Qt::CaseInsensitive) == 0 &&
                legend.name.compare(settings.blockLegend, Qt::CaseInsensitive) == 0) {
                activeLegend = &legend;
                break;
            }
        }
    }
    const bool useLegend = activeLegend && !activeLegend->fieldName.isEmpty() && !activeLegend->bins.isEmpty();
    auto colorForBlockValue = [&](double value, bool hasValue) -> std::optional<QColor> {
        if (!useLegend) {
            return settings.blockColor;
        }
        if (!hasValue) {
            return std::nullopt;
        }
        for (const BlockModelLegendBin &bin : activeLegend->bins) {
            if (!bin.visible) {
                continue;
            }
            const double lo = std::min(bin.minValue, bin.maxValue);
            const double hi = std::max(bin.minValue, bin.maxValue);
            if (value >= lo && value <= hi) {
                return bin.color;
            }
        }
        return std::nullopt;
    };
    auto rebuildSectionActor = [&]() {
        if (blockIt->sectionSourceCells.empty() && !blockIt->sectionCells.empty()) {
            blockIt->sectionSourceCells = blockIt->sectionCells;
            blockIt->sectionSourcePolygons = blockIt->sectionPolygons;
            blockIt->sectionSourceRows = blockIt->sectionRows;
        }
        if (blockIt->sectionSourceCells.empty()) {
            return;
        }

        vtkNew<vtkPoints> points;
        vtkNew<vtkCellArray> polys;
        vtkNew<vtkUnsignedCharArray> colors;
        colors->SetName("SectionColor");
        colors->SetNumberOfComponents(3);

        blockIt->sectionCells.clear();
        blockIt->sectionPolygons.clear();
        blockIt->sectionRows.clear();

        std::vector<std::optional<double>> legendValues(blockIt->sectionSourceCells.size());
        std::vector<std::optional<visor::datamine::BlockCell>> fullCells(blockIt->sectionSourceCells.size());
        if (useLegend) {
            std::unordered_map<std::int64_t, std::size_t> sourceRowToIndex;
            sourceRowToIndex.reserve(blockIt->sectionSourceRows.size());
            for (std::size_t i = 0; i < blockIt->sectionSourceRows.size(); ++i) {
                sourceRowToIndex.emplace(blockIt->sectionSourceRows[i], i);
            }
            std::size_t remaining = sourceRowToIndex.size();
            try {
                visor::datamine::DmBlockModelImporter::forEachInternalCellValue(
                    blockIt->internalPath.toStdString(),
                    activeLegend->fieldName.toStdString(),
                    [&](const visor::datamine::BlockCell &fullCell, double value, bool hasValue, std::int64_t row, std::int64_t) {
                        auto it = sourceRowToIndex.find(row);
                        if (it != sourceRowToIndex.end()) {
                            if (hasValue) {
                                legendValues[it->second] = value;
                            }
                            fullCells[it->second] = fullCell;
                            sourceRowToIndex.erase(it);
                            --remaining;
                        }
                        return remaining > 0;
                    },
                    std::nullopt,
                    true);
            } catch (const std::exception &ex) {
                if (m_consoleLog) {
                    m_consoleLog->append(QString(">> Unable to read legend values: %1")
                                             .arg(QString::fromLocal8Bit(ex.what())));
                }
            }
        }

        for (std::size_t i = 0; i < blockIt->sectionSourceCells.size(); ++i) {
            double value = 0.0;
            bool hasValue = !useLegend;
            if (useLegend) {
                if (i < legendValues.size() && legendValues[i]) {
                    value = *legendValues[i];
                    hasValue = true;
                }
            }

            const std::optional<QColor> cellColor = colorForBlockValue(value, hasValue);
            if (!cellColor || i >= blockIt->sectionSourcePolygons.size()) {
                continue;
            }

            const auto &polygon = blockIt->sectionSourcePolygons[i];
            std::vector<vtkIdType> ids;
            ids.reserve(polygon.size());
            for (const auto &p : polygon) {
                ids.push_back(points->InsertNextPoint(p[0], p[1], p[2]));
            }
            polys->InsertNextCell(static_cast<vtkIdType>(ids.size()), ids.data());
            unsigned char tuple[3] = {
                static_cast<unsigned char>(std::clamp(cellColor->red(), 0, 255)),
                static_cast<unsigned char>(std::clamp(cellColor->green(), 0, 255)),
                static_cast<unsigned char>(std::clamp(cellColor->blue(), 0, 255))};
            colors->InsertNextTypedTuple(tuple);
            blockIt->sectionCells.push_back(blockIt->sectionSourceCells[i]);
            blockIt->sectionPolygons.push_back(polygon);
            std::int64_t sourceRow = static_cast<std::int64_t>(i);
            if (i < blockIt->sectionSourceRows.size()) {
                sourceRow = blockIt->sectionSourceRows[i];
            }
            blockIt->sectionRows.push_back(sourceRow);
            if (i < fullCells.size() && fullCells[i]) {
                blockIt->fullCellCache[sourceRow] = *fullCells[i];
            }
        }

        vtkSmartPointer<vtkPolyData> sectionData = vtkSmartPointer<vtkPolyData>::New();
        sectionData->SetPoints(points);
        sectionData->SetPolys(polys);
        sectionData->GetCellData()->SetScalars(colors);

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(sectionData);
        mapper->ScalarVisibilityOn();
        mapper->SetScalarModeToUseCellData();
        mapper->SetColorModeToDirectScalars();
        mapper->Update();

        if (blockIt->sectionActor) {
            m_renderer->RemoveActor(blockIt->sectionActor);
        }
        blockIt->sectionActor = vtkSmartPointer<vtkActor>::New();
        blockIt->sectionActor->SetMapper(mapper);
        blockIt->sectionActor->GetProperty()->SetRepresentationToWireframe();
        blockIt->sectionActor->GetProperty()->SetLineWidth(1.2);
        blockIt->sectionActor->GetProperty()->LightingOff();
        m_renderer->AddActor(blockIt->sectionActor);
        blockIt->renderDirty = false;
    };

    if (settings.renderMode == BlockModelRenderMode::Section2D) {
        if (blockIt->renderDirty) {
            rebuildSectionActor();
        }
        if (blockIt->actor) {
            blockIt->actor->SetVisibility(0);
        }
        if (blockIt->sectionActor) {
            blockIt->sectionActor->SetVisibility(settings.blocksEnabled ? 1 : 0);
        }
        if (m_renderWindow) {
            m_renderWindow->Render();
        }
        return;
    }
    if (blockIt->sectionActor) {
        blockIt->sectionActor->SetVisibility(0);
    }

    if (!settings.blocksEnabled) {
        if (blockIt->actor) {
            blockIt->actor->SetVisibility(0);
            if (m_renderWindow) {
                m_renderWindow->Render();
            }
        }
        return;
    }

    if (!blockIt->pointsData || blockIt->renderDirty) {
        vtkNew<vtkPoints> points;
        points->SetDataTypeToDouble();
        vtkNew<vtkDoubleArray> scales;
        scales->SetName("BlockScale");
        scales->SetNumberOfComponents(3);
        vtkNew<vtkUnsignedCharArray> colors;
        colors->SetName("BlockColor");
        colors->SetNumberOfComponents(3);
        const double scaleFactor = std::clamp(1.0 - (settings.blockGapPercent / 100.0), 0.05, 1.0);
        auto colorForValue = [&](double value, bool hasValue) -> std::optional<QColor> {
            if (!useLegend) {
                return settings.blockColor;
            }
            if (!hasValue) {
                return std::nullopt;
            }
            for (const BlockModelLegendBin &bin : activeLegend->bins) {
                if (!bin.visible) {
                    continue;
                }
                const double lo = std::min(bin.minValue, bin.maxValue);
                const double hi = std::max(bin.minValue, bin.maxValue);
                if (value >= lo && value <= hi) {
                    return bin.color;
                }
            }
            return std::nullopt;
        };
        auto appendColor = [&](const QColor &color) {
            unsigned char tuple[3] = {
                static_cast<unsigned char>(std::clamp(color.red(), 0, 255)),
                static_cast<unsigned char>(std::clamp(color.green(), 0, 255)),
                static_cast<unsigned char>(std::clamp(color.blue(), 0, 255))};
            colors->InsertNextTypedTuple(tuple);
        };
        double modelBounds[6];
        if (BlockModelBounds(blockIt->info, modelBounds)) {
            blockIt->renderOrigin = {
                0.5 * (modelBounds[0] + modelBounds[1]),
                0.5 * (modelBounds[2] + modelBounds[3]),
                0.5 * (modelBounds[4] + modelBounds[5])};
        } else {
            blockIt->renderOrigin = {0.0, 0.0, 0.0};
        }

        auto appendCell = [&](const visor::datamine::BlockCell &cell, double value, bool hasValue, std::int64_t row) {
            const std::optional<QColor> cellColor = colorForValue(value, hasValue);
            if (!cellColor) {
                return;
            }
            points->InsertNextPoint(
                cell.xc - blockIt->renderOrigin[0],
                cell.yc - blockIt->renderOrigin[1],
                cell.zc - blockIt->renderOrigin[2]);
            const double scale[3] = {
                std::max(cell.xinc * scaleFactor, 1e-9),
                std::max(cell.yinc * scaleFactor, 1e-9),
                std::max(cell.zinc * scaleFactor, 1e-9)};
            scales->InsertNextTuple(scale);
            if (useLegend) {
                appendColor(*cellColor);
            }
            blockIt->previewCells.push_back(cell);
            blockIt->previewRows.push_back(row);
        };

        std::int64_t scanned = 0;
        blockIt->previewCells.clear();
        blockIt->previewRows.clear();
        blockIt->fullCellCache.clear();
        if (blockIt->info.cellCount > 0) {
            const std::int64_t reserveCount = useLegend
                ? std::min<std::int64_t>(blockIt->info.cellCount, 1000000)
                : blockIt->info.cellCount;
            blockIt->previewCells.reserve(static_cast<std::size_t>(reserveCount));
        }
        try {
            if (useLegend) {
                visor::datamine::DmBlockModelImporter::forEachInternalCellValue(
                    blockIt->internalPath.toStdString(),
                    activeLegend->fieldName.toStdString(),
                    [&](const visor::datamine::BlockCell &cell, double value, bool hasValue, std::int64_t row, std::int64_t) {
                        appendCell(cell, value, hasValue, row);
                        ++scanned;
                        return true;
                    });
            } else {
                visor::datamine::DmBlockModelImporter::forEachInternalBaseCell(
                    blockIt->internalPath.toStdString(),
                    [&](const visor::datamine::BlockCell &cell, std::int64_t row, std::int64_t) {
                        appendCell(cell, 0.0, false, row);
                        ++scanned;
                        return true;
                    });
            }
        } catch (const std::exception &ex) {
            QMessageBox::warning(this, "Block Model", QString("Unable to render block model:\n%1").arg(QString::fromLocal8Bit(ex.what())));
            return;
        }

        vtkSmartPointer<vtkPolyData> pointData = vtkSmartPointer<vtkPolyData>::New();
        pointData->SetPoints(points);
        pointData->GetPointData()->AddArray(scales);
        pointData->GetPointData()->SetActiveVectors("BlockScale");
        if (useLegend) {
            pointData->GetPointData()->SetScalars(colors);
        }
        blockIt->pointsData = pointData;
        blockIt->renderDirty = false;

        if (m_consoleLog) {
            m_consoleLog->append(QString(">> Block model 3D render prepared: %1 visible of %2 scanned cells.")
                                     .arg(static_cast<qlonglong>(blockIt->previewCells.size()))
                                     .arg(static_cast<qlonglong>(scanned)));
            if (useLegend && blockIt->previewCells.empty()) {
                m_consoleLog->append(">> No blocks matched the selected legend bins.");
            }
        }
    }

    if (!blockIt->mapper) {
        vtkNew<vtkCubeSource> cube;
        cube->SetXLength(1.0);
        cube->SetYLength(1.0);
        cube->SetZLength(1.0);
        cube->Update();

        vtkSmartPointer<vtkGlyph3DMapper> mapper = vtkSmartPointer<vtkGlyph3DMapper>::New();
        mapper->SetSourceData(cube->GetOutput());
        mapper->SetInputData(blockIt->pointsData);
        mapper->SetScaleArray("BlockScale");
        mapper->SetScaleModeToScaleByVectorComponents();
        mapper->SetScaleFactor(1.0);
        mapper->ScalingOn();
        mapper->OrientOff();
        mapper->SetCullingAndLOD(false);
        blockIt->mapper = mapper;
    } else {
        blockIt->mapper->SetInputData(blockIt->pointsData);
    }
    if (useLegend) {
        blockIt->mapper->ScalarVisibilityOn();
        blockIt->mapper->SetScalarModeToUsePointData();
        blockIt->mapper->SetColorModeToDirectScalars();
    } else {
        blockIt->mapper->ScalarVisibilityOff();
    }
    blockIt->mapper->Update();

    if (!blockIt->actor) {
        vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(blockIt->mapper);
        m_renderer->AddActor(actor);
        blockIt->actor = actor;
    }

    const QColor color = settings.blockColor;
    blockIt->actor->GetProperty()->SetColor(color.redF(), color.greenF(), color.blueF());
    blockIt->actor->GetProperty()->SetOpacity(1.0);
    blockIt->actor->GetProperty()->SetRepresentationToSurface();
    blockIt->actor->GetProperty()->LightingOff();
    blockIt->actor->GetProperty()->SetAmbient(1.0);
    blockIt->actor->GetProperty()->SetDiffuse(0.0);
    blockIt->actor->GetProperty()->SetSpecular(0.0);
    blockIt->actor->GetProperty()->BackfaceCullingOff();
    blockIt->actor->GetProperty()->FrontfaceCullingOff();
    blockIt->actor->SetPosition(
        blockIt->renderOrigin[0],
        blockIt->renderOrigin[1],
        blockIt->renderOrigin[2]);
    blockIt->actor->SetVisibility(settings.blocksEnabled ? 1 : 0);

    if (!m_hasScene) {
        m_renderer->ResetCamera();
        if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
            camera->ParallelProjectionOn();
        }
        m_hasScene = true;
    } else {
        m_renderer->ResetCameraClippingRange();
    }
    if (m_renderWindow) {
        m_renderWindow->Render();
    }
}

void MainWindow::SetViewMode3D()
{
    m_viewMode = ViewMode::View3D;
    if (vtkCamera *camera = m_renderer ? m_renderer->GetActiveCamera() : nullptr) {
        camera->ParallelProjectionOn();
        camera->Azimuth(45.0);
        camera->Elevation(30.0);
        camera->OrthogonalizeViewUp();
        m_renderer->ResetCameraClippingRange();
    }
    if (m_renderWindow) {
        m_renderWindow->Render();
    }
}

void MainWindow::SetViewMode2D()
{
    m_viewMode = ViewMode::View2D;
    if (vtkCamera *camera = m_renderer ? m_renderer->GetActiveCamera() : nullptr) {
        camera->ParallelProjectionOn();
        camera->SetViewUp(0.0, 1.0, 0.0);
        camera->SetPosition(0.0, 0.0, 1.0);
        camera->SetFocalPoint(0.0, 0.0, 0.0);
        m_renderer->ResetCameraClippingRange();
    }
    if (m_renderWindow) {
        m_renderWindow->Render();
    }
}

void MainWindow::OnOpenRouteSetup()
{
    RouteSetupDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted && m_consoleLog) {
        m_consoleLog->append(">> Route setup parameters accepted.");
    }
}

void MainWindow::OnCalculateRoutes()
{
    if (g_routeSetupState.starts.empty() && g_routeSetupState.mains.empty() && g_routeSetupState.ends.empty()) {
        QMessageBox::information(this, "Calculate Routes", "No routes configured. Use Setup first.");
        return;
    }
    if (g_routeSetupState.loadedSpeeds.empty() || g_routeSetupState.unloadedSpeeds.empty()) {
        QMessageBox::warning(this, "Calculate Routes", "Define speed bands in Setup before calculating.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Calculate Routes");
    dialog.resize(920, 500);
    QVBoxLayout *root = new QVBoxLayout(&dialog);
    QTableWidget *table = new QTableWidget(12, 4, &dialog);
    table->setHorizontalHeaderLabels({"Start", "Main", "Ends", "Name"});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    root->addWidget(table, 1);

    QStringList starts;
    QStringList mains;
    QStringList ends;
    starts << "";
    mains << "";
    ends << "";
    for (const auto &entry : g_routeSetupState.starts) {
        starts << entry.displayName;
    }
    for (const auto &entry : g_routeSetupState.mains) {
        mains << entry.displayName;
    }
    for (const auto &entry : g_routeSetupState.ends) {
        ends << entry.displayName;
    }
    starts.removeDuplicates();
    mains.removeDuplicates();
    ends.removeDuplicates();

    for (int row = 0; row < table->rowCount(); ++row) {
        QComboBox *startCombo = new QComboBox(table);
        QComboBox *mainCombo = new QComboBox(table);
        QComboBox *endCombo = new QComboBox(table);
        startCombo->addItems(starts);
        mainCombo->addItems(mains);
        endCombo->addItems(ends);
        if (row < static_cast<int>(g_routeSetupState.calcRows.size())) {
            const auto &saved = g_routeSetupState.calcRows[static_cast<std::size_t>(row)];
            int idx = startCombo->findText(saved.startDisplay);
            if (idx >= 0) {
                startCombo->setCurrentIndex(idx);
            }
            idx = mainCombo->findText(saved.mainDisplay);
            if (idx >= 0) {
                mainCombo->setCurrentIndex(idx);
            }
            idx = endCombo->findText(saved.endDisplay);
            if (idx >= 0) {
                endCombo->setCurrentIndex(idx);
            }
        }
        table->setCellWidget(row, 0, startCombo);
        table->setCellWidget(row, 1, mainCombo);
        table->setCellWidget(row, 2, endCombo);
        const QString savedName = (row < static_cast<int>(g_routeSetupState.calcRows.size()))
            ? g_routeSetupState.calcRows[static_cast<std::size_t>(row)].name
            : QString();
        table->setItem(row, 3, new QTableWidgetItem(savedName));
    }

    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    buttons->addButton("Accept", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    std::vector<RouteCalcInput> rows;
    std::vector<RoutePersistentCalcRow> savedRows;
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *start = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
        auto *main = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
        auto *end = qobject_cast<QComboBox *>(table->cellWidget(row, 2));
        const QString startText = start ? start->currentText().trimmed() : QString();
        const QString mainText = main ? main->currentText().trimmed() : QString();
        const QString endText = end ? end->currentText().trimmed() : QString();
        if (startText.isEmpty() && mainText.isEmpty() && endText.isEmpty()) {
            continue;
        }
        QString name = table->item(row, 3) ? table->item(row, 3)->text().trimmed() : QString();
        if (name.isEmpty()) {
            QStringList parts;
            if (!startText.isEmpty()) {
                parts << startText;
            }
            if (!mainText.isEmpty()) {
                parts << mainText;
            }
            if (!endText.isEmpty()) {
                parts << endText;
            }
            name = parts.join(" + ");
        }
        savedRows.push_back({startText, mainText, endText, name});
        rows.push_back({startText, mainText, endText, name});
    }

    if (rows.empty()) {
        QMessageBox::warning(this, "Calculate Routes", "Configure at least one route row.");
        return;
    }
    g_routeSetupState.calcRows = std::move(savedRows);

    auto findEntry = [](const std::vector<RoutePersistentEntry> &entries, const QString &displayName) -> const RoutePersistentEntry * {
        if (displayName.isEmpty()) {
            return nullptr;
        }
        for (const auto &entry : entries) {
            if (entry.displayName == displayName) {
                return &entry;
            }
        }
        return nullptr;
    };

    auto samePoint = [](const DxfPoint3 &a, const DxfPoint3 &b) -> bool {
        return std::abs(a[0] - b[0]) < 1e-9 &&
            std::abs(a[1] - b[1]) < 1e-9 &&
            std::abs(a[2] - b[2]) < 1e-9;
    };

    auto appendEntry = [&](std::vector<DxfPoint3> &dest, const RoutePersistentEntry *entry) {
        if (!entry) {
            return;
        }
        for (const auto &poly : entry->polylines) {
            for (std::size_t i = 0; i < poly.size(); ++i) {
                const DxfPoint3 &p = poly[i];
                if (!dest.empty() && i == 0 && samePoint(dest.back(), p)) {
                    continue;
                }
                dest.push_back(p);
            }
        }
    };

    auto computeLeg = [&](const std::vector<DxfPoint3> &points, bool loaded) -> std::pair<double, double> {
        if (points.size() < 2) {
            return {0.0, 0.0};
        }
        const auto &bands = loaded ? g_routeSetupState.loadedSpeeds : g_routeSetupState.unloadedSpeeds;
        const double accel = std::max(loaded ? g_routeSetupState.accelLoaded : g_routeSetupState.accelUnloaded, 1e-4);
        const double decel = std::max(loaded ? g_routeSetupState.decelLoaded : g_routeSetupState.decelUnloaded, 1e-4);

        double totalDistanceM = 0.0;
        double totalTimeSec = 0.0;
        double currentSpeed = 0.0;

        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            const DxfPoint3 &a = points[i];
            const DxfPoint3 &b = points[i + 1];
            const double dx = b[0] - a[0];
            const double dy = b[1] - a[1];
            const double dz = b[2] - a[2];
            const double horiz = std::sqrt(dx * dx + dy * dy);
            const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist <= 1e-6) {
                continue;
            }
            totalDistanceM += dist;

            const double slope = (horiz > 1e-6) ? (dz / horiz) * 100.0 : (dz >= 0.0 ? 999.0 : -999.0);
            double targetSpeed = LookupSpeedMpsClosest(bands, slope);
            if (targetSpeed <= 0.0) {
                targetSpeed = std::max(currentSpeed, 0.5);
            }

            double nextTarget = 0.0;
            if (i + 2 < points.size()) {
                const DxfPoint3 &c = points[i + 2];
                const double ndx = c[0] - b[0];
                const double ndy = c[1] - b[1];
                const double ndz = c[2] - b[2];
                const double nh = std::sqrt(ndx * ndx + ndy * ndy);
                const double nslope = (nh > 1e-6) ? (ndz / nh) * 100.0 : (ndz >= 0.0 ? 999.0 : -999.0);
                nextTarget = LookupSpeedMpsClosest(bands, nslope);
            }
            const double vmax = std::max(targetSpeed, 0.1);
            const double vend = std::max(0.0, std::min(vmax, nextTarget));

            double sAcc = 0.0;
            double tAcc = 0.0;
            if (vmax > currentSpeed) {
                sAcc = (vmax * vmax - currentSpeed * currentSpeed) / (2.0 * accel);
                tAcc = (vmax - currentSpeed) / accel;
            } else if (vmax < currentSpeed) {
                sAcc = (currentSpeed * currentSpeed - vmax * vmax) / (2.0 * decel);
                tAcc = (currentSpeed - vmax) / decel;
            }

            double sDec = 0.0;
            double tDec = 0.0;
            if (vmax > vend) {
                sDec = (vmax * vmax - vend * vend) / (2.0 * decel);
                tDec = (vmax - vend) / decel;
            }

            if ((sAcc + sDec) <= dist && vmax > 1e-4) {
                const double sCruise = dist - sAcc - sDec;
                totalTimeSec += tAcc + (sCruise / vmax) + tDec;
                currentSpeed = vend;
            } else {
                double endSpeed = currentSpeed;
                if (vmax > currentSpeed) {
                    endSpeed = std::min(vmax, std::sqrt(currentSpeed * currentSpeed + 2.0 * accel * dist));
                } else if (vmax < currentSpeed) {
                    const double sq = std::max(currentSpeed * currentSpeed - 2.0 * decel * dist, 0.0);
                    endSpeed = std::max(vmax, std::sqrt(sq));
                }

                const double avgSpeed = 0.5 * (currentSpeed + endSpeed);
                totalTimeSec += dist / std::max(avgSpeed, 0.1);
                currentSpeed = endSpeed;
            }
        }

        return {totalTimeSec / 60.0, totalDistanceM / 1000.0};
    };

    std::vector<RouteCalcResult> results;
    results.reserve(rows.size());

    QProgressDialog progress("Calculating routes...", "Cancel", 0, static_cast<int>(rows.size()), this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.show();

    for (std::size_t i = 0; i < rows.size(); ++i) {
        progress.setValue(static_cast<int>(i));
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            break;
        }

        const RouteCalcInput &row = rows[i];
        std::vector<DxfPoint3> routePoints;
        appendEntry(routePoints, findEntry(g_routeSetupState.starts, row.startDisplay));
        appendEntry(routePoints, findEntry(g_routeSetupState.mains, row.mainDisplay));
        appendEntry(routePoints, findEntry(g_routeSetupState.ends, row.endDisplay));
        if (routePoints.size() < 2) {
            continue;
        }

        auto outLeg = computeLeg(routePoints, true);
        std::reverse(routePoints.begin(), routePoints.end());
        auto backLeg = computeLeg(routePoints, false);
        results.push_back({row.name, outLeg.first, outLeg.second, backLeg.first, backLeg.second});
    }
    progress.setValue(static_cast<int>(rows.size()));

    if (results.empty()) {
        QMessageBox::information(this, "Calculate Routes", "No valid route combinations to calculate.");
        return;
    }

    QDialog resultDialog(this);
    resultDialog.setWindowTitle("Route Results");
    resultDialog.resize(860, 420);
    QVBoxLayout *resultLayout = new QVBoxLayout(&resultDialog);
    QTableWidget *resultTable = new QTableWidget(static_cast<int>(results.size()), 5, &resultDialog);
    resultTable->setHorizontalHeaderLabels(
        {"Name", "Outbound Time (min)", "Return Time (min)", "Distance (km)", "Total Time (min)"});
    resultTable->verticalHeader()->setVisible(false);
    resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTable->setSelectionMode(QAbstractItemView::SingleSelection);

    for (int row = 0; row < static_cast<int>(results.size()); ++row) {
        const auto &r = results[static_cast<std::size_t>(row)];
        const double totalTime = r.outMin + r.backMin;
        resultTable->setItem(row, 0, new QTableWidgetItem(r.name));
        resultTable->setItem(row, 1, new QTableWidgetItem(QString::number(r.outMin, 'f', 3)));
        resultTable->setItem(row, 2, new QTableWidgetItem(QString::number(r.backMin, 'f', 3)));
        resultTable->setItem(row, 3, new QTableWidgetItem(QString::number(r.outKm, 'f', 3)));
        resultTable->setItem(row, 4, new QTableWidgetItem(QString::number(totalTime, 'f', 3)));
    }

    resultLayout->addWidget(resultTable, 1);
    QDialogButtonBox *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &resultDialog);
    connect(closeButtons, &QDialogButtonBox::rejected, &resultDialog, &QDialog::reject);
    connect(closeButtons, &QDialogButtonBox::accepted, &resultDialog, &QDialog::accept);
    resultLayout->addWidget(closeButtons);
    resultDialog.exec();
}

void MainWindow::OnVisualizeRoutes()
{
    if (g_routeSetupState.calcRows.empty()) {
        QMessageBox::information(this, "Visualize Routes", "No calculated routes found. Run Calculate first.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Visualize Routes");
    dialog.resize(520, 430);
    dialog.setMinimumSize(420, 400);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *title = new QLabel("Select one route to visualize:", &dialog);
    layout->addWidget(title);

    QWidget *listHost = new QWidget(&dialog);
    QVBoxLayout *listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);
    QButtonGroup *group = new QButtonGroup(&dialog);
    group->setExclusive(true);
    std::vector<int> rowMap;
    rowMap.reserve(g_routeSetupState.calcRows.size());
    for (int i = 0; i < static_cast<int>(g_routeSetupState.calcRows.size()); ++i) {
        const auto &row = g_routeSetupState.calcRows[static_cast<std::size_t>(i)];
        if (row.name.trimmed().isEmpty()) {
            continue;
        }
        QRadioButton *radio = new QRadioButton(row.name, listHost);
        if (rowMap.empty()) {
            radio->setChecked(true);
        }
        group->addButton(radio, static_cast<int>(rowMap.size()));
        rowMap.push_back(i);
        listLayout->addWidget(radio);
    }
    listLayout->addStretch(1);
    layout->addWidget(listHost, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    buttons->addButton("Create", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (rowMap.empty()) {
        QMessageBox::warning(this, "Visualize Routes", "No valid route names found.");
        return;
    }

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int selectedId = group->checkedId();
    if (selectedId < 0 || selectedId >= static_cast<int>(rowMap.size())) {
        return;
    }

    const RoutePersistentCalcRow &selectedRow =
        g_routeSetupState.calcRows[static_cast<std::size_t>(rowMap[static_cast<std::size_t>(selectedId)])];

    auto findEntry = [](const std::vector<RoutePersistentEntry> &entries, const QString &displayName) -> const RoutePersistentEntry * {
        if (displayName.isEmpty()) {
            return nullptr;
        }
        for (const auto &entry : entries) {
            if (entry.displayName == displayName) {
                return &entry;
            }
        }
        return nullptr;
    };

    auto samePoint = [](const DxfPoint3 &a, const DxfPoint3 &b) -> bool {
        return std::abs(a[0] - b[0]) < 1e-9 &&
            std::abs(a[1] - b[1]) < 1e-9 &&
            std::abs(a[2] - b[2]) < 1e-9;
    };

    auto appendEntry = [&](std::vector<DxfPoint3> &dest, const RoutePersistentEntry *entry) {
        if (!entry) {
            return;
        }
        for (const auto &poly : entry->polylines) {
            for (std::size_t i = 0; i < poly.size(); ++i) {
                const DxfPoint3 &p = poly[i];
                if (!dest.empty() && i == 0 && samePoint(dest.back(), p)) {
                    continue;
                }
                dest.push_back(p);
            }
        }
    };

    std::vector<DxfPoint3> routePoints;
    appendEntry(routePoints, findEntry(g_routeSetupState.starts, selectedRow.startDisplay));
    appendEntry(routePoints, findEntry(g_routeSetupState.mains, selectedRow.mainDisplay));
    appendEntry(routePoints, findEntry(g_routeSetupState.ends, selectedRow.endDisplay));
    if (routePoints.size() < 2) {
        QMessageBox::warning(this, "Visualize Routes", "Selected route has insufficient geometry.");
        return;
    }

    StopRouteVisualization(true);
    m_routeVisualSegments.clear();
    m_routeSegmentIndex = 0;
    m_routeDistanceInSegment = 0.0;

    double currentSpeed = 0.0;
    const double accel = std::max(g_routeSetupState.accelLoaded, 1e-4);
    const double decel = std::max(g_routeSetupState.decelLoaded, 1e-4);
    for (std::size_t i = 0; i + 1 < routePoints.size(); ++i) {
        const DxfPoint3 &a = routePoints[i];
        const DxfPoint3 &b = routePoints[i + 1];
        const double dx = b[0] - a[0];
        const double dy = b[1] - a[1];
        const double dz = b[2] - a[2];
        const double horiz = std::sqrt(dx * dx + dy * dy);
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length <= 1e-8) {
            continue;
        }
        const double slopePct = (horiz > 1e-8) ? (dz / horiz) * 100.0 : (dz >= 0.0 ? 999.0 : -999.0);
        const double targetSpeed = std::max(LookupSpeedMpsClosest(g_routeSetupState.loadedSpeeds, slopePct), 0.1);

        double endSpeed = targetSpeed;
        if (targetSpeed > currentSpeed) {
            endSpeed = std::min(targetSpeed, std::sqrt(currentSpeed * currentSpeed + 2.0 * accel * length));
        } else {
            const double sq = std::max(currentSpeed * currentSpeed - 2.0 * decel * length, 0.0);
            endSpeed = std::max(targetSpeed, std::sqrt(sq));
        }
        const double segmentSpeed = std::max((currentSpeed + endSpeed) * 0.5, 0.1);
        currentSpeed = endSpeed;

        RouteVisualSegment seg;
        seg.length = length;
        seg.speedMps = segmentSpeed;
        seg.slopePct = slopePct;
        seg.start[0] = a[0];
        seg.start[1] = a[1];
        seg.start[2] = a[2];
        seg.end[0] = b[0];
        seg.end[1] = b[1];
        seg.end[2] = b[2];
        m_routeVisualSegments.push_back(seg);
    }

    if (m_routeVisualSegments.empty()) {
        QMessageBox::warning(this, "Visualize Routes", "Unable to build simulation segments.");
        return;
    }

    vtkNew<vtkPoints> pathPoints;
    vtkNew<vtkCellArray> pathLines;
    pathLines->InsertNextCell(static_cast<vtkIdType>(routePoints.size()));
    for (const auto &pt : routePoints) {
        const vtkIdType id = pathPoints->InsertNextPoint(pt[0], pt[1], pt[2]);
        pathLines->InsertCellPoint(id);
    }
    vtkNew<vtkPolyData> pathData;
    pathData->SetPoints(pathPoints);
    pathData->SetLines(pathLines);

    vtkNew<vtkPolyDataMapper> pathMapper;
    pathMapper->SetInputData(pathData);
    m_routePathActor = vtkSmartPointer<vtkActor>::New();
    m_routePathActor->SetMapper(pathMapper);
    m_routePathActor->GetProperty()->SetColor(1.0, 0.92, 0.2);
    m_routePathActor->GetProperty()->SetLineWidth(2.0);
    m_renderer->AddActor(m_routePathActor);

    const QString truckPath = FindAssetPath("assets/routes/CAT_Truck_795F.obj");
    if (!truckPath.isEmpty()) {
        vtkNew<vtkOBJReader> truckReader;
        truckReader->SetFileName(truckPath.toStdString().c_str());
        truckReader->Update();
        if (truckReader->GetOutput() && truckReader->GetOutput()->GetNumberOfPoints() > 0) {
            vtkNew<vtkPolyDataMapper> truckMapper;
            truckMapper->SetInputConnection(truckReader->GetOutputPort());
            m_routeTruckActor = vtkSmartPointer<vtkActor>::New();
            m_routeTruckActor->SetMapper(truckMapper);
            m_routeTruckActor->GetProperty()->SetColor(0.93, 0.93, 0.95);

            double routeBounds[6];
            pathData->GetBounds(routeBounds);
            const double rdx = routeBounds[1] - routeBounds[0];
            const double rdy = routeBounds[3] - routeBounds[2];
            const double rdz = routeBounds[5] - routeBounds[4];
            const double routeDiag = std::max(std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz), 1e-6);

            double truckBounds[6];
            truckReader->GetOutput()->GetBounds(truckBounds);
            const double tdx = truckBounds[1] - truckBounds[0];
            const double tdy = truckBounds[3] - truckBounds[2];
            const double tdz = truckBounds[5] - truckBounds[4];
            const double truckDiagMm = std::max(std::sqrt(tdx * tdx + tdy * tdy + tdz * tdz), 1e-6);
            constexpr double kMmToM = 0.001;
            const double truckDiagM = std::max(truckDiagMm * kMmToM, 1e-9);
            const double fitScale = std::clamp((routeDiag * 0.03) / truckDiagM, 0.01, 1000.0);
            const double totalScale = fitScale * kMmToM;
            m_routeTruckActor->SetScale(totalScale, totalScale, totalScale);
            m_routeTruckActor->SetPosition(routePoints.front()[0], routePoints.front()[1], routePoints.front()[2]);
            m_renderer->AddActor(m_routeTruckActor);
        }
    }

    if (!m_routeTimer) {
        m_routeTimer = new QTimer(this);
        m_routeTimer->setInterval(30);
        connect(m_routeTimer, &QTimer::timeout, this, &MainWindow::StepRouteVisualization);
    }

    BuildRouteControlOverlay();
    ShowRouteControlOverlay(true);
    m_routePlaybackMultiplier = 1.0;
    if (m_routeSpeed1xBtn) {
        m_routeSpeed1xBtn->setChecked(true);
    }

    m_routeSegmentIndex = 0;
    m_routeDistanceInSegment = 0.0;
    m_routeTimer->start();

    if (m_renderWindow) {
        m_renderer->ResetCameraClippingRange();
        m_renderWindow->Render();
    }
    if (m_consoleLog) {
        m_consoleLog->append(QString(">> Route visualization started: %1").arg(selectedRow.name));
    }
}

void MainWindow::BuildRouteControlOverlay()
{
    if (!m_vtkWidget || m_routeControlOverlay) {
        return;
    }

    m_routeControlOverlay = new QWidget(m_vtkWidget);
    m_routeControlOverlay->setStyleSheet(
        "QWidget { background: rgba(35,35,35,220); border: 1px solid #4a4a4a; border-radius: 4px; }"
        "QToolButton { background: transparent; border: 1px solid #575757; padding: 3px; border-radius: 3px; }"
        "QToolButton:hover { background: #3a3a3a; }"
        "QToolButton:checked { background: #4a4a4a; border: 1px solid #7a7a7a; }"
        "QLabel { color: #d7d7d7; border: none; }");

    QHBoxLayout *layout = new QHBoxLayout(m_routeControlOverlay);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    QToolButton *playBtn = new QToolButton(m_routeControlOverlay);
    playBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playBtn->setToolTip("Play");
    connect(playBtn, &QToolButton::clicked, this, [this]() {
        if (m_routeTimer && !m_routeVisualSegments.empty()) {
            m_routeTimer->start();
        }
    });

    QToolButton *pauseBtn = new QToolButton(m_routeControlOverlay);
    pauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    pauseBtn->setToolTip("Pause");
    connect(pauseBtn, &QToolButton::clicked, this, [this]() {
        if (m_routeTimer) {
            m_routeTimer->stop();
        }
    });

    QToolButton *stopBtn = new QToolButton(m_routeControlOverlay);
    stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    stopBtn->setToolTip("Stop");
    connect(stopBtn, &QToolButton::clicked, this, [this]() {
        StopRouteVisualization(true);
    });

    m_routeSpeed1xBtn = new QToolButton(m_routeControlOverlay);
    m_routeSpeed1xBtn->setText("x1");
    m_routeSpeed1xBtn->setCheckable(true);
    m_routeSpeed1xBtn->setToolTip("Playback speed x1");
    connect(m_routeSpeed1xBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_routePlaybackMultiplier = 1.0;
        }
    });

    m_routeSpeed10xBtn = new QToolButton(m_routeControlOverlay);
    m_routeSpeed10xBtn->setText("x10");
    m_routeSpeed10xBtn->setCheckable(true);
    m_routeSpeed10xBtn->setToolTip("Playback speed x10");
    connect(m_routeSpeed10xBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_routePlaybackMultiplier = 10.0;
        }
    });

    m_routeSpeed20xBtn = new QToolButton(m_routeControlOverlay);
    m_routeSpeed20xBtn->setText("x20");
    m_routeSpeed20xBtn->setCheckable(true);
    m_routeSpeed20xBtn->setToolTip("Playback speed x20");
    connect(m_routeSpeed20xBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_routePlaybackMultiplier = 20.0;
        }
    });

    m_routeFollowBtn = new QToolButton(m_routeControlOverlay);
    m_routeFollowBtn->setCheckable(true);
    m_routeFollowBtn->setToolTip("Follow truck");
    {
        QPixmap px(16, 16);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(220, 220, 220), 1.4));
        p.drawEllipse(QPointF(8.0, 8.0), 5.2, 5.2);
        p.drawLine(QPointF(8.0, 1.5), QPointF(8.0, 4.0));
        p.drawLine(QPointF(8.0, 12.0), QPointF(8.0, 14.5));
        p.drawLine(QPointF(1.5, 8.0), QPointF(4.0, 8.0));
        p.drawLine(QPointF(12.0, 8.0), QPointF(14.5, 8.0));
        p.setBrush(QColor(220, 220, 220));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(8.0, 8.0), 1.2, 1.2);
        p.end();
        m_routeFollowBtn->setIcon(QIcon(px));
        m_routeFollowBtn->setIconSize(QSize(16, 16));
    }
    connect(m_routeFollowBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_routeFollowTruck = checked;
    });

    QToolButton *profileBtn = new QToolButton(m_routeControlOverlay);
    profileBtn->setToolTip("Route profile");
    {
        QPixmap px(16, 16);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(220, 220, 220), 1.4));
        p.drawLine(QPointF(1.5, 13.5), QPointF(14.5, 13.5));
        p.drawLine(QPointF(1.5, 13.5), QPointF(1.5, 1.5));
        p.setPen(QPen(QColor(90, 220, 255), 1.8));
        p.drawLine(QPointF(2.5, 11.5), QPointF(6.0, 9.0));
        p.drawLine(QPointF(6.0, 9.0), QPointF(9.5, 10.0));
        p.drawLine(QPointF(9.5, 10.0), QPointF(13.5, 4.0));
        p.end();
        profileBtn->setIcon(QIcon(px));
        profileBtn->setIconSize(QSize(16, 16));
    }
    connect(profileBtn, &QToolButton::clicked, this, &MainWindow::ShowRouteProfileDialog);

    QButtonGroup *speedGroup = new QButtonGroup(m_routeControlOverlay);
    speedGroup->setExclusive(true);
    speedGroup->addButton(m_routeSpeed1xBtn);
    speedGroup->addButton(m_routeSpeed10xBtn);
    speedGroup->addButton(m_routeSpeed20xBtn);
    m_routeSpeed1xBtn->setChecked(true);

    m_routeSpeedLabel = new QLabel("Speed: 0.00 km/h", m_routeControlOverlay);
    m_routeSlopeLabel = new QLabel("Slope: 0.00 %", m_routeControlOverlay);

    layout->addWidget(playBtn);
    layout->addWidget(pauseBtn);
    layout->addWidget(stopBtn);
    layout->addSpacing(4);
    layout->addWidget(m_routeSpeed1xBtn);
    layout->addWidget(m_routeSpeed10xBtn);
    layout->addWidget(m_routeSpeed20xBtn);
    layout->addWidget(m_routeFollowBtn);
    layout->addWidget(profileBtn);
    layout->addSpacing(8);
    layout->addWidget(m_routeSpeedLabel);
    layout->addWidget(m_routeSlopeLabel);
    m_routeControlOverlay->adjustSize();
    m_routeControlOverlay->hide();
}

void MainWindow::ShowRouteProfileDialog()
{
    if (m_routeVisualSegments.empty()) {
        QMessageBox::information(this, "Route Profile", "No active route visualization.");
        return;
    }

    std::vector<std::pair<double, double>> profileSegments;
    {
        constexpr double kSmoothStepM = 100.0;
        double totalDistance = 0.0;
        for (const auto &segment : m_routeVisualSegments) {
            totalDistance += std::max(segment.length, 0.0);
        }
        if (totalDistance <= 1e-9) {
            QMessageBox::information(this, "Route Profile", "Route has no measurable length.");
            return;
        }

        profileSegments.clear();
        std::size_t segmentIndex = 0;
        double segmentOffset = 0.0;
        double covered = 0.0;
        while (covered < totalDistance - 1e-9 && segmentIndex < m_routeVisualSegments.size()) {
            const double binLength = std::min(kSmoothStepM, totalDistance - covered);
            double binUsed = 0.0;
            double slopeWeighted = 0.0;

            while (binUsed < binLength - 1e-9 && segmentIndex < m_routeVisualSegments.size()) {
                const RouteVisualSegment &seg = m_routeVisualSegments[segmentIndex];
                const double segLen = std::max(seg.length, 0.0);
                if (segLen <= 1e-9) {
                    ++segmentIndex;
                    segmentOffset = 0.0;
                    continue;
                }

                const double segRemaining = segLen - segmentOffset;
                const double take = std::min(segRemaining, binLength - binUsed);
                slopeWeighted += seg.slopePct * take;
                binUsed += take;
                segmentOffset += take;

                if (segmentOffset >= segLen - 1e-9) {
                    ++segmentIndex;
                    segmentOffset = 0.0;
                }
            }

            if (binUsed > 1e-9) {
                profileSegments.emplace_back(binUsed, slopeWeighted / binUsed);
                covered += binUsed;
            } else {
                break;
            }
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Route Profile");
    dialog.resize(860, 460);
    dialog.setMinimumSize(760, 420);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    RouteProfileChartWidget *chart = new RouteProfileChartWidget(profileSegments, &dialog);
    layout->addWidget(chart, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::ShowRouteControlOverlay(bool visible)
{
    if (!m_routeControlOverlay || !m_vtkWidget) {
        return;
    }
    if (visible) {
        m_routeControlOverlay->adjustSize();
        const int x = m_vtkWidget->width() - m_routeControlOverlay->width() - 14;
        const int y = 12;
        m_routeControlOverlay->move(std::max(8, x), y);
        m_routeControlOverlay->show();
        m_routeControlOverlay->raise();
    } else {
        m_routeControlOverlay->hide();
    }
}

void MainWindow::StopRouteVisualization(bool removeActors)
{
    if (m_routeTimer) {
        m_routeTimer->stop();
    }
    if (removeActors && m_renderer) {
        if (m_routePathActor) {
            m_renderer->RemoveActor(m_routePathActor);
            m_routePathActor = nullptr;
        }
        if (m_routeTruckActor) {
            m_renderer->RemoveActor(m_routeTruckActor);
            m_routeTruckActor = nullptr;
        }
    }
    m_routeVisualSegments.clear();
    m_routeSegmentIndex = 0;
    m_routeDistanceInSegment = 0.0;
    m_routePlaybackMultiplier = 1.0;
    if (m_routeSpeed1xBtn) {
        m_routeSpeed1xBtn->setChecked(true);
    }
    if (m_routeSpeedLabel) {
        m_routeSpeedLabel->setText("Speed: 0.00 km/h");
    }
    if (m_routeSlopeLabel) {
        m_routeSlopeLabel->setText("Slope: 0.00 %");
    }
    ShowRouteControlOverlay(false);
    if (m_renderWindow) {
        m_renderWindow->Render();
    }
}

void MainWindow::StepRouteVisualization()
{
    if (m_routeVisualSegments.empty()) {
        if (m_routeTimer) {
            m_routeTimer->stop();
        }
        return;
    }

    double dt = (m_routeTimer ? m_routeTimer->interval() : 30) / 1000.0;
    dt *= std::max(m_routePlaybackMultiplier, 0.01);
    while (dt > 1e-9 && m_routeSegmentIndex < static_cast<int>(m_routeVisualSegments.size())) {
        RouteVisualSegment &seg = m_routeVisualSegments[static_cast<std::size_t>(m_routeSegmentIndex)];
        const double remaining = std::max(seg.length - m_routeDistanceInSegment, 0.0);
        const double speed = std::max(seg.speedMps, 0.01);
        const double travel = speed * dt;
        if (travel < remaining) {
            m_routeDistanceInSegment += travel;
            dt = 0.0;
        } else {
            dt -= remaining / speed;
            m_routeSegmentIndex++;
            m_routeDistanceInSegment = 0.0;
        }
    }

    if (m_routeSegmentIndex >= static_cast<int>(m_routeVisualSegments.size())) {
        if (m_routeTimer) {
            m_routeTimer->stop();
        }
        if (m_routeSpeedLabel) {
            m_routeSpeedLabel->setText("Speed: 0.00 km/h");
        }
        if (m_routeSlopeLabel) {
            m_routeSlopeLabel->setText("Slope: 0.00 %");
        }
        return;
    }

    const RouteVisualSegment &seg = m_routeVisualSegments[static_cast<std::size_t>(m_routeSegmentIndex)];
    const double t = (seg.length > 1e-9) ? std::clamp(m_routeDistanceInSegment / seg.length, 0.0, 1.0) : 0.0;
    const double x = seg.start[0] + (seg.end[0] - seg.start[0]) * t;
    const double y = seg.start[1] + (seg.end[1] - seg.start[1]) * t;
    const double z = seg.start[2] + (seg.end[2] - seg.start[2]) * t;

    if (m_routeTruckActor) {
        m_routeTruckActor->SetPosition(x, y, z);
        const double dx = seg.end[0] - seg.start[0];
        const double dy = seg.end[1] - seg.start[1];
        const double dz = seg.end[2] - seg.start[2];
        const double yawDeg = vtkMath::DegreesFromRadians(std::atan2(dy, dx));
        const double horiz = std::sqrt(dx * dx + dy * dy);
        const double pitchDeg = vtkMath::DegreesFromRadians(std::atan2(dz, std::max(horiz, 1e-9)));
        m_routeTruckActor->SetOrientation(0.0, -pitchDeg, yawDeg + 180.0);

        if (m_routeFollowTruck && m_renderer) {
            if (vtkCamera *camera = m_renderer->GetActiveCamera()) {
                double camPos[3];
                double camFocal[3];
                camera->GetPosition(camPos);
                camera->GetFocalPoint(camFocal);
                const double shift[3] = {x - camFocal[0], y - camFocal[1], z - camFocal[2]};
                camera->SetFocalPoint(x, y, z);
                camera->SetPosition(camPos[0] + shift[0], camPos[1] + shift[1], camPos[2] + shift[2]);
                camera->ParallelProjectionOn();
                m_renderer->ResetCameraClippingRange();
            }
        }
    }

    if (m_routeSpeedLabel) {
        m_routeSpeedLabel->setText(QString("Speed: %1 km/h").arg(seg.speedMps * 3.6, 0, 'f', 2));
    }
    if (m_routeSlopeLabel) {
        m_routeSlopeLabel->setText(QString("Slope: %1 %").arg(seg.slopePct, 0, 'f', 2));
    }

    if (m_renderWindow) {
        m_renderWindow->Render();
    }
}

void MainWindow::UpdateLayerRender(const QString &layerName, qint64 dateKey)
{
    if (layerName.isEmpty()) {
        return;
    }
    auto it = m_layerRenders.find(layerName);
    if (it == m_layerRenders.end()) {
        return;
    }
    LayerRenderData &render = it.value();
    if (!render.mapper) {
        return;
    }
    auto vit = render.versions.find(dateKey);
    if (vit == render.versions.end()) {
        return;
    }
    vtkSmartPointer<vtkPolyData> poly = vit.value();
    render.mapper->SetInputData(poly);
    render.mapper->Update();

    for (auto &item : m_sceneItems) {
        if (item.layerName == layerName) {
            item.data = poly;
            break;
        }
    }

    if (m_renderWindow) {
        UpdateDynamicLineWidths();
        m_renderWindow->Render();
    }
}

void MainWindow::UpdateDynamicLineWidths()
{
    if (!m_renderer) {
        return;
    }
    vtkCamera *camera = m_renderer->GetActiveCamera();
    if (!camera) {
        return;
    }
    camera->ParallelProjectionOn();

    const double metric = std::max(camera->GetParallelScale(), 1e-9);

    if (m_lineWidthReference < 0.0) {
        m_lineWidthReference = metric;
    }

    const double ratio = m_lineWidthReference / metric;
    const double width = std::clamp(1.1 * std::pow(ratio, 0.6), 0.15, 2.2);

    for (auto it = m_layerRenders.begin(); it != m_layerRenders.end(); ++it) {
        LayerRenderData &render = it.value();
        if (!render.actor || !render.mapper) {
            continue;
        }
        vtkPolyData *poly = render.mapper->GetInput();
        if (!poly) {
            continue;
        }
        const bool isLineOnly = poly->GetNumberOfLines() > 0 && poly->GetNumberOfPolys() == 0;
        if (isLineOnly) {
            render.actor->GetProperty()->SetLineWidth(width);
        }
    }

    for (auto &item : m_sceneItems) {
        if (!item.highlightActor) {
            continue;
        }
        item.highlightActor->GetProperty()->SetLineWidth(std::clamp(width + 1.0, 1.0, 3.5));
    }

    if (m_routePathActor) {
        m_routePathActor->GetProperty()->SetLineWidth(std::clamp(width + 0.7, 0.5, 3.0));
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != m_vtkWidget) {
        return QMainWindow::eventFilter(obj, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_blockSectionPickActive && mouseEvent->button() == Qt::LeftButton) {
            return HandleBlockSectionPick(mouseEvent->pos());
        }
        if (mouseEvent->button() == Qt::LeftButton &&
            (mouseEvent->modifiers() & Qt::ShiftModifier) == 0) {
            m_selecting = true;
            m_selectStart = mouseEvent->pos();
            m_selectEnd = m_selectStart;
            if (!m_selectionBand) {
                m_selectionBand = new QRubberBand(QRubberBand::Rectangle, m_vtkWidget);
            }
            UpdateSelectionBand();
            m_selectionBand->show();
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        if (!m_selecting) {
            break;
        }
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        m_selectEnd = mouseEvent->pos();
        UpdateSelectionBand();
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_selecting && mouseEvent->button() == Qt::LeftButton) {
            m_selecting = false;
            m_selectEnd = mouseEvent->pos();
            if (m_selectionBand) {
                m_selectionBand->hide();
            }

            const QPoint delta = m_selectEnd - m_selectStart;
            if (delta.manhattanLength() >= 4) {
                const bool crossing = m_selectEnd.x() < m_selectStart.x();
                SelectArea(m_selectStart, m_selectEnd, crossing);
            } else {
                const int half = 4;
                const QPoint p = m_selectEnd;
                SelectArea(p + QPoint(-half, -half), p + QPoint(half, half), true);
            }
            return true;
        }
        break;
    }
    case QEvent::Resize: {
        if (m_routeControlOverlay && m_routeControlOverlay->isVisible()) {
            ShowRouteControlOverlay(true);
        }
        if (m_blockPropertiesOverlay && m_blockPropertiesOverlay->isVisible()) {
            PositionBlockPropertiesOverlay();
        }
        break;
    }
    default:
        break;
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        "Close Project",
        "Do you want to save changes before closing?",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Yes);

    if (choice == QMessageBox::Cancel) {
        event->ignore();
        return;
    }

    if (choice == QMessageBox::Yes) {
        bool saved = false;
        if (m_projectPath.isEmpty()) {
            const QString path = QFileDialog::getSaveFileName(
                this,
                "Save Project",
                QString(),
                "GreenVisor Project (*.gvs)");
            if (path.isEmpty()) {
                event->ignore();
                return;
            }
            saved = SaveProjectTo(path);
        } else {
            saved = SaveProjectTo(m_projectPath);
        }
        if (!saved) {
            event->ignore();
            return;
        }
    }

    DetachVtkCallbacksForShutdown();
    event->accept();
}

void MainWindow::ApplyDockStyle()
{
    AppStyle::ApplyWidgetStyle(this);
}
