#pragma once

#include <QMainWindow>
#include <QColor>
#include <QList>
#include <QModelIndex>
#include <QPoint>
#include <vtkSmartPointer.h>

#include <string>
#include <array>
#include <cstdint>
#include <QHash>
#include <QMap>
#include <unordered_map>
#include <vector>

#include "io/DatamineImporter.h"

class QComboBox;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTabWidget;
class QTextEdit;
class QTimer;
class QTreeView;
class QVTKOpenGLNativeWidget;
class QRubberBand;
class QToolButton;
class QWidget;

class vtkActor;
class vtkGenericOpenGLRenderWindow;
class vtkGlyph3DMapper;
class vtkPolyDataMapper;
class vtkPolyData;
class vtkOrientationMarkerWidget;
class vtkRenderer;

class LayerModel;
class LayerNode;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool LoadProject(const QString &path);
    bool NewProject(const QString &path = QString());

private:
    struct SceneItem
    {
        vtkSmartPointer<vtkActor> actor;
        vtkSmartPointer<vtkPolyData> data;
        vtkSmartPointer<vtkActor> highlightActor;
        QString layerName;
    };

    void BuildViewport();
    void BuildMenuBar();
    void BuildRibbon();
    void BuildDockWidgets();
    void ApplyDockStyle();
    void LoadDxfFile();
    void LoadBlockModelFile();
    void LoadObjFile();
    void ClearSelection();
    void SelectArea(const QPoint &start, const QPoint &end, bool crossing);
    void UpdateSelectionBand();
    void OnLayerSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void OnLayerDoubleClicked(const QModelIndex &index);
    void OnLayerContextMenu(const QPoint &pos);
    void OnLayerVisibilityChanged(LayerNode *node, bool visible);
    void OnLayerDateChanged(int index);
    void OnAddDateClicked();
    void OnOpenRouteSetup();
    void OnCalculateRoutes();
    void OnVisualizeRoutes();
    void OnCreateEconomicModel();
    void OnModifyEconomicModel();
    void OnDeleteEconomicModel();
    void OnOptimizationSettings();
    LayerNode *FindBlockModelAncestor(LayerNode *node) const;
    LayerNode *FindLegendOwnerBlockModel(LayerNode *node) const;
    LayerNode *FindEconomicModelsFolder(LayerNode *blockModelNode) const;
    LayerNode *EnsureBlockModelSystemFolder(LayerNode *blockModelNode, const QString &folderName);
    void EnsureBlockModelSystemFolders(LayerNode *blockModelNode);
    void SyncLegendLayerNodes(const QString &blockModelLayerName = QString());
    bool ModifyLegendFromLayerNode(LayerNode *legendNode);
    bool RunMineFlowOptimization(
        const QString &economicModelKey,
        double startRevenueFactor,
        double maxRevenueFactor,
        double revenueFactorStep,
        double globalSlopeAngleDeg,
        QString *errorMessage = nullptr);
    void OnPickBlockSectionTwoPoints();
    void ShowBlockModelProperties(LayerNode *node);
    void ApplyBlockModelRender(const QString &layerName);
    void SelectBlockModelCell(const QPoint &start, const QPoint &end);
    bool HandleBlockSectionPick(const QPoint &pos);
    std::array<double, 3> PickWorldPointWithSnap(const QPoint &pos) const;
    void BuildBlockModelSection(
        const QString &layerName,
        const std::array<double, 3> &firstPoint,
        const std::array<double, 3> &secondPoint,
        bool verticalSection);
    void ShowSelectedBlockProperties(
        const QString &layerName,
        std::size_t previewIndex,
        const visor::datamine::BlockCell &cell);
    void HideSelectedBlockProperties();
    void PositionBlockPropertiesOverlay();
    void SetViewMode3D();
    void SetViewMode2D();
    void OnFileNew();
    void OnFileOpen();
    void OnFileSave();
    void OnFileSaveAs();
    void UpdateLayerRender(const QString &layerName, qint64 dateKey);
    bool SaveProjectTo(const QString &path);
    bool LoadProjectFrom(const QString &path);
    void ResetProjectState();
    void DetachVtkCallbacksForShutdown();
    void UpdateDynamicLineWidths();
    void BuildRouteControlOverlay();
    void ShowRouteProfileDialog();
    void ShowRouteControlOverlay(bool visible);
    void StopRouteVisualization(bool removeActors = true);
    void StepRouteVisualization();

    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    QTabWidget *m_ribbon = nullptr;
    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    QTextEdit *m_consoleLog = nullptr;
    QLineEdit *m_consoleInput = nullptr;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_axesWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
    bool m_hasScene = false;
    QTreeView *m_layersView = nullptr;
    LayerModel *m_layerModel = nullptr;
    QComboBox *m_layerDateCombo = nullptr;
    QWidget *m_blockPropertiesOverlay = nullptr;
    QTableWidget *m_blockPropertiesTable = nullptr;
    QLabel *m_blockPropertiesTitle = nullptr;
    QModelIndex m_currentLayerIndex;
    std::vector<SceneItem> m_sceneItems;
    std::unordered_map<vtkActor *, std::size_t> m_actorIndex;
    struct LayerRenderData
    {
        vtkSmartPointer<vtkActor> actor;
        vtkSmartPointer<vtkPolyDataMapper> mapper;
        QMap<qint64, vtkSmartPointer<vtkPolyData>> versions;
    };
    struct BlockModelLayerData
    {
        visor::datamine::InternalBlockModelInfo info;
        QString internalPath;
        QString storedRelativePath;
        QString description;
        vtkSmartPointer<vtkActor> actor;
        vtkSmartPointer<vtkActor> selectionActor;
        vtkSmartPointer<vtkActor> sectionActor;
        vtkSmartPointer<vtkGlyph3DMapper> mapper;
        vtkSmartPointer<vtkPolyData> pointsData;
        std::array<double, 3> renderOrigin{0.0, 0.0, 0.0};
        std::vector<visor::datamine::BlockCell> previewCells;
        std::vector<visor::datamine::BlockCell> sectionSourceCells;
        std::vector<std::vector<std::array<double, 3>>> sectionSourcePolygons;
        std::vector<std::int64_t> sectionSourceRows;
        std::vector<visor::datamine::BlockCell> sectionCells;
        std::vector<std::vector<std::array<double, 3>>> sectionPolygons;
        std::vector<std::int64_t> sectionRows;
        std::unordered_map<std::int64_t, visor::datamine::BlockCell> fullCellCache;
        bool renderDirty = true;
    };
public:
    enum class BlockModelRenderMode
    {
        Section2D,
        Solid3D
    };
    enum class ViewMode
    {
        View3D,
        View2D
    };
    struct BlockModelDisplaySettings
    {
        bool blocksEnabled = true;
        bool linesEnabled = false;
        bool labelsEnabled = false;
        QColor blockColor = QColor(80, 190, 190);
        QColor lineColor = QColor(255, 255, 255);
        QString blockLegend;
        QString lineLegend;
        QString labelField;
        int labelFontSize = 10;
        double blockGapPercent = 8.0;
        BlockModelRenderMode renderMode = BlockModelRenderMode::Section2D;
    };
    struct BlockModelLegendBin
    {
        bool visible = true;
        double minValue = 0.0;
        double maxValue = 0.0;
        QColor color;
        QString label;
    };
    struct BlockModelLegend
    {
        QString name;
        QString layerName;
        QString fieldName;
        QList<BlockModelLegendBin> bins;
    };
    struct EconomicModelDefinition
    {
        struct Variable
        {
            QString name;
            QString formula;
        };
        struct Destination
        {
            bool enabled = true;
            QString name;
            double processingCost = 0.0;
            QStringList productValues;
        };
        struct RockTypeSettings
        {
            QString rockType;
            double dilution = 0.0;
            double miningRecovery = 100.0;
            double miningCost = 0.0;
            QList<Destination> destinations;
        };

        QString name;
        QString blockModelLayerName;
        QHash<QString, QString> fieldRoles;
        QHash<QString, QString> fieldUnits;
        QHash<QString, QStringList> uniqueFieldValues;
        QList<Variable> variables;
        QList<RockTypeSettings> rockTypeSettings;
        bool profitModel = false;
        QString profitField;
        QString generatedInternalPath;
        QString generatedStoredRelativePath;
        qint64 generatedCellCount = 0;
    };
private:
    bool GenerateEconomicBlockModel(EconomicModelDefinition &definition, QString *errorMessage = nullptr);

    QHash<QString, LayerRenderData> m_layerRenders;
    QHash<QString, BlockModelLayerData> m_blockModelLayers;
    QHash<QString, BlockModelDisplaySettings> m_blockModelSettings;
    QList<BlockModelLegend> m_blockModelLegends;
    QHash<QString, EconomicModelDefinition> m_economicModels;
    ViewMode m_viewMode = ViewMode::View3D;
    QString m_projectPath;
    QRubberBand *m_selectionBand = nullptr;
    bool m_selecting = false;
    bool m_crossingSelect = false;
    bool m_blockSectionPickActive = false;
    QString m_blockSectionLayerName;
    std::vector<std::array<double, 3>> m_blockSectionPoints;
    QPoint m_selectStart;
    QPoint m_selectEnd;
    double m_lineWidthReference = -1.0;

    struct RouteVisualSegment
    {
        double length = 0.0;
        double speedMps = 0.0;
        double slopePct = 0.0;
        double start[3] = {0.0, 0.0, 0.0};
        double end[3] = {0.0, 0.0, 0.0};
    };
    std::vector<RouteVisualSegment> m_routeVisualSegments;
    int m_routeSegmentIndex = 0;
    double m_routeDistanceInSegment = 0.0;
    QTimer *m_routeTimer = nullptr;
    double m_routePlaybackMultiplier = 1.0;
    QWidget *m_routeControlOverlay = nullptr;
    QToolButton *m_routeSpeed1xBtn = nullptr;
    QToolButton *m_routeSpeed10xBtn = nullptr;
    QToolButton *m_routeSpeed20xBtn = nullptr;
    QToolButton *m_routeFollowBtn = nullptr;
    bool m_routeFollowTruck = false;
    QLabel *m_routeSpeedLabel = nullptr;
    QLabel *m_routeSlopeLabel = nullptr;
    vtkSmartPointer<vtkActor> m_routePathActor;
    vtkSmartPointer<vtkActor> m_routeTruckActor;
};
