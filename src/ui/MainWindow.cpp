#include "MainWindow.h"
#include "data/CanopyImportOptions.h"
#include "CompactLocationLabel.h"
#include "WorkspaceTheme.h"
#include "LayersPanel.h"
#include "ShortcutsDialog.h"
#include "TerrainPresentation.h"
#include <QToolButton>
#include "RuntimePaths.h"
#include "data/TerrainLoader.h"
#include "data/GlobalTerrainLoader.h"
#include "data/ScenarioIO.h"
#include "data/RecoveryStore.h"
#include "data/WorkspaceSettings.h"
#include "data/AntennaJson.h"
#include "sim/SolveSignature.h"
#include "data/MeasurementIO.h"
#include "data/Exporter.h"
#include "data/CoverageGeoTiff.h"
#include "data/AntennaLibrary.h"
#include "LocationDialog.h"
#include "ReceiverDialog.h"
#include "SourcePositionDialog.h"
#include "geo/Mgrs.h"
#include "sim/PointAnalysis.h"
#include <QWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QApplication>
#include <QMenuBar>
#include <QFileDialog>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QInputDialog>
#include <tuple>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QNetworkAccessManager>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QUndoCommand>
#include "SnapshotCommand.h"
#include "VegetationAreaDialog.h"
#include "map/VegetationGeometry.h"
#include <QUndoStack>
#include <QCloseEvent>
#include <QScopeGuard>
#include <QDateTime>
#include <QtConcurrent/QtConcurrentRun>
#include <stdexcept>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <functional>
#include <utility>

namespace {

// Whole-state snapshots for both editable lists. One template, so sources and
// drawn vegetation share merge rules, and a command of one kind can never
// merge into the other.
using SourceStateCommand = SnapshotCommand<std::vector<Antenna>>;
using VegetationStateCommand = SnapshotCommand<std::vector<VegetationArea>>;

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Ridgeline[*]");
    applyTheme();
    setupScene();

    try {
        m_antennaLib.load(
            RuntimePaths::resolve("assets/antennas.json").toStdString());
    } catch (...) {
        m_antennaLib.loadDefaults();
    }

    m_undoStack = new QUndoStack(this);
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this] {
        setWindowModified(isScenarioDirty());
    });

    QMenu* fileMenu = menuBar()->addMenu("&File");
    QAction* locationAct = fileMenu->addAction("&Choose Location…");
    locationAct->setShortcut(QKeySequence("Ctrl+L"));
    connect(locationAct, &QAction::triggered, this, &MainWindow::onChooseLocation);

    fileMenu->addSeparator();
    QAction* loadAct = fileMenu->addAction("&Load Terrain…");
    loadAct->setShortcut(QKeySequence::Open);
    connect(loadAct, &QAction::triggered, this, &MainWindow::onLoadTerrain);

    fileMenu->addSeparator();
    m_recentLocationsMenu = fileMenu->addMenu("Recent &Locations");
    m_recentLocationsMenu->setObjectName("RecentLocationsMenu");
    m_recentScenariosMenu = fileMenu->addMenu("Recent &Scenarios");
    m_recentScenariosMenu->setObjectName("RecentScenariosMenu");
    fileMenu->addSeparator();
    QAction* openScenAct = fileMenu->addAction("&Open Scenario…");
    openScenAct->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openScenAct, &QAction::triggered, this, &MainWindow::onOpenScenario);

    QAction* saveScenAct = fileMenu->addAction("&Save Scenario…");
    saveScenAct->setShortcut(QKeySequence::Save);
    connect(saveScenAct, &QAction::triggered, this, &MainWindow::onSaveScenario);

    fileMenu->addSeparator();
    QAction* importMeasurementsAct =
        fileMenu->addAction("Import RF &Measurements…");
    importMeasurementsAct->setObjectName("ImportMeasurementsAction");
    connect(importMeasurementsAct, &QAction::triggered,
            this, &MainWindow::onImportMeasurements);

    QMenu* exportMenu = fileMenu->addMenu("&Export");
    QAction* exportPngAct = exportMenu->addAction("Coverage &PNG…");
    connect(exportPngAct, &QAction::triggered, this, &MainWindow::onExportPng);
    QAction* exportCsvAct = exportMenu->addAction("Coverage &CSV…");
    connect(exportCsvAct, &QAction::triggered, this, &MainWindow::onExportCsv);
    // One metric per file, so each opens in a GIS as a single labelled band.
    QMenu* geoTiffMenu = exportMenu->addMenu("Coverage &GeoTIFF");
    geoTiffMenu->setObjectName("ExportGeoTiffMenu");
    for (const auto& [label, name, metric] : {
             std::tuple{"Desired &Signal (dBm)…", "ExportGeoTiffSignalAction",
                        CoverageGeoTiff::Metric::DesiredSignal},
             std::tuple{"S&INR (dB)…", "ExportGeoTiffSinrAction",
                        CoverageGeoTiff::Metric::Sinr},
             std::tuple{"&Fade Margin (dB)…", "ExportGeoTiffFadeMarginAction",
                        CoverageGeoTiff::Metric::FadeMargin},
             std::tuple{"Ser&vice (in = 1, out = 0)…", "ExportGeoTiffServiceAction",
                        CoverageGeoTiff::Metric::Service}}) {
        QAction* action = geoTiffMenu->addAction(label);
        action->setObjectName(name);
        const CoverageGeoTiff::Metric chosen = metric;
        connect(action, &QAction::triggered, this, [this, chosen] { onExportGeoTiff(chosen); });
    }

    QMenu* editMenu = menuBar()->addMenu("&Edit");
    QAction* undoAction = m_undoStack->createUndoAction(this, "&Undo");
    undoAction->setObjectName("UndoSourceAction");
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    QAction* redoAction = m_undoStack->createRedoAction(this, "&Redo");
    redoAction->setObjectName("RedoSourceAction");
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    QAction* duplicateAct = editMenu->addAction("&Duplicate Source");
    duplicateAct->setObjectName("DuplicateSourceAction");
    duplicateAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAct, &QAction::triggered, this, &MainWindow::onDuplicateAntenna);
    editMenu->addSeparator();
    QAction* savePresetAct = editMenu->addAction("Save Source as &Preset…");
    savePresetAct->setObjectName("SaveEquipmentPresetAction");
    connect(savePresetAct, &QAction::triggered, this, &MainWindow::onSaveEquipmentPreset);
    m_applyPresetMenu = editMenu->addMenu("&Apply Preset");
    m_applyPresetMenu->setObjectName("ApplyEquipmentPresetMenu");
    m_deletePresetMenu = editMenu->addMenu("De&lete Preset");
    m_deletePresetMenu->setObjectName("DeleteEquipmentPresetMenu");
    refreshPresetMenus();

    QMenu* analysisMenu = menuBar()->addMenu("&Analysis");
    // Running coverage is the point of the application, and until now it lived
    // only on a toolbar button with no shortcut and no menu entry.
    QAction* runAct = analysisMenu->addAction("&Run Coverage");
    runAct->setShortcut(QKeySequence("F5"));
    connect(runAct, &QAction::triggered, this, &MainWindow::onSolve);
    QAction* measureAct = analysisMenu->addAction("&Measure Distance");
    measureAct->setShortcut(QKeySequence("M"));
    measureAct->setCheckable(true);
    analysisMenu->addSeparator();
    QAction* receiverAct = analysisMenu->addAction("&Receiver / Service Criteria…");
    connect(receiverAct, &QAction::triggered, this, &MainWindow::onReceiverSettings);
    QAction* comparisonAct = analysisMenu->addAction("Scenario &Comparison…");
    comparisonAct->setObjectName("ScenarioComparisonAction");
    QAction* calibrationAct = analysisMenu->addAction("Measurement &Calibration…");
    calibrationAct->setObjectName("MeasurementCalibrationAction");

    QMenu* vegetationMenu = analysisMenu->addMenu("&Vegetation");
    vegetationMenu->setObjectName("VegetationMenu");
    QAction* loadCanopyAct = vegetationMenu->addAction("&Load Canopy for This Area…");
    loadCanopyAct->setObjectName("LoadCanopyAction");
    connect(loadCanopyAct, &QAction::triggered, this, &MainWindow::onLoadCanopy);
    m_clearCanopyAct = vegetationMenu->addAction("&Clear Imported Canopy");
    m_clearCanopyAct->setObjectName("ClearCanopyAction");
    connect(m_clearCanopyAct, &QAction::triggered, this, &MainWindow::onClearCanopy);
    QAction* canopyOptionsAct = vegetationMenu->addAction("Canopy &Options…");
    canopyOptionsAct->setObjectName("CanopyOptionsAction");
    connect(canopyOptionsAct, &QAction::triggered, this, &MainWindow::onCanopyOptions);
    vegetationMenu->addSeparator();
    // Drawing is available anywhere, including terrain with no real-world
    // location, which makes it the only route to vegetation outside LANDFIRE's
    // coverage. No shortcuts: a window-wide Delete would fire while the source
    // list has focus, and single letters are taken by the map.
    m_drawStandsAct = vegetationMenu->addAction("Draw &Stands");
    m_drawStandsAct->setObjectName("DrawVegetationStandsAction");
    m_drawStandsAct->setCheckable(true);
    m_drawClearingsAct = vegetationMenu->addAction("Draw C&learings");
    m_drawClearingsAct->setObjectName("DrawVegetationClearingsAction");
    m_drawClearingsAct->setCheckable(true);
    m_editVegetationAct = vegetationMenu->addAction("Vegetation Area &Properties…");
    m_editVegetationAct->setObjectName("EditVegetationAreaAction");
    connect(m_editVegetationAct, &QAction::triggered, this, &MainWindow::onEditVegetationArea);
    m_deleteVegetationAct = vegetationMenu->addAction("&Delete Vegetation Area");
    m_deleteVegetationAct->setObjectName("DeleteVegetationAreaAction");
    connect(m_deleteVegetationAct, &QAction::triggered, this, &MainWindow::onDeleteVegetationArea);
    m_deleteOffTerrainVegetationAct =
        vegetationMenu->addAction("Delete Areas &Outside This Terrain");
    m_deleteOffTerrainVegetationAct->setObjectName("DeleteOffTerrainVegetationAction");
    connect(m_deleteOffTerrainVegetationAct, &QAction::triggered,
            this, &MainWindow::onDeleteOffTerrainVegetation);
    vegetationMenu->addSeparator();
    // A permanently disabled entry, used as a label: what is loaded should be
    // readable without opening a dialog, and there is nowhere else in this menu
    // for it to live.
    m_canopyStatusAct = vegetationMenu->addAction(QString());
    m_canopyStatusAct->setEnabled(false);

    m_viewport = new ViewportWidget(&m_engine, this);
    setCentralWidget(m_viewport);

    m_scenarioTree = new ScenarioTree(this);
    m_propsPanel = new PropertiesPanel(this);
    m_propsPanel->setAntennaLibrary(&m_antennaLib);
    m_scenarioTree->setMinimumHeight(195);
    m_propsPanel->setMinimumHeight(250);

    auto* sourceWorkspace = new QSplitter(Qt::Vertical, this);
    sourceWorkspace->setChildrenCollapsible(false);
    sourceWorkspace->addWidget(m_scenarioTree);
    sourceWorkspace->addWidget(m_propsPanel);
    sourceWorkspace->setObjectName("SourceWorkspaceSplitter");
    sourceWorkspace->setHandleWidth(5);
    sourceWorkspace->setStretchFactor(0, 1);
    sourceWorkspace->setStretchFactor(1, 3);
    sourceWorkspace->setSizes({240, 430});

    auto* leftDock = new QDockWidget("Sources", this);
    leftDock->setObjectName("SourcesDock");
    leftDock->setMinimumWidth(310);
    leftDock->setMaximumWidth(440);
    leftDock->setWidget(sourceWorkspace);
    leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    leftDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    m_pointPanel = new PointAnalysisPanel(this);
    m_pointDock = new QDockWidget("Result Inspector", this);
    m_pointDock->setObjectName("ResultInspectorDock");
    m_pointDock->setWidget(m_pointPanel);
    m_pointDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_pointDock->setFeatures(
        QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_pointDock->setMinimumWidth(310);
    addDockWidget(Qt::RightDockWidgetArea, m_pointDock);
    m_pointDock->hide();

    m_comparisonPanel = new ScenarioComparisonPanel(this);
    m_comparisonDock = new QDockWidget("Scenario Comparison", this);
    m_comparisonDock->setObjectName("ScenarioComparisonDock");
    m_comparisonDock->setWidget(m_comparisonPanel);
    m_comparisonDock->setAllowedAreas(
        Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_comparisonDock->setFeatures(
        QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_comparisonDock->setMinimumWidth(310);
    addDockWidget(Qt::RightDockWidgetArea, m_comparisonDock);
    tabifyDockWidget(m_pointDock, m_comparisonDock);
    m_comparisonDock->hide();

    connect(comparisonAct, &QAction::triggered, this, [this]() {
        m_comparisonPanel->setCurrentResultAvailable(
            m_engine.fieldGrid().valid());
        m_comparisonDock->show();
        m_comparisonDock->raise();
    });
    connect(m_comparisonPanel,
            &ScenarioComparisonPanel::setBaselineRequested,
            this, &MainWindow::captureScenarioBaseline);
    connect(m_comparisonPanel,
            &ScenarioComparisonPanel::replaceBaselineRequested,
            this, &MainWindow::captureScenarioBaseline);
    connect(m_comparisonPanel,
            &ScenarioComparisonPanel::clearComparisonRequested,
            this, &MainWindow::clearScenarioBaseline);
    connect(m_comparisonPanel,
            &ScenarioComparisonPanel::showChangesOnMapChanged,
            m_viewport, &ViewportWidget::setScenarioComparisonVisible);

    m_calibrationPanel = new CalibrationPanel(this);
    m_calibrationDock = new QDockWidget("Measurement Calibration", this);
    m_calibrationDock->setObjectName("MeasurementCalibrationDock");
    m_calibrationDock->setWidget(m_calibrationPanel);
    m_calibrationDock->setAllowedAreas(
        Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_calibrationDock->setFeatures(
        QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_calibrationDock->setMinimumWidth(320);
    addDockWidget(Qt::RightDockWidgetArea, m_calibrationDock);
    tabifyDockWidget(m_comparisonDock, m_calibrationDock);
    m_calibrationDock->hide();

    m_layersPanel = new LayersPanel(this);
    m_layersDock = new QDockWidget("Layers", this);
    m_layersDock->setObjectName("LayersDock");
    m_layersDock->setWidget(m_layersPanel);
    m_layersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_layersDock->setFeatures(
        QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_layersDock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, m_layersDock);
    tabifyDockWidget(m_calibrationDock, m_layersDock);

    connect(calibrationAct, &QAction::triggered,
            this, &MainWindow::showCalibrationPanel);
    connect(m_calibrationPanel, &CalibrationPanel::importMeasurementsRequested,
            this, &MainWindow::onImportMeasurements);
    connect(m_calibrationPanel,
            &CalibrationPanel::applyRecommendedCorrectionRequested,
            this, &MainWindow::applyCalibrationCorrection);
    connect(m_calibrationPanel,
            &CalibrationPanel::resetAppliedCorrectionRequested,
            this, &MainWindow::resetCalibrationCorrection);
    connect(m_calibrationPanel, &CalibrationPanel::clearMeasurementsRequested,
            this, &MainWindow::clearCalibrationMeasurements);

    connect(m_scenarioTree, &ScenarioTree::addEmitterRequested,
            this, &MainWindow::onAddAntenna);
    connect(m_scenarioTree, &ScenarioTree::addJammerRequested,
            this, &MainWindow::onAddJammer);
    connect(m_scenarioTree, &ScenarioTree::removeRequested,
            this, &MainWindow::onRemoveAntenna);
    connect(m_scenarioTree, &ScenarioTree::selectionChanged,
            this, &MainWindow::onAntennaSelected);
    connect(m_propsPanel, &PropertiesPanel::antennaChanged,
            this, &MainWindow::onAntennaChanged);
    connect(m_propsPanel, &PropertiesPanel::positionEditRequested,
            this, &MainWindow::onEditAntennaPosition);

    QToolBar* tb = addToolBar("Main");
    tb->setObjectName("MainToolbar");
    tb->setMovable(false);

    auto* brand = new QLabel("  RIDGELINE  ");
    brand->setStyleSheet("color: #f8fafc; font-size: 15px; font-weight: 750; letter-spacing: 2px;");
    tb->addWidget(brand);

    auto* sourcesButton = new QToolButton();
    sourcesButton->setObjectName("SourcesPanelButton");
    sourcesButton->setDefaultAction(leftDock->toggleViewAction());
    sourcesButton->setToolTip("Show or hide source planning controls");
    tb->addWidget(sourcesButton);

    auto* locationButton = new QPushButton("⌖  Location");
    locationButton->setObjectName("LocationButton");
    locationButton->setToolTip("Choose any location worldwide (Ctrl+L)");
    connect(locationButton, &QPushButton::clicked, this, &MainWindow::onChooseLocation);
    tb->addWidget(locationButton);
    m_locationLabel = new CompactLocationLabel("Demo terrain · Choose a location to begin");
    m_locationLabel->setObjectName("LocationSummary");
    m_locationLabel->setTextFormat(Qt::PlainText);
    m_locationLabel->setMinimumWidth(140);
    m_locationLabel->setMaximumWidth(270);
    m_locationLabel->setWordWrap(false);
    m_locationLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    tb->addWidget(m_locationLabel);

    auto* toolbarSpacer = new QWidget();
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(toolbarSpacer);

    auto* qualityLabel = new QLabel("QUALITY");
    qualityLabel->setStyleSheet("color: #a8b7ca; font-size: 11px; font-weight: 600;");
    tb->addWidget(qualityLabel);

    m_modeCombo = new QComboBox();
    m_modeCombo->setObjectName("CoverageQualityCombo");
    m_modeCombo->setAccessibleName("Coverage quality");
    m_modeCombo->addItem("Quick preview");
    m_modeCombo->addItem("Balanced");
    m_modeCombo->addItem("High accuracy · ITM");
    m_modeCombo->setCurrentIndex(1);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (idx == 0) {
                    m_engine.mode = SimEngine::Mode::Preview;
                    m_statusLabel->setText("  Preview RF Mode");
                } else if (idx == 1) {
                    m_engine.mode = SimEngine::Mode::Planner;
                    m_statusLabel->setText("  Planner RF Mode");
                } else {
                    m_engine.mode = SimEngine::Mode::Engineering;
                    m_statusLabel->setText("  Engineering RF Mode · NTIA ITM");
                }
                markScenarioDirty();
                if (m_engine.fieldGrid().valid()) {
                    m_engine.setFieldGrid({});
        m_solvedSignature.clear();
    m_solvedSignature.clear();
                    invalidateScenarioComparisonCurrent();
                    invalidateCalibrationCurrent();
                    if (m_pointPanel) m_pointPanel->clear();
                    if (m_pointDock) m_pointDock->hide();
                    if (m_viewport) {
                        m_viewport->clearAnalysisPoint();
                        m_viewport->refreshOverlays();
                    }
                    m_statusLabel->setText("  Analysis quality changed · Run coverage to refresh results");
                }
            });
    tb->addWidget(m_modeCombo);

    m_solveBtn = new QPushButton("Run coverage");
    m_solveBtn->setObjectName("RunButton");
    connect(m_solveBtn, &QPushButton::clicked, this, &MainWindow::onSolve);
    tb->addWidget(m_solveBtn);

    m_overlayBar = new OverlayToolbar(this);
    // Keep the quality and solve controls reachable at ordinary laptop widths.
    // Two dense toolbars on one row force QWidget actions into Qt's clipped
    // overflow popup, where combo boxes and buttons cannot be used reliably.
    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar* overlayTb = addToolBar("Overlays");
    overlayTb->setObjectName("OverlayToolbar");
    overlayTb->setMovable(false);
    overlayTb->addWidget(m_overlayBar);
    connect(m_overlayBar, &OverlayToolbar::heatmapMetricChanged,
            m_viewport, &ViewportWidget::setHeatmapMetric);
    connect(m_overlayBar, &OverlayToolbar::measureModeToggled,
            m_viewport, &ViewportWidget::setMeasureMode);
    connect(measureAct, &QAction::toggled, m_viewport, &ViewportWidget::setMeasureMode);
    connect(m_viewport, &ViewportWidget::measureModeChanged, measureAct,
            [measureAct](bool active) {
        if (measureAct->isChecked() == active) return;
        const QSignalBlocker blocker(measureAct);
        measureAct->setChecked(active);
    });
    connect(m_viewport, &ViewportWidget::measureModeChanged,
            m_overlayBar, &OverlayToolbar::setMeasureChecked);
    connect(m_viewport, &ViewportWidget::measureModeChanged, this, [this](bool active) {
        if (active)
            m_statusLabel->setText("  Measuring · click two points on the map · Esc to stop");
        else
            m_statusLabel->setText("  Ready");
    });
    connect(m_viewport, &ViewportWidget::measurementChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) m_statusLabel->setText("  " + text);
    });
    connect(m_drawStandsAct, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_viewport->setVegetationDrawMode(true, VegetationKind::Stand);
        else if (m_viewport->vegetationDrawMode() &&
                 m_viewport->vegetationDrawKind() == VegetationKind::Stand)
            m_viewport->setVegetationDrawMode(false);
    });
    connect(m_drawClearingsAct, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_viewport->setVegetationDrawMode(true, VegetationKind::Clearing);
        else if (m_viewport->vegetationDrawMode() &&
                 m_viewport->vegetationDrawKind() == VegetationKind::Clearing)
            m_viewport->setVegetationDrawMode(false);
    });
    connect(m_viewport, &ViewportWidget::vegetationDrawModeChanged,
            this, &MainWindow::onVegetationDrawModeChanged);
    connect(m_viewport, &ViewportWidget::vegetationAreaDrawn,
            this, &MainWindow::onVegetationAreaDrawn);
    connect(m_viewport, &ViewportWidget::vegetationAreaReshaped,
            this, &MainWindow::onVegetationAreaReshaped);
    connect(m_viewport, &ViewportWidget::vegetationAreaClicked,
            this, &MainWindow::onVegetationAreaClicked);
    connect(m_viewport, &ViewportWidget::vegetationDeleteRequested,
            this, &MainWindow::onDeleteVegetationArea);
    connect(m_viewport, &ViewportWidget::vegetationEditRequested,
            this, &MainWindow::onEditVegetationArea);
    connect(m_viewport, &ViewportWidget::vegetationGestureRejected,
            this, [this](const QString& reason) { m_statusLabel->setText("  " + reason); });
    connect(m_overlayBar, &OverlayToolbar::resetViewRequested,
            m_viewport, &ViewportWidget::resetView);
    connect(m_overlayBar, &OverlayToolbar::topViewRequested,
            m_viewport, &ViewportWidget::topView);

    m_statusLabel = new QLabel("  Ready");
    m_statusLabel->setObjectName("WorkspaceStatus");
    m_statusLabel->setStyleSheet("color: #b8c8da; font-size: 12px; padding: 4px 8px;");
    statusBar()->addWidget(m_statusLabel);
    statusBar()->setStyleSheet("background: #0a1220; border-top: 1px solid #1e2d42;");
    connect(m_viewport, &ViewportWidget::basemapInfoChanged, this,
            [this](const QString& provider, const QString& attribution, const QString& error) {
        if (m_locationLabel) {
            m_locationLabel->setProperty("basemapInfo",
                QString("Basemap: %1\n%2%3")
                    .arg(provider, attribution,
                         error.isEmpty() ? QString{} : QString("\n%1").arg(error)));
            updateLocationSummary();
        }
    });

    m_bottomBar = new BottomBar(this);
    auto* bottomDock = new QDockWidget("Spectrum", this);
    bottomDock->setWidget(m_bottomBar);
    bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    bottomDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    bottomDock->hide();

    QTimer::singleShot(0, this, [this, leftDock]() {
        addDockWidget(Qt::LeftDockWidgetArea, leftDock);
        leftDock->show();
        resizeDocks({leftDock}, {350}, Qt::Horizontal);
    });

    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(leftDock->toggleViewAction());
    viewMenu->addAction(bottomDock->toggleViewAction());
    viewMenu->addAction(m_pointDock->toggleViewAction());
    viewMenu->addAction(m_comparisonDock->toggleViewAction());
    viewMenu->addAction(m_calibrationDock->toggleViewAction());
    viewMenu->addAction(m_layersDock->toggleViewAction());
    viewMenu->addSeparator();
    // These camera keys already worked but appeared nowhere in the UI, so the
    // only way to find them was to read the README.
    auto* topViewAction = viewMenu->addAction("Top-down view");
    topViewAction->setShortcut(QKeySequence("T"));
    connect(topViewAction, &QAction::triggered, m_viewport, &ViewportWidget::topView);
    auto* resetViewAction = viewMenu->addAction("Reset map view");
    resetViewAction->setShortcut(QKeySequence("R"));
    connect(resetViewAction, &QAction::triggered, m_viewport, &ViewportWidget::resetView);

    viewMenu->addSeparator();
    auto* resetLayoutAction = viewMenu->addAction("Reset workspace layout");
    resetLayoutAction->setObjectName("ResetLayoutAction");
    resetLayoutAction->setToolTip(
        "Put the panels back where they started, without restarting");
    connect(resetLayoutAction, &QAction::triggered, this, [this] {
        if (m_defaultLayout.isEmpty()) return;
        restoreState(m_defaultLayout);
        m_statusLabel->setText("  Workspace layout reset");
    });

    QMenu* helpMenu = menuBar()->addMenu("&Help");
    auto* shortcutsAction = helpMenu->addAction("&Keyboard Shortcuts…");
    shortcutsAction->setObjectName("ShortcutsAction");
    shortcutsAction->setShortcut(QKeySequence("F1"));
    connect(shortcutsAction, &QAction::triggered, this, [this] {
        ShortcutsDialog(this).exec();
    });

    // Bind the stack last, so the viewport and panel are both live before it
    // pushes its first state to them.
    m_viewport->setLayerStack(&m_layerStack);
    m_layersPanel->setStack(&m_layerStack);

    // Queued after the deferred dock re-add above, so what is captured is the
    // arrangement the user actually starts with.
    refreshRecentMenus();
    // Nothing is imported yet, so Clear starts disabled and the status line in
    // the vegetation menu says so rather than being blank.
    refreshCanopyActions();
    refreshVegetationActions();

    QTimer::singleShot(0, this, [this] {
        // Captured before any stored layout is applied, so Reset always returns
        // to the arrangement the application ships with.
        m_defaultLayout = saveState();
        const QByteArray geometry = WorkspaceSettings::layoutGeometry();
        const QByteArray state = WorkspaceSettings::layoutState();
        if (!geometry.isEmpty()) restoreGeometry(geometry);
        if (!state.isEmpty()) restoreState(state);
    });

    // Unsaved work is only as durable as this process, so keep a copy outside
    // it. Two minutes bounds what a crash can cost without writing constantly.
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setObjectName("AutosaveTimer");
    m_autosaveTimer->setInterval(120000);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveTick);
    m_autosaveTimer->start();

    // Coalesces map-texture rebuilds after vegetation edits, so holding Ctrl+Z
    // costs one rebuild rather than one per step.
    m_vegetationWashTimer = new QTimer(this);
    m_vegetationWashTimer->setObjectName("VegetationWashTimer");
    m_vegetationWashTimer->setSingleShot(true);
    m_vegetationWashTimer->setInterval(120);
    connect(m_vegetationWashTimer, &QTimer::timeout, this, &MainWindow::flushVegetationWash);

    // Queued so the window is visible before any recovery prompt appears over it.
    QTimer::singleShot(0, this, [this] { offerRecovery(); });

    refreshTree();

    connect(m_viewport, &ViewportWidget::terrainClicked,
            this, &MainWindow::onTerrainClicked);
    connect(m_viewport, &ViewportWidget::antennaClicked,
            this, &MainWindow::onAntennaSelected);
    connect(m_viewport, &ViewportWidget::antennaMoveRequested,
            this, &MainWindow::onAntennaMoved);
    connect(m_viewport, &ViewportWidget::terrainInspected,
            this, &MainWindow::onTerrainInspected);
    connect(m_viewport, &ViewportWidget::placementCancelled, this, [this]() {
        m_placementActive = false;
        m_statusLabel->setText("  Placement cancelled");
    });
}

void MainWindow::setupScene() {
    TerrainModel terrain = TerrainLoader::makeHills(200, 200, 10.0f, 40.0f);
    m_engine.setTerrain(std::move(terrain));
    m_terrainSource = TerrainSource{};
    placeDefaultAntennas();
    m_engine.loadMaterials(
        RuntimePaths::resolve("assets/materials.json").toStdString());
}

void MainWindow::captureScenarioBaseline() {
    const FieldGrid& current = m_engine.fieldGrid();
    if (!current.valid()) {
        m_comparisonPanel->setCurrentResultAvailable(false);
        m_statusLabel->setText(
            "  Run coverage before saving a scenario baseline");
        return;
    }

    m_baselineField = current;
    m_baselineTerrainRevision = m_terrainRevision;
    if (m_viewport) m_viewport->clearScenarioComparison();
    m_comparisonPanel->showBaselineAwaitingCurrent(true);
    m_comparisonDock->show();
    m_comparisonDock->raise();
    m_statusLabel->setText(
        "  Baseline saved · Change the scenario and run coverage again to compare");
}

void MainWindow::clearScenarioBaseline() {
    m_baselineField = {};
    m_baselineTerrainRevision = 0;
    if (m_viewport) m_viewport->clearScenarioComparison();
    if (m_comparisonPanel)
        m_comparisonPanel->showNoBaseline(m_engine.fieldGrid().valid());
    if (m_statusLabel)
        m_statusLabel->setText("  Scenario comparison baseline cleared");
}

void MainWindow::invalidateScenarioComparisonCurrent() {
    if (m_viewport) m_viewport->clearScenarioComparison();
    if (!m_comparisonPanel) return;
    if (m_baselineField.valid())
        m_comparisonPanel->showBaselineAwaitingCurrent(false);
    else
        m_comparisonPanel->showNoBaseline(false);
}

void MainWindow::resetScenarioComparisonForTerrain() {
    ++m_terrainRevision;
    m_baselineField = {};
    m_baselineTerrainRevision = 0;
    if (m_viewport) m_viewport->clearScenarioComparison();
    if (m_comparisonPanel) m_comparisonPanel->showNoBaseline(false);
    handleCalibrationTerrainChange();
}

void MainWindow::updateScenarioComparisonAfterSolve() {
    const FieldGrid& current = m_engine.fieldGrid();
    if (!m_comparisonPanel) return;
    if (!m_baselineField.valid()) {
        m_comparisonPanel->showNoBaseline(current.valid());
        if (m_viewport) m_viewport->clearScenarioComparison();
        return;
    }

    ScenarioComparisonResult comparison;
    if (m_baselineTerrainRevision != m_terrainRevision) {
        comparison.reason =
            "The analysis area changed after the baseline was saved. Save a new baseline for this location.";
    } else {
        comparison = ScenarioComparison::compare(m_baselineField, current);
    }

    m_comparisonPanel->setComparison(comparison);
    if (comparison.compatible) {
        m_viewport->setScenarioComparison(
            comparison.serviceTransition, true);
        m_comparisonPanel->setChangesVisible(true);
    } else {
        m_viewport->clearScenarioComparison();
    }
    m_comparisonDock->show();
    m_comparisonDock->raise();
}

void MainWindow::showCalibrationPanel() {
    if (!m_calibrationPanel || !m_calibrationDock) return;
    const double applied = m_engine.calibration.enabled
        ? static_cast<double>(m_engine.calibration.desiredPowerOffsetDb)
        : 0.0;
    m_calibrationPanel->setAppliedCorrection(applied);
    m_calibrationDock->show();
    m_calibrationDock->raise();
}

CalibrationPanel::Report MainWindow::calibrationAwaitingReport() const {
    CalibrationPanel::Report report;
    report.importedSamples = m_measurementRowsRead > 0
        ? m_measurementRowsRead : m_measurements.size();
    report.rejectedSamples = m_measurementRejectedRows;
    report.appliedCorrectionDb = m_engine.calibration.enabled
        ? static_cast<double>(m_engine.calibration.desiredPowerOffsetDb)
        : 0.0;
    report.warnings = m_measurementImportWarnings;
    return report;
}

void MainWindow::invalidateCalibrationCurrent() {
    m_calibrationReportCurrent = false;
    m_calibrationReport = {};
    if (!m_calibrationPanel) return;
    if (m_measurements.empty()) {
        m_calibrationPanel->showNoMeasurements();
        m_calibrationPanel->setAppliedCorrection(
            m_engine.calibration.enabled
                ? m_engine.calibration.desiredPowerOffsetDb : 0.0);
    } else {
        m_calibrationPanel->showAwaitingCoverage(calibrationAwaitingReport());
    }
}

void MainWindow::updateCalibrationAfterSolve() {
    if (!m_calibrationPanel) return;
    if (m_measurements.empty()) {
        m_calibrationReportCurrent = false;
        m_calibrationPanel->showNoMeasurements();
        m_calibrationPanel->setAppliedCorrection(
            m_engine.calibration.enabled
                ? m_engine.calibration.desiredPowerOffsetDb : 0.0);
        return;
    }
    if (!m_engine.fieldGrid().valid()) {
        invalidateCalibrationCurrent();
        return;
    }

    MeasurementCalibrationOptions options;
    options.expectedReceiverHeightAglM =
        m_engine.receiver.receiverHeightAglM;
    m_calibrationReport = MeasurementCalibration::evaluate(
        m_measurements, m_engine.fieldGrid(), m_engine.terrain(),
        m_engine.antennas(), options);
    m_calibrationReport.imported = m_measurementRowsRead > 0
        ? m_measurementRowsRead : m_measurements.size();
    m_calibrationReport.rejected += m_measurementRejectedRows;
    for (const QString& warning : m_measurementImportWarnings)
        m_calibrationReport.warnings.push_back(warning.toStdString());
    m_calibrationReportCurrent = true;
    m_calibrationPanel->setCalibrationReport(
        m_calibrationReport,
        m_engine.calibration.enabled
            ? m_engine.calibration.desiredPowerOffsetDb : 0.0);
}

void MainWindow::applyCalibrationCorrection() {
    if (!m_calibrationReportCurrent || !m_calibrationReport.canApply() ||
        !std::isfinite(m_calibrationReport.meanBiasDb)) {
        QMessageBox::information(
            this, "Calibration not ready",
            "Run coverage with at least three usable field measurements before applying a correction.");
        return;
    }

    const double current = m_engine.calibration.enabled
        ? static_cast<double>(m_engine.calibration.desiredPowerOffsetDb)
        : 0.0;
    const double corrected = current + m_calibrationReport.meanBiasDb;
    if (!std::isfinite(corrected) || std::abs(corrected) > 100.0) {
        QMessageBox::warning(
            this, "Correction not applied",
            "The recommended total correction is outside the supported ±100 dB range. Check the measurement units, frequency, receiver height, and analysis area.");
        return;
    }

    m_engine.calibration.enabled = std::abs(corrected) >= 0.005;
    m_engine.calibration.desiredPowerOffsetDb =
        m_engine.calibration.enabled ? static_cast<float>(corrected) : 0.0f;
    m_appliedCalibrationSourceName = QFileInfo(m_measurementPath).fileName();
    m_appliedCalibrationPairedSamples = m_calibrationReport.paired;
    m_appliedCalibrationRmseBeforeDb =
        static_cast<float>(m_calibrationReport.rmseBeforeDb);
    m_appliedCalibrationRmseAfterDb =
        static_cast<float>(m_calibrationReport.rmseAfterBiasDb);

    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    invalidateScenarioComparisonCurrent();
    if (m_pointPanel) m_pointPanel->clear();
    if (m_pointDock) m_pointDock->hide();
    if (m_viewport) {
        m_viewport->clearAnalysisPoint();
        m_viewport->refreshOverlays();
    }
    invalidateCalibrationCurrent();
    markScenarioDirty();
    showCalibrationPanel();
    m_statusLabel->setText(QString(
        "  Calibration correction %1%2 dB applied · Run coverage to refresh results")
        .arg(corrected > 0.0 ? "+" : "")
        .arg(corrected, 0, 'f', 2));
}

void MainWindow::resetCalibrationCorrection() {
    m_engine.calibration = {};
    m_appliedCalibrationSourceName.clear();
    m_appliedCalibrationPairedSamples = 0;
    m_appliedCalibrationRmseBeforeDb =
        std::numeric_limits<float>::quiet_NaN();
    m_appliedCalibrationRmseAfterDb =
        std::numeric_limits<float>::quiet_NaN();

    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    invalidateScenarioComparisonCurrent();
    if (m_pointPanel) m_pointPanel->clear();
    if (m_pointDock) m_pointDock->hide();
    if (m_viewport) {
        m_viewport->clearAnalysisPoint();
        m_viewport->refreshOverlays();
    }
    invalidateCalibrationCurrent();
    markScenarioDirty();
    showCalibrationPanel();
    m_statusLabel->setText(
        "  Calibration correction reset · Run coverage to refresh results");
}

void MainWindow::clearCalibrationMeasurements() {
    m_measurements.clear();
    m_calibrationReport = {};
    m_measurementPath.clear();
    m_measurementRowsRead = 0;
    m_measurementRejectedRows = 0;
    m_measurementImportWarnings.clear();
    m_calibrationReportCurrent = false;
    if (m_calibrationPanel) {
        m_calibrationPanel->showNoMeasurements();
        m_calibrationPanel->setAppliedCorrection(
            m_engine.calibration.enabled
                ? m_engine.calibration.desiredPowerOffsetDb : 0.0);
    }
    if (m_statusLabel) {
        m_statusLabel->setText(m_engine.calibration.enabled
            ? "  Field measurements cleared · Applied correction retained"
            : "  Field measurements cleared");
    }
}

// ---------------------------------------------------------------------------
// Vegetation
// ---------------------------------------------------------------------------

QString MainWindow::canopySummary() const {
    if (!m_canopy) return QStringLiteral("No canopy data loaded");
    const CanopyProvenance& source = m_canopy->provenance;
    const CanopyCoverage& coverage = m_canopy->coverage;
    QString text = QString::fromStdString(source.productVintage);
    text += QString(" · %1% of the area is vegetated")
                .arg(100.0 * coverage.vegetatedFraction(), 0, 'f', 0);
    if (coverage.assignedHeight > 0 &&
        m_canopyOptions.developed != landfire::DevelopedPolicy::TreatAsBare)
        text += QString(" · %1% of it at an assumed height")
                    .arg(100.0 * coverage.assignedFraction(), 0, 'f', 0);
    return text;
}

void MainWindow::refreshCanopyActions() {
    if (m_clearCanopyAct) m_clearCanopyAct->setEnabled(static_cast<bool>(m_canopy));
    if (m_canopyStatusAct) {
        QString text = canopySummary();
        const std::size_t drawn = m_engine.vegetation().size();
        if (drawn > 0) text += QString(" · %1 drawn area(s)").arg(drawn);
        m_canopyStatusAct->setText(text);
    }
}

void MainWindow::installCanopy(CanopyRaster raster) {
    auto shared = std::make_shared<const CanopyRaster>(std::move(raster));
    m_canopy = shared;

    CanopySourceInfo info;
    info.displayName = shared->provenance.displayName;
    info.vintage = shared->provenance.productVintage;
    info.licence = shared->provenance.licence;
    info.attribution = shared->provenance.attribution;
    info.retrievedUtc = shared->provenance.retrievedUtc;
    info.sourceResolutionM = shared->provenance.sourceResolutionM;
    info.vegetatedSamples = shared->coverage.vegetated;
    info.assignedSamples = shared->coverage.assignedHeight;

    // The sampler holds the raster, so it stays alive for exactly as long as
    // the engine needs it and is re-placed on every later terrain load.
    m_engine.setImportedCanopy(
        [shared](const TerrainModel& terrain,
                 std::vector<std::uint16_t>& heightDm,
                 std::vector<std::uint8_t>& coverPct) {
            return shared->sampleOnto(terrain, heightDm, coverPct);
        },
        std::move(info));

    // Vegetation changes every predicted path, so whatever is on screen was
    // computed for a different scene.
    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    invalidateScenarioComparisonCurrent();
    invalidateCalibrationCurrent();
    markScenarioDirty();
    refreshCanopyActions();
    if (m_viewport) m_viewport->refreshLayers();
    refreshBottomBar();
}

void MainWindow::onLoadCanopy() {
    if (m_solveInProgress || m_terrainLoadInProgress) return;
    if (!m_engine.terrain().hasGeoReference()) {
        QMessageBox::information(this, "Canopy needs real-world terrain",
            "Vegetation is fetched for the ground the terrain covers, so the "
            "terrain has to know where it is. Choose a location first, then "
            "load canopy for it.\n\nAnywhere, including terrain without a real-world "
            "location, you can draw stands and clearings by hand: Analysis ▸ "
            "Vegetation ▸ Draw Stands.");
        return;
    }

    // The downloader runs a nested event loop. Reuse the scene-load guard so
    // autosave, close and other scene operations cannot observe a half-applied
    // options/raster pair while the request is outstanding.
    m_terrainLoadInProgress = true;
    const auto loadingGuard = qScopeGuard([this] { m_terrainLoadInProgress = false; });
    QProgressDialog progress("Contacting the LANDFIRE image service…", "Cancel",
                             0, 0, this);
    progress.setWindowTitle("Loading canopy");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumWidth(460);
    progress.show();

    QNetworkAccessManager network;
    const CanopyLoader::Result result = CanopyLoader::loadForTerrain(
        network, m_engine.terrain(), m_canopyOptions,
        [&progress](int completed, int total) {
            progress.setRange(0, std::max(total, 1));
            progress.setValue(completed);
            progress.setLabelText(QString("Canopy layers · %1 / %2")
                                      .arg(completed).arg(total));
        },
        [&progress] { return progress.wasCanceled(); });
    const bool cancelled = progress.wasCanceled();
    progress.close();

    if (cancelled) {
        m_statusLabel->setText("  Canopy loading cancelled");
        return;
    }
    if (!result.ok()) {
        m_statusLabel->setText("  Canopy not loaded · Existing vegetation kept");
        // This is the moment an operator outside LANDFIRE's coverage learns
        // import will not help, so it says what will.
        const QString reason = result.error.isEmpty()
            ? QStringLiteral("The canopy layer came back unusable.") : result.error;
        QMessageBox::warning(this, "Could not load canopy",
                             reason + QStringLiteral("\n\nYou can draw stands and clearings "
                                                     "by hand instead: Analysis ▸ Vegetation "
                                                     "▸ Draw Stands."));
        return;
    }

    const CanopyProvenance source = result.canopy.provenance;
    const CanopyCoverage coverage = result.canopy.coverage;
    installCanopy(result.canopy);

    m_statusLabel->setText("  " + canopySummary());

    // Everything an operator needs to judge the number, in one place: what the
    // data is, how old, what licence, how much of it was substituted, and that
    // the attenuation model is an extrapolation rather than a standard.
    QString detail = QString(
        "<b>%1</b><br>%2 · %3 m ground sample · retrieved %4<br>%5<br><br>")
        .arg(QString::fromStdString(source.displayName))
        .arg(QString::fromStdString(source.productVintage))
        .arg(source.sourceResolutionM, 0, 'f', 0)
        .arg(QString::fromStdString(source.retrievedUtc))
        .arg(QString::fromStdString(source.licence));
    detail += QString("%1% of this area carries canopy.")
                  .arg(100.0 * coverage.vegetatedFraction(), 0, 'f', 0);
    if (!result.note.isEmpty()) detail += "<br>" + result.note + ".";
    if (coverage.assignedHeight > 0 &&
        m_canopyOptions.developed == landfire::DevelopedPolicy::AssumeHeight) {
        detail += QString(
            "<br><br>Over developed land LANDFIRE assigns a fixed canopy height "
            "rather than measuring one — a nationwide 39 m for evergreen and "
            "mixed forest, 19 m for deciduous. Those cells have been replaced "
            "with an assumed %1 m. Change this under Analysis ▸ Vegetation ▸ "
            "Canopy Options.")
            .arg(m_canopyOptions.assumedDevelopedCanopyM, 0, 'f', 1);
    }
    detail += QStringLiteral(
        "<br><br>The vegetation attenuation model is an engineering "
        "extrapolation, not an ITU-R P.833 procedure: the standard has no "
        "sanctioned terrestrial model for woodland with both terminals outside "
        "it. Nothing here is validated against field measurements.");

    QMessageBox note(QMessageBox::Information, "Canopy loaded", detail,
                     QMessageBox::Ok, this);
    note.setTextFormat(Qt::RichText);
    note.exec();
}

void MainWindow::onClearCanopy() {
    if (!m_canopy) return;
    m_canopy.reset();
    m_engine.clearImportedCanopy();
    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    invalidateScenarioComparisonCurrent();
    invalidateCalibrationCurrent();
    markScenarioDirty();
    refreshCanopyActions();
    if (m_viewport) m_viewport->refreshLayers();
    refreshBottomBar();
    m_statusLabel->setText(m_engine.terrain().hasVegetation()
        ? "  Imported canopy cleared · Hand-drawn vegetation kept"
        : "  Imported canopy cleared");
}

void MainWindow::onCanopyOptions() {
    if (m_solveInProgress || m_terrainLoadInProgress) return;
    QDialog dialog(this);
    dialog.setObjectName("CanopyOptionsDialog");
    dialog.setWindowTitle("Canopy options");
    auto* form = new QFormLayout(&dialog);

    auto* policy = new QComboBox(&dialog);
    policy->setObjectName("DevelopedCanopyPolicy");
    policy->setAccessibleName("Developed land canopy policy");
    policy->addItem("Substitute an assumed height",
                    static_cast<int>(landfire::DevelopedPolicy::AssumeHeight));
    policy->addItem("Use the provider's value unchanged",
                    static_cast<int>(landfire::DevelopedPolicy::UseSourceValue));
    policy->addItem("Treat as bare ground",
                    static_cast<int>(landfire::DevelopedPolicy::TreatAsBare));
    policy->setCurrentIndex(policy->findData(
        static_cast<int>(m_canopyOptions.developed)));

    auto* assumed = new QDoubleSpinBox(&dialog);
    assumed->setObjectName("AssumedCanopyHeight");
    assumed->setAccessibleName("Assumed canopy height in metres");
    assumed->setRange(0.0, 60.0);
    assumed->setDecimals(1);
    assumed->setSingleStep(0.5);
    assumed->setSuffix(" m");
    assumed->setValue(m_canopyOptions.assumedDevelopedCanopyM);

    auto* explanation = new QLabel(
        "Over developed land LANDFIRE does not measure canopy height. It assigns "
        "a nationwide constant by vegetation type — 39 m for evergreen and mixed "
        "forest, 19 m for deciduous — which would paint a tall forest across "
        "every suburb. Ten metres is a mature residential tree, and is an "
        "assumption rather than a measurement.", &dialog);
    explanation->setWordWrap(true);
    explanation->setMinimumWidth(420);

    form->addRow(explanation);
    form->addRow("Developed land", policy);
    form->addRow("Assumed canopy height", assumed);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    const auto syncEnabled = [policy, assumed] {
        assumed->setEnabled(policy->currentData().toInt() ==
                            static_cast<int>(landfire::DevelopedPolicy::AssumeHeight));
    };
    connect(policy, &QComboBox::currentIndexChanged, &dialog, syncEnabled);
    syncEnabled();

    if (dialog.exec() != QDialog::Accepted) return;

    const auto chosen =
        static_cast<landfire::DevelopedPolicy>(policy->currentData().toInt());
    const float chosenHeight = static_cast<float>(assumed->value());
    if (chosen == m_canopyOptions.developed &&
        std::abs(chosenHeight - m_canopyOptions.assumedDevelopedCanopyM) < 1e-3f)
        return;

    auto proposed = m_canopyOptions;
    proposed.developed = chosen;
    proposed.assumedDevelopedCanopyM = chosenHeight;

    // The policy is applied while decoding, so a loaded canopy has to be
    // rebuilt. The request is unchanged, so this is a cache read rather than a
    // second download.
    if (!m_canopy) {
        m_canopyOptions = proposed;
        m_statusLabel->setText("  Canopy options saved");
        return;
    }
    const auto previousCanopy = m_canopy;
    replaceCanopyOptions(m_canopyOptions, proposed, [&] {
        onLoadCanopy();
        return m_canopy != previousCanopy;
    });
    refreshCanopyActions();
}

void MainWindow::restoreCanopyImport(const CanopyImport& record) {
    if (!record.enabled) return;
    if (!m_engine.terrain().hasGeoReference()) return;

    m_canopyOptions = canopyOptionsForImport(record);

    QNetworkAccessManager network;
    const CanopyLoader::Result result =
        CanopyLoader::loadForTerrain(network, m_engine.terrain(), m_canopyOptions);
    if (!result.ok()) {
        // Opening a scenario must not fail because vegetation could not be
        // restored, but the operator has to know the scene is missing it.
        if (m_statusLabel)
            m_statusLabel->setText("  Scenario opened · Canopy could not be "
                                   "restored, so vegetation is missing");
        return;
    }

    // Putting back what the file already recorded is not an edit. The caller
    // decides whether the loaded document counts as clean - opening a scenario
    // does, restoring a crash recovery does not - so this must not overrule it
    // in either direction.
    const bool wasDirty = m_scenarioDirty;
    installCanopy(result.canopy);
    m_scenarioDirty = wasDirty;
    setWindowModified(isScenarioDirty());
}

// ---------------------------------------------------------------------------
// Drawn vegetation
// ---------------------------------------------------------------------------

QString MainWindow::vegetationToolHint(VegetationKind kind) const {
    return kind == VegetationKind::Clearing
        ? QStringLiteral("  Drawing clearings · drag to mark felled ground · a clearing removes "
                         "any stand or imported canopy beneath it · click an area to select it "
                         "· right-drag orbits · Esc stops")
        : QStringLiteral("  Drawing stands · drag on the map to outline one · click an area to "
                         "select it · right-drag orbits · Esc stops");
}

void MainWindow::refreshVegetationActions() {
    const auto& areas = m_engine.vegetation();
    const bool haveSelection = m_selectedVegetationIndex >= 0 &&
        m_selectedVegetationIndex < static_cast<int>(areas.size());
    if (m_editVegetationAct) m_editVegetationAct->setEnabled(haveSelection);
    if (m_deleteVegetationAct) m_deleteVegetationAct->setEnabled(haveSelection);
    if (m_deleteOffTerrainVegetationAct)
        m_deleteOffTerrainVegetationAct->setEnabled(
            VegetationAreas::countOutside(m_engine.terrain(), areas) > 0);
}

void MainWindow::scheduleVegetationWashRefresh() {
    if (m_vegetationWashTimer)
        m_vegetationWashTimer->start();   // restarts, so a burst costs one rebuild
    else if (m_viewport)
        m_viewport->refreshLayers();
}

void MainWindow::flushVegetationWash() {
    if (m_vegetationWashTimer) m_vegetationWashTimer->stop();
    if (m_solveInProgress || m_terrainLoadInProgress) {
        // Try again once the running operation has finished rather than
        // rebuilding the map texture underneath it.
        if (m_vegetationWashTimer) m_vegetationWashTimer->start();
        return;
    }
    if (m_viewport) m_viewport->refreshLayers();
}

QString MainWindow::vegetationEffectSummary(const VegetationArea& area) const {
    const VegetationAreas::Footprint fp =
        VegetationAreas::footprint(m_engine.terrain(), area);
    if (fp.empty) return QStringLiteral("lies outside this terrain and has no effect");
    // The ground the solver paints, which rounds outward from the outline.
    return QStringLiteral("solves as %1 × %2 m (%3 × %4 samples)")
        .arg(static_cast<double>(fp.maxX - fp.minX), 0, 'f', 0)
        .arg(static_cast<double>(fp.maxZ - fp.minZ), 0, 'f', 0)
        .arg(fp.x1 - fp.x0 + 1)
        .arg(fp.z1 - fp.z0 + 1);
}

float MainWindow::defaultStandHeightM(const VegetationArea& area, bool& fromImport) const {
    fromImport = false;
    std::vector<std::uint16_t> heights;
    std::vector<std::uint8_t> covers;
    if (m_engine.hasImportedCanopy() && m_engine.sampleImportedCanopy(heights, covers)) {
        const VegetationAreas::ImportedUnder under =
            VegetationAreas::importedHeightUnder(m_engine.terrain(), area, heights);
        if (under.samples > 0 && under.vegetated * 2 >= under.samples) {
            fromImport = true;
            return std::clamp(std::round(under.medianM * 2.0f) / 2.0f, 0.5f,
                              VegetationAreas::kMaxCanopyHeightM);
        }
    }
    return m_lastStandHeightM;
}

std::string MainWindow::nextVegetationName(VegetationKind kind) const {
    const std::string stem = kind == VegetationKind::Clearing ? "Clearing" : "Stand";
    const auto& areas = m_engine.vegetation();
    for (int n = 1;; ++n) {
        const std::string candidate = stem + " " + std::to_string(n);
        const bool taken = std::any_of(areas.begin(), areas.end(),
            [&candidate](const VegetationArea& area) { return area.name == candidate; });
        if (!taken) return candidate;
    }
}

void MainWindow::applyVegetationState(const std::vector<VegetationArea>& areas, int selected) {
    const std::uint64_t revisionBefore = m_engine.canopyRevision();
    // The solver reads the canopy grid, not the area list, so a result survives
    // any change that leaves the grid identical: a rename, which the engine does
    // not even rebuild for, or deleting areas that lie off this terrain and paint
    // nothing, which it does rebuild for. Anything that changes a single sample
    // clears it.
    const auto heightsBefore = m_engine.terrain().canopyHeightDm;
    const auto coverBefore = m_engine.terrain().canopyCoverPct;
    m_engine.setVegetation(areas);
    const bool rebuilt = m_engine.canopyRevision() != revisionBefore;
    const bool canopyChanged = rebuilt &&
        (m_engine.terrain().canopyHeightDm != heightsBefore ||
         m_engine.terrain().canopyCoverPct != coverBefore);
    // The map texture is keyed on the revision, so it follows every rebuild.
    if (rebuilt) scheduleVegetationWashRefresh();
    if (canopyChanged) {
        m_engine.setFieldGrid({});
        m_solvedSignature.clear();
        invalidateScenarioComparisonCurrent();
        invalidateCalibrationCurrent();
        if (m_pointPanel) m_pointPanel->clear();
        if (m_pointDock) m_pointDock->hide();
        if (m_viewport) {
            m_viewport->clearAnalysisPoint();
            // Without this the old coverage texture keeps drawing.
            m_viewport->refreshOverlays();
        }
    }

    const int count = static_cast<int>(m_engine.vegetation().size());
    m_selectedVegetationIndex =
        (m_viewport && m_viewport->vegetationDrawMode() && selected >= 0 && selected < count)
        ? selected : -1;
    if (m_viewport) {
        m_viewport->setVegetationSelection(m_selectedVegetationIndex);
        // Also cancels any gesture, which is what makes an undo mid-drag safe.
        m_viewport->refreshVegetationAreas();
    }
    refreshVegetationActions();
    refreshCanopyActions();
}

void MainWindow::pushVegetationChange(std::vector<VegetationArea> after, int selectedAfter,
                                      const QString& description) {
    if (m_solveInProgress || m_terrainLoadInProgress) {
        if (m_statusLabel)
            m_statusLabel->setText(
                "  Wait for the current operation to finish before changing vegetation");
        return;
    }
    if (!m_undoStack) {
        applyVegetationState(after, selectedAfter);
        return;
    }
    // No merging: every vegetation change is one discrete commit, so an index
    // shifted by a delete can never fold an edit into a different area.
    m_undoStack->push(new VegetationStateCommand(
        m_engine.vegetation(), m_selectedVegetationIndex, std::move(after), selectedAfter,
        description, -1,
        [this](const std::vector<VegetationArea>& state, int selected) {
            applyVegetationState(state, selected);
        }));
}

void MainWindow::onVegetationDrawModeChanged(bool active, int kind) {
    const bool stands = active && kind == static_cast<int>(VegetationKind::Stand);
    const bool clearings = active && kind == static_cast<int>(VegetationKind::Clearing);
    if (m_drawStandsAct && m_drawStandsAct->isChecked() != stands) {
        const QSignalBlocker blocker(m_drawStandsAct);
        m_drawStandsAct->setChecked(stands);
    }
    if (m_drawClearingsAct && m_drawClearingsAct->isChecked() != clearings) {
        const QSignalBlocker blocker(m_drawClearingsAct);
        m_drawClearingsAct->setChecked(clearings);
    }
    if (!active) {
        m_selectedVegetationIndex = -1;
        refreshVegetationActions();
        // Measuring or placement taking over writes its own status afterwards;
        // a load in progress owns the status line.
        if (!m_terrainLoadInProgress && m_statusLabel) m_statusLabel->setText("  Ready");
        return;
    }
    if (m_statusLabel)
        m_statusLabel->setText(vegetationToolHint(static_cast<VegetationKind>(kind)));
}

void MainWindow::onVegetationAreaDrawn(float minX, float minZ, float maxX, float maxZ) {
    if (m_solveInProgress || m_terrainLoadInProgress) return;
    if (!m_viewport || !m_viewport->vegetationDrawMode()) return;
    const TerrainModel& terrain = m_engine.terrain();
    const glm::vec3 world = terrain.worldSize();
    const VegetationGeometry::Rect rect = VegetationGeometry::fromCorners(
        {minX, minZ}, {maxX, maxZ}, {world.x, world.z});
    if (!VegetationGeometry::coversACell(rect, terrain)) {
        m_statusLabel->setText(QString("  Area not drawn · smaller than one %1 m terrain cell")
            .arg(static_cast<double>(std::max(terrain.metersPerCellX(), terrain.metersPerCellZ())),
                 0, 'f', 0));
        return;
    }

    VegetationArea area;
    area.minX = rect.minX;
    area.minZ = rect.minZ;
    area.maxX = rect.maxX;
    area.maxZ = rect.maxZ;
    area.kind = m_viewport->vegetationDrawKind();
    area.name = nextVegetationName(area.kind);
    bool fromImport = false;
    area.canopyHeightM = area.kind == VegetationKind::Clearing
        ? 0.0f : defaultStandHeightM(area, fromImport);
    const VegetationAreas::Footprint fp = VegetationAreas::footprint(terrain, area);

    std::vector<VegetationArea> after = m_engine.vegetation();
    after.push_back(area);
    const int selectedAfter = static_cast<int>(after.size()) - 1;
    pushVegetationChange(std::move(after), selectedAfter,
                         area.kind == VegetationKind::Clearing ? "Draw clearing" : "Draw stand");

    if (area.kind == VegetationKind::Clearing) {
        m_statusLabel->setText(QString("  Clearing drawn · clears %1 × %2 samples · Ctrl+Z undoes")
            .arg(fp.x1 - fp.x0 + 1).arg(fp.z1 - fp.z0 + 1));
    } else if (fromImport) {
        m_statusLabel->setText(QString("  Stand drawn · %1 m, the median imported canopy under "
                                       "it · Enter changes it · Ctrl+Z undoes")
            .arg(static_cast<double>(area.canopyHeightM), 0, 'f', 1));
    } else {
        // Said out loud, because a solve run on this number rests on an
        // assumption the operator has not yet confirmed.
        m_statusLabel->setText(QString("  Stand drawn · %1 m assumed, not measured · Enter sets "
                                       "its height · Ctrl+Z undoes")
            .arg(static_cast<double>(area.canopyHeightM), 0, 'f', 1));
    }
}

void MainWindow::onVegetationAreaReshaped(int index, float minX, float minZ, float maxX,
                                          float maxZ, bool resized) {
    if (m_solveInProgress || m_terrainLoadInProgress) return;
    const auto& areas = m_engine.vegetation();
    if (index != m_selectedVegetationIndex || index < 0 ||
        index >= static_cast<int>(areas.size()))
        return;
    const TerrainModel& terrain = m_engine.terrain();
    const VegetationGeometry::Rect rect{std::min(minX, maxX), std::min(minZ, maxZ),
                                        std::max(minX, maxX), std::max(minZ, maxZ)};
    if (!VegetationGeometry::coversACell(rect, terrain)) {
        m_statusLabel->setText(QString("  Not changed · smaller than one %1 m terrain cell")
            .arg(static_cast<double>(std::max(terrain.metersPerCellX(), terrain.metersPerCellZ())),
                 0, 'f', 0));
        return;
    }
    const VegetationArea& current = areas[static_cast<std::size_t>(index)];
    const auto moved = [](float a, float b) { return std::abs(a - b) > 1.0e-3f; };
    if (!moved(rect.minX, current.minX) && !moved(rect.minZ, current.minZ) &&
        !moved(rect.maxX, current.maxX) && !moved(rect.maxZ, current.maxZ))
        return;

    std::vector<VegetationArea> after = areas;
    VegetationArea& changed = after[static_cast<std::size_t>(index)];
    changed.minX = rect.minX;
    changed.minZ = rect.minZ;
    changed.maxX = rect.maxX;
    changed.maxZ = rect.maxZ;
    pushVegetationChange(std::move(after), index,
                         resized ? "Resize vegetation area" : "Move vegetation area");
    onVegetationAreaClicked(index);
}

void MainWindow::onVegetationAreaClicked(int index) {
    const auto& areas = m_engine.vegetation();
    if (index < 0 || index >= static_cast<int>(areas.size())) index = -1;
    m_selectedVegetationIndex = index;
    if (m_viewport) m_viewport->setVegetationSelection(index);
    refreshVegetationActions();
    if (!m_statusLabel) return;
    if (index < 0) {
        if (m_viewport && m_viewport->vegetationDrawMode())
            m_statusLabel->setText(vegetationToolHint(m_viewport->vegetationDrawKind()));
        return;
    }
    const VegetationArea& area = areas[static_cast<std::size_t>(index)];
    const QString kindText = area.kind == VegetationKind::Clearing
        ? QStringLiteral("clearing")
        : QStringLiteral("stand %1 m").arg(static_cast<double>(area.canopyHeightM), 0, 'f', 1);
    m_statusLabel->setText(
        QString("  %1 · %2 · %3 · drag the middle to move, a corner to resize · Enter edits "
                "· Delete removes")
            .arg(QString::fromStdString(area.name), kindText, vegetationEffectSummary(area)));
}

void MainWindow::onDeleteVegetationArea() {
    if (m_solveInProgress || m_terrainLoadInProgress) return;
    const auto& areas = m_engine.vegetation();
    const int index = m_selectedVegetationIndex;
    if (index < 0 || index >= static_cast<int>(areas.size())) return;
    const QString name = QString::fromStdString(areas[static_cast<std::size_t>(index)].name);
    std::vector<VegetationArea> after = areas;
    after.erase(after.begin() + index);
    pushVegetationChange(std::move(after), -1, "Delete vegetation area");
    m_statusLabel->setText(QString("  %1 deleted · Ctrl+Z restores it").arg(name));
}

void MainWindow::onEditVegetationArea() {
    if (m_solveInProgress || m_terrainLoadInProgress) return;
    const int index = m_selectedVegetationIndex;
    if (index < 0 || index >= static_cast<int>(m_engine.vegetation().size())) return;
    const VegetationArea original = m_engine.vegetation()[static_cast<std::size_t>(index)];

    // On the stack and gone when exec returns, so it never lingers as a second
    // spin box under the main window.
    VegetationAreaDialog dialog(original, vegetationEffectSummary(original),
                                m_lastStandHeightM, this);
    if (dialog.exec() != QDialog::Accepted) return;
    if (index >= static_cast<int>(m_engine.vegetation().size())) return;

    const VegetationArea edited = dialog.area();
    const bool changed = edited.name != original.name || edited.kind != original.kind ||
        std::lround(edited.canopyHeightM * 10.0f) != std::lround(original.canopyHeightM * 10.0f);
    if (!changed) return;
    // Clamped: a renamed stand can keep a stored height outside what a new one
    // should start from.
    if (edited.kind == VegetationKind::Stand)
        m_lastStandHeightM = std::clamp(edited.canopyHeightM, 0.5f, VegetationAreas::kMaxCanopyHeightM);

    std::vector<VegetationArea> after = m_engine.vegetation();
    after[static_cast<std::size_t>(index)] = edited;
    pushVegetationChange(std::move(after), index, "Edit vegetation area");
    onVegetationAreaClicked(index);
}

void MainWindow::onDeleteOffTerrainVegetation() {
    if (m_solveInProgress || m_terrainLoadInProgress) return;
    const TerrainModel& terrain = m_engine.terrain();
    std::vector<VegetationArea> after;
    int removed = 0;
    for (const VegetationArea& area : m_engine.vegetation()) {
        if (area.valid() && VegetationAreas::footprint(terrain, area).empty) {
            ++removed;
            continue;
        }
        after.push_back(area);
    }
    if (removed == 0) return;
    pushVegetationChange(std::move(after), -1, "Delete off-terrain vegetation areas");
    m_statusLabel->setText(
        QString("  %1 drawn area(s) outside this terrain deleted · Ctrl+Z restores them")
            .arg(removed));
}

void MainWindow::handleCalibrationTerrainChange() {
    if (m_measurements.empty()) {
        invalidateCalibrationCurrent();
        return;
    }

    const auto firstLocal = std::stable_partition(
        m_measurements.begin(), m_measurements.end(),
        [](const RFMeasurement& measurement) {
            return measurement.coordinateKind == MeasurementCoordinateKind::Wgs84;
        });
    const std::size_t removed = static_cast<std::size_t>(
        std::distance(firstLocal, m_measurements.end()));
    m_measurements.erase(firstLocal, m_measurements.end());
    if (removed > 0) {
        m_measurementRejectedRows += removed;
        m_measurementImportWarnings.push_back(QString(
            "%1 local X/Z measurement(s) were removed because the analysis area changed; WGS84 measurements were retained.")
            .arg(removed));
    }
    if (m_measurements.empty()) {
        m_measurementPath.clear();
        m_measurementRowsRead = 0;
        m_measurementRejectedRows = 0;
        m_measurementImportWarnings.clear();
    }
    invalidateCalibrationCurrent();
}

void MainWindow::placeDefaultAntennas() {
    if (!m_engine.antennas().empty()) return;
    const auto& t = m_engine.terrain();
    const float w = t.worldSize().x;
    const float d = t.worldSize().z;
    Antenna a1;
    a1.position = {w * 0.5f, t.heightAtWorld(w*0.5f, d*0.5f) + 10.0f, d * 0.5f};
    a1.frequencyMHz = 150.0f;
    a1.powerW = 50.0f;
    a1.gainDbi = 3.0f;
    m_engine.setAntennas({a1});
}

void MainWindow::loadGlobalLocation(double latitude, double longitude,
                                    double extentKm, const QString& name) {
    if (m_terrainLoadInProgress || m_solveInProgress) return;
    m_terrainLoadInProgress = true;
    const auto loadingGuard = qScopeGuard([this] {
        m_terrainLoadInProgress = false;
        if (m_viewport) m_viewport->setBasemapLoadCallbacks({}, {});
    });
    bool terrainInstalled = false;
    m_statusLabel->setText("  Loading real-world elevation…");

    QProgressDialog loadProgress("Preparing map data…", "Cancel", 0, 0, this);
    loadProgress.setWindowTitle("Loading analysis area");
    loadProgress.setWindowModality(Qt::WindowModal);
    loadProgress.setMinimumDuration(0);
    loadProgress.setAutoClose(false);
    loadProgress.setAutoReset(false);
    loadProgress.setMinimumWidth(460);
    loadProgress.show();

    try {
        m_placementActive = false;
        if (m_viewport) m_viewport->setPlacementMode(false);
        if (m_viewport) m_viewport->setVegetationDrawMode(false);

        auto terrainProgress = [&loadProgress](int completed, int total) {
            loadProgress.setRange(0, std::max(total, 1));
            loadProgress.setValue(completed);
            loadProgress.setLabelText(QString("Elevation tiles · %1 / %2")
                .arg(completed).arg(total));
        };
        auto cancelLoad = [&loadProgress]() { return loadProgress.wasCanceled(); };

        TerrainModel terrain = GlobalTerrainLoader::load(
            latitude, longitude, extentKm, 513, terrainProgress, cancelLoad);
        if (loadProgress.wasCanceled()) {
            m_statusLabel->setText("  Location loading cancelled");
            loadProgress.close();
            return;
        }

        m_engine.setTerrain(std::move(terrain));
        terrainInstalled = true;
        // Recorded on install, not on request, so a cancelled or failed load
        // never enters the history.
        WorkspaceSettings::addRecentLocation({name, latitude, longitude, extentKm});
        refreshRecentMenus();
        markScenarioDirty();
        m_engine.setFieldGrid({});
        m_solvedSignature.clear();
    m_solvedSignature.clear();
        resetScenarioComparisonForTerrain();
        if (m_undoStack) m_undoStack->clear();
        if (m_pointPanel) m_pointPanel->clear();
        if (m_pointDock) m_pointDock->hide();
        m_terrainSource = TerrainSource{};
        m_terrainSource.type = TerrainSource::Type::Global;
        m_terrainSource.centerLat = latitude;
        m_terrainSource.centerLon = longitude;
        m_terrainSource.extentKm = extentKm;
        m_locationName = name;
        placeDefaultAntennas();

        if (m_viewport) {
            m_viewport->setBasemapLoadCallbacks(
                [&loadProgress](int completed, int total) {
                    loadProgress.setRange(0, std::max(total, 1));
                    loadProgress.setValue(completed);
                    loadProgress.setLabelText(QString("Satellite imagery · %1 / %2")
                        .arg(completed).arg(total));
                }, cancelLoad);
            if (m_vegetationWashTimer) m_vegetationWashTimer->stop();
            m_viewport->reloadTerrain();
            m_viewport->setBasemapLoadCallbacks({}, {});
        }

        m_selectedIndex = m_engine.antennas().empty() ? -1 : 0;
        refreshTree();
        if (m_selectedIndex >= 0) m_scenarioTree->selectRow(m_selectedIndex);
        refreshPropsPanel();
        refreshBottomBar();
        updateLocationSummary();
        setWindowTitle(m_locationName.isEmpty()
            ? QString("Ridgeline — %1°, %2°[*]")
                .arg(latitude, 0, 'f', 4).arg(longitude, 0, 'f', 4)
            : QString("Ridgeline — %1[*]").arg(m_locationName.section(',', 0, 0)));

        refreshVegetationActions();
        const auto world = m_engine.terrain().worldSize();
        const int outside = static_cast<int>(std::count_if(
            m_engine.antennas().begin(), m_engine.antennas().end(),
            [world](const Antenna& a) {
                return a.position.x < 0.0f || a.position.z < 0.0f ||
                       a.position.x > world.x || a.position.z > world.z;
            }));
        // The elevation commit above marks the scenario dirty even if imagery
        // was cancelled or could not finish.
        if (loadProgress.wasCanceled()) {
            m_statusLabel->setText("  Elevation loaded · Satellite imagery cancelled");
        } else {
            const bool imageryReady = m_viewport && m_viewport->basemapReady();
            const QString imageryError = m_viewport
                ? m_viewport->basemapError() : QString{};
            QString status;
            if (!imageryReady) {
                status = "  Elevation loaded · Satellite imagery unavailable";
                if (!imageryError.isEmpty()) status += " — " + imageryError;
            } else if (!imageryError.isEmpty()) {
                status = "  Elevation and satellite imagery loaded with a warning — "
                       + imageryError;
            } else {
                status = "  Real elevation and satellite imagery aligned";
            }
            status += outside > 0
                ? QString(" · %1 source(s) remain outside this analysis area")
                    .arg(outside)
                : QStringLiteral(" · Sources preserved");
            // Drawn areas follow the ground, so a new footprint can leave some
            // outside it: kept and saved, but with no effect and nothing on the
            // map to show them.
            const int drawnOutside = VegetationAreas::countOutside(
                m_engine.terrain(), m_engine.vegetation());
            if (drawnOutside > 0)
                status += QString(" · %1 drawn vegetation area(s) lie outside this analysis area")
                              .arg(drawnOutside);
            m_statusLabel->setText(status);
        }
    } catch (const std::runtime_error& e) {
        loadProgress.hide();
        if (loadProgress.wasCanceled() || std::string(e.what()) == "Terrain load cancelled")
            m_statusLabel->setText(terrainInstalled
                ? "  Elevation loaded · Map loading cancelled"
                : "  Location loading cancelled · Previous terrain preserved");
        else {
            m_statusLabel->setText(terrainInstalled
                ? "  Elevation loaded · Map display failed"
                : "  Location load failed · Previous terrain preserved");
            QMessageBox::critical(this, "Could not load location", e.what());
        }
    } catch (const std::exception& e) {
        loadProgress.hide();
        m_statusLabel->setText(terrainInstalled
            ? "  Elevation loaded · Map display failed"
            : "  Location load failed · Previous terrain preserved");
        QMessageBox::critical(this, "Could not load location", e.what());
    }
    if (m_viewport) m_viewport->setBasemapLoadCallbacks({}, {});
    loadProgress.close();
}

void MainWindow::onChooseLocation() {
    if (!confirmDiscardChanges()) return;
    double latitude = m_terrainSource.centerLat;
    double longitude = m_terrainSource.centerLon;
    double extentKm = m_terrainSource.extentKm;
    if (m_engine.terrain().hasGeoReference() &&
        m_terrainSource.type != TerrainSource::Type::Global) {
        const auto center = m_engine.terrain().worldToLatLon(
            m_engine.terrain().worldSize().x * 0.5f,
            m_engine.terrain().worldSize().z * 0.5f);
        latitude = center.latDeg;
        longitude = center.lonDeg;
    }
    LocationDialog dialog(latitude, longitude, extentKm, this);
    if (dialog.exec() != QDialog::Accepted) return;
    loadGlobalLocation(dialog.latitude(), dialog.longitude(),
                       dialog.extentKm(), dialog.locationName());
}

void MainWindow::onReceiverSettings() {
    ReceiverDialog dialog(m_engine.receiver, this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_engine.receiver = dialog.settings();
    markScenarioDirty();
    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    invalidateScenarioComparisonCurrent();
    invalidateCalibrationCurrent();
    if (m_pointPanel) m_pointPanel->clear();
    if (m_pointDock) m_pointDock->hide();
    if (m_viewport) {
        m_viewport->clearAnalysisPoint();
        m_viewport->refreshOverlays();
    }
    m_statusLabel->setText(QString(
        "  Receiver updated · height %1 m AGL · sensitivity %2 dBm · bandwidth %3 kHz · required SINR %4 dB · Run coverage to refresh")
        .arg(m_engine.receiver.receiverHeightAglM, 0, 'f', 1)
        .arg(m_engine.receiver.sensitivityDbm, 0, 'f', 0)
        .arg(m_engine.receiver.bandwidthHz / 1000.0f, 0, 'f', 1)
        .arg(m_engine.receiver.requiredSnrDb, 0, 'f', 1));
}

void MainWindow::updateLocationSummary() {
    if (!m_locationLabel) return;
    const auto& terrain = m_engine.terrain();
    const QString tooltip = m_locationName + "\n" + TerrainPresentation::gridDetails(terrain) + "\n" +
        TerrainPresentation::elevationReference(m_terrainSource.type) + "\n" +
        m_locationLabel->property("basemapInfo").toString();
    m_locationLabel->setToolTip("<qt>" + tooltip.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
    if (!terrain.hasGeoReference()) {
        const bool demo = m_terrainSource.type == TerrainSource::Type::Hills ||
                          m_terrainSource.type == TerrainSource::Type::Flat;
        m_locationLabel->setText(demo ? "Demo terrain · Choose a location to begin"
            : QString("Local terrain · %1").arg(m_locationName));
        return;
    }
    const auto world = terrain.worldSize();
    const auto center = terrain.worldToLatLon(world.x * 0.5f, world.z * 0.5f);
    const QString area = TerrainPresentation::areaSummary(world.x, world.z);
    if (!m_locationName.isEmpty()) {
        m_locationLabel->setText(QString("%1\n%2")
            .arg(m_locationName.section(',', 0, 0), area));
    } else {
        m_locationLabel->setText(QString("%1°, %2°\n%3")
            .arg(center.latDeg, 0, 'f', 4)
            .arg(center.lonDeg, 0, 'f', 4)
            .arg(area));
    }
}

void MainWindow::startPlacement(bool jammer) {
    m_placementActive = true;
    m_placingJammer = jammer;
    m_viewport->setPlacementMode(true, jammer);
    m_statusLabel->setText(jammer
        ? "  Click the terrain to place a jammer · Esc cancels"
        : "  Click the terrain to place an emitter · Esc cancels");
}

bool MainWindow::isScenarioDirty() const {
    return m_scenarioDirty || (m_undoStack && !m_undoStack->isClean());
}

void MainWindow::markScenarioDirty() {
    m_scenarioDirty = true;
    setWindowModified(true);
}

void MainWindow::markScenarioClean() {
    // The work now lives in a file the user chose, so a recovery copy would
    // only offer to restore something already kept.
    RecoveryStore::clear();
    m_scenarioDirty = false;
    if (m_undoStack) m_undoStack->setClean();
    setWindowModified(false);
}

bool MainWindow::confirmDiscardChanges() {
    if (m_terrainLoadInProgress || m_solveInProgress) return false;
    if (!isScenarioDirty()) return true;

    const auto answer = QMessageBox::warning(
        this, "Unsaved changes",
        "This scenario has unsaved changes.\n\nSave them before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) {
        onSaveScenario();
        // A cancelled or failed save leaves the scenario dirty; don't discard.
        return !isScenarioDirty();
    }
    return true;  // Discard
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_terrainLoadInProgress) {
        m_statusLabel->setText("  Cancel the terrain or canopy load before closing");
        event->ignore();
        return;
    }
    // A coverage run already owns the interaction through its own modal
    // progress dialog. Raising the unsaved-changes prompt on top of it stacks
    // a second modal — and a third if the user picks Save and gets a file
    // dialog — while onSolve() is still blocked in its nested event loop. To
    // anyone not looking closely the window just appears wedged. Make them
    // cancel the run first; the progress dialog has a Cancel button for it.
    if (m_solveInProgress) {
        m_statusLabel->setText("  Cancel the coverage run before closing");
        event->ignore();
        return;
    }
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    // The user has decided: saved, or discarded on purpose. Either way a
    // recovery copy would resurrect work already dealt with.
    RecoveryStore::clear();
    WorkspaceSettings::saveLayout(saveGeometry(), saveState());
    event->accept();
}

void MainWindow::loadTerrain(const QString& path) {
    if (m_terrainLoadInProgress || m_solveInProgress) return;
    m_terrainLoadInProgress = true;
    const auto loadingGuard = qScopeGuard([this] { m_terrainLoadInProgress = false; });
    bool terrainInstalled = false;
    try {
        m_statusLabel->setText("  Loading terrain…");
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const QString ext = QFileInfo(path).suffix().toLower();
        TerrainModel t = (ext == "hgt")
                       ? TerrainLoader::fromHGT(path.toStdString())
                       : ((ext == "tif" || ext == "tiff")
                          ? TerrainLoader::fromGeoTIFF(path.toStdString())
                          : TerrainLoader::fromDTED(path.toStdString()));
        if (m_viewport) m_viewport->setVegetationDrawMode(false);
        m_engine.setTerrain(std::move(t));
        terrainInstalled = true;
        markScenarioDirty();
        m_engine.setFieldGrid({});
        m_solvedSignature.clear();
    m_solvedSignature.clear();
        resetScenarioComparisonForTerrain();
        if (m_undoStack) m_undoStack->clear();
        if (m_pointPanel) m_pointPanel->clear();
        if (m_pointDock) m_pointDock->hide();
        if (ext == "hgt") {
            m_terrainSource.type = TerrainSource::Type::Hgt;
            m_terrainSource.hgtPath = path.toStdString();
        } else if (ext == "tif" || ext == "tiff") {
            m_terrainSource.type = TerrainSource::Type::GeoTiff;
            m_terrainSource.hgtPath = path.toStdString();
        } else {
            m_terrainSource.type = TerrainSource::Type::Dted;
            m_terrainSource.hgtPath = path.toStdString();
        }
        const auto& loadedTerrain = m_engine.terrain();
        if (loadedTerrain.hasGeoReference()) {
            const auto center = loadedTerrain.worldToLatLon(
                loadedTerrain.worldSize().x * 0.5f,
                loadedTerrain.worldSize().z * 0.5f);
            m_terrainSource.centerLat = center.latDeg;
            m_terrainSource.centerLon = center.lonDeg;
            m_terrainSource.extentKm =
                std::max(loadedTerrain.worldSize().x, loadedTerrain.worldSize().z) / 1000.0;
            m_locationName = QFileInfo(path).completeBaseName();
            updateLocationSummary();
        }
        placeDefaultAntennas();
        if (m_vegetationWashTimer) m_vegetationWashTimer->stop();
        if (m_viewport) m_viewport->reloadTerrain();
        m_selectedIndex = m_engine.antennas().empty() ? -1 : 0;
        refreshTree();
        if (m_selectedIndex >= 0) m_scenarioTree->selectRow(m_selectedIndex);
        refreshPropsPanel();
        refreshBottomBar();
        markScenarioDirty();
        setWindowTitle(QString("Ridgeline — %1[*]").arg(QFileInfo(path).fileName()));
        refreshVegetationActions();
        QString terrainStatus =
            QStringLiteral("  Terrain loaded · Existing georeferenced sources preserved");
        const int drawnOutside =
            VegetationAreas::countOutside(m_engine.terrain(), m_engine.vegetation());
        if (drawnOutside > 0)
            terrainStatus += QString(" · %1 drawn vegetation area(s) lie outside this terrain")
                                 .arg(drawnOutside);
        m_statusLabel->setText(terrainStatus);
    } catch (const std::exception& e) {
        m_statusLabel->setText(terrainInstalled
            ? "  Terrain loaded · Map display failed"
            : "  Terrain load failed · Previous terrain preserved");
        QMessageBox::critical(this, "Load failed", e.what());
    }
}

void MainWindow::onSolve() {
    if (m_terrainLoadInProgress || m_solveInProgress) return;
    if (m_engine.antennas().empty()) {
        QMessageBox::information(this, "Place an emitter",
            "Place at least one emitter before running coverage.");
        startPlacement(false);
        return;
    }
    const bool hasEmitter = std::any_of(
        m_engine.antennas().begin(), m_engine.antennas().end(),
        [](const Antenna& antenna) { return !antenna.isJammer; });
    if (!hasEmitter) {
        QMessageBox::information(this, "Place an emitter",
            "The scenario only contains jammers. Place an emitter to calculate coverage.");
        startPlacement(false);
        return;
    }

    const auto world = m_engine.terrain().worldSize();
    const bool sourceOutside = std::any_of(
        m_engine.antennas().begin(), m_engine.antennas().end(),
        [world](const Antenna& a) {
            return a.position.x < 0.0f || a.position.z < 0.0f ||
                   a.position.x > world.x || a.position.z > world.z;
        });
    if (sourceOutside) {
        QMessageBox::warning(this, "Source outside analysis area",
            "One or more preserved sources are outside the current terrain. "
            "Choose an analysis area containing them, reposition them, or remove them before solving.");
        m_statusLabel->setText("  Coverage not run · Source outside analysis area");
        return;
    }

    m_statusLabel->setText("  Calculating terrain-aware coverage…");
    m_solveInProgress = true;
    const auto solveGuard = qScopeGuard([this] { m_solveInProgress = false; });
    m_solveBtn->setEnabled(false);
    m_solveBtn->setText("Calculating…");
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto cancelled = std::make_shared<std::atomic_bool>(false);

    QProgressDialog progress(this);
    progress.setWindowTitle("Calculating coverage");
    progress.setLabelText("Preparing terrain paths…");
    progress.setRange(0, 0);
    progress.setCancelButtonText("Cancel");
    progress.setWindowModality(Qt::WindowModal);
    progress.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumWidth(460);
    progress.setStyleSheet(
        "QProgressDialog { background: #0b1422; color: #dbeafe; }"
        "QLabel { color: #cbd5e1; font-size: 12px; padding: 10px 4px; }"
        "QProgressBar { background: #111c2e; border: 1px solid #2a3b52;"
        " border-radius: 7px; color: #e2e8f0; min-height: 18px; text-align: center; }"
        "QProgressBar::chunk { background: #14b8a6; border-radius: 6px; }"
    );
    connect(&progress, &QProgressDialog::canceled, this,
            [cancelled]() { cancelled->store(true, std::memory_order_relaxed); });

    QPointer<QProgressDialog> progressGuard(&progress);
    const QString quality = m_modeCombo->currentText();
    auto reportProgress = [progressGuard, quality, timer](int completed, int total) {
        QMetaObject::invokeMethod(qApp, [progressGuard, quality, completed, total, timer]() {
            if (!progressGuard) return;
            if (progressGuard->maximum() != total) progressGuard->setRange(0, total);
            progressGuard->setValue(completed);
            const int percent = total > 0 ? completed * 100 / total : 0;
            progressGuard->setLabelText(
                QString("%1 analysis · %2% complete · %3 s elapsed")
                    .arg(quality).arg(percent).arg(timer->elapsed() / 1000.0, 0, 'f', 1));
        }, Qt::QueuedConnection);
    };

    auto solveEngine = std::make_shared<SimEngine>(m_engine);
    solveEngine->onFieldUpdated = {};
    FieldGrid solvedField;
    std::exception_ptr solveError;
    QFutureWatcher<void> watcher;
    QEventLoop waitLoop;
    connect(&watcher, &QFutureWatcher<void>::finished, &waitLoop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(
        [solveEngine, reportProgress, cancelled, &solvedField, &solveError]() {
            try {
                solveEngine->solve(
                    reportProgress,
                    [cancelled]() { return cancelled->load(std::memory_order_relaxed); });
                solvedField = solveEngine->fieldGrid();
            } catch (...) {
                solveError = std::current_exception();
            }
        }));
    progress.show();
    waitLoop.exec();
    progress.close();

    try {
        if (solveError) std::rethrow_exception(solveError);
        m_engine.setFieldGrid(std::move(solvedField));
        // Remember exactly what this field was computed from, so any later
        // change to those inputs is detectable.
        m_solvedSignature = SolveSignature::of(
            m_engine.antennas(), m_engine.receiver, m_engine.mode,
            m_engine.calibration, m_engine.terrain());
        if (m_pointPanel) m_pointPanel->clear();
        if (m_pointDock) m_pointDock->hide();
        if (m_viewport) m_viewport->clearAnalysisPoint();
        m_viewport->refreshOverlays();
        updateScenarioComparisonAfterSolve();
        updateCalibrationAfterSolve();
        const auto& grid = m_engine.fieldGrid();
        float minDbm = std::numeric_limits<float>::infinity();
        float maxDbm = -std::numeric_limits<float>::infinity();
        for (const float value : grid.dbm) {
            if (!std::isfinite(value)) continue;
            minDbm = std::min(minDbm, value);
            maxDbm = std::max(maxDbm, value);
        }
        const QString signalRange = std::isfinite(minDbm) && std::isfinite(maxDbm)
            ? QString(" · %1 to %2 dBm").arg(minDbm, 0, 'f', 0).arg(maxDbm, 0, 'f', 0)
            : QString();
        const std::size_t served = static_cast<std::size_t>(std::count(
            grid.service.begin(), grid.service.end(), static_cast<std::uint8_t>(1)));
        const double servicePercent = grid.service.empty()
            ? 0.0 : 100.0 * static_cast<double>(served) / grid.service.size();
        m_statusLabel->setText(QString(
            "  Coverage ready in %1 s · %2 × %3 cells · %4% service%5 · Click the map to inspect")
            .arg(timer->elapsed() / 1000.0, 0, 'f', 1)
            .arg(grid.width).arg(grid.height)
            .arg(servicePercent, 0, 'f', 1).arg(signalRange));
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()) == "Coverage solve cancelled") {
            m_statusLabel->setText("  Coverage calculation cancelled");
        } else {
            QMessageBox::critical(this, "Coverage failed", e.what());
            m_statusLabel->setText("  Coverage calculation failed");
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Coverage failed", e.what());
        m_statusLabel->setText("  Coverage calculation failed");
    } catch (...) {
        QMessageBox::critical(this, "Coverage failed", "The solver stopped unexpectedly.");
        m_statusLabel->setText("  Coverage calculation failed");
    }
    m_solveBtn->setEnabled(true);
    m_solveBtn->setText("Run coverage");
}

void MainWindow::onLoadTerrain() {
    if (!confirmDiscardChanges()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Terrain", {},
        "Terrain files (*.dt0 *.dt1 *.dt2 *.hgt *.tif *.tiff);;"
        "GeoTIFF DEM files (*.tif *.tiff);;"
        "DTED files (*.dt0 *.dt1 *.dt2);;"
        "SRTM HGT files (*.hgt);;"
        "All files (*)");
    if (!path.isEmpty()) loadTerrain(path);
}

void MainWindow::onAddAntenna() { startPlacement(false); }
void MainWindow::onAddJammer() { startPlacement(true); }

void MainWindow::onRemoveAntenna(int index) {
    if (index < 0 || index >= static_cast<int>(m_engine.antennas().size())) return;
    std::vector<Antenna> after = m_engine.antennas();
    after.erase(after.begin() + index);
    int selectedAfter = m_selectedIndex;
    if (selectedAfter > index) --selectedAfter;
    else if (selectedAfter == index)
        selectedAfter = std::min(index, static_cast<int>(after.size()) - 1);
    pushSourceChange(std::move(after), selectedAfter, "Remove source");
    m_statusLabel->setText("  Antenna removed");
}

// A copy with every setting of the original, set down beside it rather than
// on top of it so both stay visible and draggable. It keeps its height above
// ground, and lands east, then west, north or south: whichever stays on the
// terrain.
void MainWindow::onDuplicateAntenna() {
    const int index = m_selectedIndex;
    if (index < 0 || index >= static_cast<int>(m_engine.antennas().size())) {
        m_statusLabel->setText("  Select a source to duplicate");
        return;
    }
    const TerrainModel& terrain = m_engine.terrain();
    const glm::vec3 size = terrain.worldSize();
    const float offsetM = std::clamp(0.02f * std::min(size.x, size.z), 25.0f, 250.0f);
    Antenna copy = m_engine.antennas()[index];
    const float ground = terrain.heightAtWorld(copy.position.x, copy.position.z);
    const float heightAgl = std::max(0.0f, copy.position.y - ground);
    struct Step { float dx, dz; const char* direction; };
    const Step steps[] = {{offsetM, 0.0f, "east"}, {-offsetM, 0.0f, "west"},
                          {0.0f, offsetM, "north"}, {0.0f, -offsetM, "south"}};
    const char* direction = nullptr;
    for (const Step& step : steps) {
        const float x = copy.position.x + step.dx;
        const float z = copy.position.z + step.dz;
        if (x < 0.0f || z < 0.0f || x > size.x || z > size.z) continue;
        copy.position.x = x;
        copy.position.z = z;
        direction = step.direction;
        break;
    }
    copy.position.y = terrain.heightAtWorld(copy.position.x, copy.position.z) + heightAgl;
    if (!terrain.hasGeoReference()) copy.hasGeoPosition = false;
    syncAntennaGeo(copy);

    std::vector<Antenna> after = m_engine.antennas();
    after.insert(after.begin() + index + 1, std::move(copy));
    pushSourceChange(std::move(after), index + 1, "Duplicate source");
    m_statusLabel->setText(direction
        ? QString("  Source duplicated %1 m %2 · Undo is available from Edit or Ctrl+Z")
              .arg(offsetM, 0, 'f', 0).arg(direction)
        : QStringLiteral("  Source duplicated in place · Undo is available from Edit or Ctrl+Z"));
}

void MainWindow::refreshPresetMenus() {
    if (!m_applyPresetMenu || !m_deletePresetMenu) return;
    m_applyPresetMenu->clear();
    m_deletePresetMenu->clear();
    const auto presets = WorkspaceSettings::equipmentPresets();
    for (const auto& preset : presets) {
        const QString name = preset.name;
        // Names are the user's text: keep an ampersand from becoming a mnemonic.
        const QString label = QString(name).replace('&', "&&");
        QAction* apply = m_applyPresetMenu->addAction(label);
        apply->setData(name);
        connect(apply, &QAction::triggered, this, [this, name] { applyEquipmentPreset(name); });
        QAction* remove = m_deletePresetMenu->addAction(label);
        remove->setData(name);
        connect(remove, &QAction::triggered, this, [this, name] {
            if (QMessageBox::question(this, "Delete Preset",
                    QString("Delete the preset \"%1\"? Sources already using it keep "
                            "their settings.").arg(name)) != QMessageBox::Yes)
                return;
            WorkspaceSettings::removeEquipmentPreset(name);
            refreshPresetMenus();
            m_statusLabel->setText(QString("  Preset \"%1\" deleted").arg(name));
        });
    }
    m_applyPresetMenu->setEnabled(!presets.empty());
    m_deletePresetMenu->setEnabled(!presets.empty());
}

void MainWindow::onSaveEquipmentPreset() {
    if (m_selectedIndex < 0 ||
        m_selectedIndex >= static_cast<int>(m_engine.antennas().size())) {
        m_statusLabel->setText("  Select a source to save its equipment as a preset");
        return;
    }
    const Antenna& source = m_engine.antennas()[m_selectedIndex];
    const QString suggested = QString("%1 MHz %2 %3 W")
        .arg(source.frequencyMHz, 0, 'g', 6)
        .arg(QString::fromStdString(source.antennaType))
        .arg(source.powerW, 0, 'g', 4);
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, "Save Equipment Preset",
        "Name for this radio and antenna. Position, height and role are not saved.",
        QLineEdit::Normal, suggested, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    saveSelectedAsPreset(name);
}

void MainWindow::saveSelectedAsPreset(const QString& name) {
    if (m_selectedIndex < 0 ||
        m_selectedIndex >= static_cast<int>(m_engine.antennas().size()) ||
        name.trimmed().isEmpty())
        return;
    const auto existing = WorkspaceSettings::equipmentPresets();
    const bool replacing = std::any_of(existing.begin(), existing.end(),
        [&](const auto& preset) { return preset.name == name.trimmed(); });
    WorkspaceSettings::saveEquipmentPreset(name, m_engine.antennas()[m_selectedIndex]);
    refreshPresetMenus();
    m_statusLabel->setText(QString(replacing ? "  Preset \"%1\" replaced"
                                             : "  Preset \"%1\" saved")
                               .arg(name.trimmed()));
}

void MainWindow::applyEquipmentPreset(const QString& name) {
    if (m_selectedIndex < 0 ||
        m_selectedIndex >= static_cast<int>(m_engine.antennas().size())) {
        m_statusLabel->setText("  Select a source to apply a preset to");
        return;
    }
    const auto presets = WorkspaceSettings::equipmentPresets();
    const auto found = std::find_if(presets.begin(), presets.end(),
        [&](const auto& preset) { return preset.name == name; });
    if (found == presets.end()) {
        refreshPresetMenus();
        m_statusLabel->setText(QString("  Preset \"%1\" no longer exists").arg(name));
        return;
    }
    std::vector<Antenna> after = m_engine.antennas();
    after[m_selectedIndex] = AntennaJson::withEquipment(after[m_selectedIndex], found->equipment);
    pushSourceChange(std::move(after), m_selectedIndex, "Apply preset");
    m_statusLabel->setText(QString("  Preset \"%1\" applied · position and height kept")
                               .arg(name));
}

void MainWindow::onAntennaSelected(int index) {
    m_selectedIndex = index;
    refreshPropsPanel();
    refreshBottomBar();
}

void MainWindow::onAntennaChanged(Antenna updated) {
    if (m_selectedIndex < 0 ||
        m_selectedIndex >= static_cast<int>(m_engine.antennas().size())) return;
    const float ground = m_engine.terrain().heightAtWorld(
        updated.position.x, updated.position.z);
    updated.position.y = ground + std::max(0.0f, updated.position.y);
    syncAntennaGeo(updated);
    std::vector<Antenna> after = m_engine.antennas();
    after[m_selectedIndex] = std::move(updated);
    pushSourceChange(std::move(after), m_selectedIndex, "Edit source",
                     1000 + m_selectedIndex);
}

void MainWindow::onEditAntennaPosition() {
    if (m_selectedIndex < 0 ||
        m_selectedIndex >= static_cast<int>(m_engine.antennas().size())) return;
    const Antenna current = m_engine.antennas()[m_selectedIndex];
    SourcePositionDialog dialog(m_engine.terrain(), current, this);
    if (dialog.exec() != QDialog::Accepted) return;
    onAntennaMoved(m_selectedIndex, dialog.worldX(), dialog.worldZ());
}

void MainWindow::onAntennaMoved(int index, float worldX, float worldZ) {
    if (index < 0 || index >= static_cast<int>(m_engine.antennas().size())) return;
    const auto& terrain = m_engine.terrain();
    const glm::vec3 size = terrain.worldSize();
    worldX = std::clamp(worldX, 0.0f, size.x);
    worldZ = std::clamp(worldZ, 0.0f, size.z);

    std::vector<Antenna> after = m_engine.antennas();
    Antenna& moved = after[index];
    const float oldGround = terrain.heightAtWorld(moved.position.x, moved.position.z);
    const float heightAgl = std::max(0.0f, moved.position.y - oldGround);
    moved.position.x = worldX;
    moved.position.z = worldZ;
    moved.position.y = terrain.heightAtWorld(worldX, worldZ) + heightAgl;
    if (!terrain.hasGeoReference()) moved.hasGeoPosition = false;
    syncAntennaGeo(moved);
    pushSourceChange(std::move(after), index, "Move source");
    m_statusLabel->setText("  Source moved · Undo is available from Edit or Ctrl+Z");
}

void MainWindow::refreshTree() {
    updateLocationSummary();
    m_scenarioTree->refresh(m_engine.antennas());
    m_viewport->setElevationCredit(TerrainPresentation::elevationCredit(
        m_terrainSource.type, m_engine.terrain().elevationProvider));
}

void MainWindow::refreshPropsPanel() {
    if (m_selectedIndex >= 0 &&
        m_selectedIndex < static_cast<int>(m_engine.antennas().size())) {
        const Antenna& source = m_engine.antennas()[m_selectedIndex];
        m_propsAntennaView = source;
        const float ground = m_engine.terrain().heightAtWorld(
            m_propsAntennaView.position.x, m_propsAntennaView.position.z);
        m_propsAntennaView.position.y =
            std::max(0.0f, m_propsAntennaView.position.y - ground);
        m_propsPanel->load(&m_propsAntennaView);
        m_propsPanel->setPositionSummary(sourcePositionSummary(source));
    } else {
        m_propsPanel->load(nullptr);
    }
}

void MainWindow::applySourceState(const std::vector<Antenna>& antennas,
                                  int selectedIndex) {
    m_engine.setAntennas(antennas);
    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    invalidateScenarioComparisonCurrent();
    invalidateCalibrationCurrent();
    if (m_pointPanel) m_pointPanel->clear();
    if (m_pointDock) m_pointDock->hide();
    if (m_viewport) m_viewport->clearAnalysisPoint();
    const int count = static_cast<int>(m_engine.antennas().size());
    m_selectedIndex = count == 0 ? -1 : std::clamp(selectedIndex, -1, count - 1);
    refreshTree();
    if (m_scenarioTree) m_scenarioTree->selectRow(m_selectedIndex);
    refreshPropsPanel();
    refreshBottomBar();
    if (m_viewport) m_viewport->refreshOverlays();
}

void MainWindow::pushSourceChange(std::vector<Antenna> after,
                                  int selectedAfter,
                                  const QString& description,
                                  int mergeId) {
    if (!m_undoStack) {
        applySourceState(after, selectedAfter);
        return;
    }
    m_undoStack->push(new SourceStateCommand(
        m_engine.antennas(), m_selectedIndex, std::move(after), selectedAfter,
        description, mergeId,
        [this](const std::vector<Antenna>& state, int selected) {
            applySourceState(state, selected);
        }));
}

void MainWindow::syncAntennaGeo(Antenna& antenna) const {
    const TerrainModel& terrain = m_engine.terrain();
    if (!terrain.hasGeoReference()) return;
    const auto coordinate = terrain.worldToLatLon(
        antenna.position.x, antenna.position.z);
    antenna.hasGeoPosition = true;
    antenna.latitudeDeg = coordinate.latDeg;
    antenna.longitudeDeg = coordinate.lonDeg;
    antenna.heightAglM = std::max(
        0.0f, antenna.position.y -
              terrain.heightAtWorld(antenna.position.x, antenna.position.z));
}

QString MainWindow::sourcePositionSummary(const Antenna& antenna) const {
    const TerrainModel& terrain = m_engine.terrain();
    const float ground = terrain.heightAtWorld(
        antenna.position.x, antenna.position.z);
    const float agl = std::max(0.0f, antenna.position.y - ground);
    QString summary = QString("X %1 m · Z %2 m\nGround %3 m · source %4 m\n%5 m AGL\n%6")
        .arg(antenna.position.x, 0, 'f', 1)
        .arg(antenna.position.z, 0, 'f', 1)
        .arg(ground, 0, 'f', 1)
        .arg(antenna.position.y, 0, 'f', 1)
        .arg(agl, 0, 'f', 1)
        .arg(TerrainPresentation::elevationReference(m_terrainSource.type));
    if (!terrain.hasGeoReference()) return summary;

    const auto coordinate = terrain.worldToLatLon(
        antenna.position.x, antenna.position.z);
    std::string mgrs;
    std::string error;
    const QString mgrsLine = wgs84ToMgrs(
        coordinate.latDeg, coordinate.lonDeg, 5, mgrs, error)
        ? QString("\nMGRS %1").arg(QString::fromStdString(mgrs))
        : QString{};
    return QString("%1°, %2°%3\n%4")
        .arg(coordinate.latDeg, 0, 'f', 6)
        .arg(coordinate.lonDeg, 0, 'f', 6)
        .arg(mgrsLine)
        .arg(summary);
}

void MainWindow::refreshBottomBar() {
    const auto& antennas = m_engine.antennas();
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(antennas.size()))
        m_bottomBar->setAntenna(antennas[m_selectedIndex]);
    else
        m_bottomBar->clearAntenna();
}

void MainWindow::onImportMeasurements() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Import RF Measurements", {},
        "RF measurement CSV (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    const MeasurementLoadResult loaded =
        MeasurementIO::loadCsv(path.toStdString());
    auto diagnosticText = [](const MeasurementDiagnostic& diagnostic) {
        return diagnostic.line > 0
            ? QString("CSV line %1: %2")
                .arg(diagnostic.line)
                .arg(QString::fromStdString(diagnostic.message))
            : QString::fromStdString(diagnostic.message);
    };

    if (loaded.measurements.empty()) {
        QStringList details;
        for (const MeasurementDiagnostic& error : loaded.errors) {
            details.push_back(diagnosticText(error));
            if (details.size() == 8) break;
        }
        for (const MeasurementDiagnostic& warning : loaded.warnings) {
            if (details.size() == 8) break;
            details.push_back(diagnosticText(warning));
        }
        if (details.isEmpty()) details.push_back("No measurement rows were found.");
        QMessageBox::warning(
            this, "No usable measurements",
            QString(
                "%1\n\nExpected columns: measured_dbm plus either latitude/longitude or x_m/z_m. Optional columns are frequency_mhz, height_agl_m, and label.")
                .arg(details.join('\n')));
        return;
    }

    m_measurements = loaded.measurements;
    m_measurementPath = path;
    m_measurementRowsRead = loaded.rowsRead;
    m_measurementRejectedRows = loaded.rejectedRows;
    m_measurementImportWarnings.clear();

    constexpr int maxDiagnostics = 8;
    int diagnosticCount = 0;
    for (const MeasurementDiagnostic& error : loaded.errors) {
        if (diagnosticCount++ >= maxDiagnostics) break;
        m_measurementImportWarnings.push_back(diagnosticText(error));
    }
    for (const MeasurementDiagnostic& warning : loaded.warnings) {
        if (diagnosticCount++ >= maxDiagnostics) break;
        m_measurementImportWarnings.push_back(diagnosticText(warning));
    }
    const int totalDiagnostics = static_cast<int>(
        loaded.errors.size() + loaded.warnings.size());
    if (totalDiagnostics > maxDiagnostics) {
        m_measurementImportWarnings.push_back(QString(
            "%1 additional CSV diagnostic(s) were omitted from this summary.")
            .arg(totalDiagnostics - maxDiagnostics));
    }

    m_calibrationReportCurrent = false;
    m_calibrationReport = {};
    if (m_engine.fieldGrid().valid())
        updateCalibrationAfterSolve();
    else
        m_calibrationPanel->showAwaitingCoverage(calibrationAwaitingReport());
    showCalibrationPanel();
    m_statusLabel->setText(QString(
        "  %1 usable measurement(s) imported from %2%3")
        .arg(m_measurements.size())
        .arg(QFileInfo(path).fileName())
        .arg(loaded.rejectedRows > 0
            ? QString(" · %1 row(s) rejected").arg(loaded.rejectedRows)
            : QString{}));
}

bool MainWindow::resultsAreStale() const {
    if (!m_engine.fieldGrid().valid() || m_solvedSignature.empty()) return false;
    return SolveSignature::of(m_engine.antennas(), m_engine.receiver, m_engine.mode,
                              m_engine.calibration, m_engine.terrain()) != m_solvedSignature;
}

void MainWindow::refreshRecentMenus() {
    if (m_recentScenariosMenu) {
        m_recentScenariosMenu->clear();
        const QStringList paths = WorkspaceSettings::recentScenarios();
        m_recentScenariosMenu->setEnabled(!paths.isEmpty());
        for (const QString& path : paths) {
            // Full path as the tooltip: two scenarios often share a filename.
            QAction* entry = m_recentScenariosMenu->addAction(QFileInfo(path).fileName());
            entry->setToolTip(path);
            connect(entry, &QAction::triggered, this, [this, path] { openScenarioPath(path); });
        }
    }
    if (m_recentLocationsMenu) {
        m_recentLocationsMenu->clear();
        const auto places = WorkspaceSettings::recentLocations();
        m_recentLocationsMenu->setEnabled(!places.empty());
        for (const auto& place : places) {
            const QString label = place.name.isEmpty()
                ? QStringLiteral("%1°, %2°").arg(place.latDeg, 0, 'f', 4).arg(place.lonDeg, 0, 'f', 4)
                : place.name;
            QAction* entry = m_recentLocationsMenu->addAction(
                QStringLiteral("%1  ·  %2 km").arg(label).arg(place.extentKm, 0, 'f', 0));
            connect(entry, &QAction::triggered, this, [this, place] {
                if (!confirmDiscardChanges()) return;
                loadGlobalLocation(place.latDeg, place.lonDeg, place.extentKm, place.name);
            });
        }
    }
}

ScenarioDoc MainWindow::currentScenarioDoc() const {
    ScenarioDoc doc;
    doc.terrain = m_terrainSource;
    doc.mode = m_engine.mode;
    doc.receiver = m_engine.receiver;
    doc.calibration = m_engine.calibration;
    doc.calibrationSourceName = m_appliedCalibrationSourceName.toStdString();
    doc.calibrationPairedSamples = m_appliedCalibrationPairedSamples;
    doc.calibrationRmseBeforeDb = m_appliedCalibrationRmseBeforeDb;
    doc.calibrationRmseAfterDb = m_appliedCalibrationRmseAfterDb;
    doc.antennas = m_engine.antennas();
    doc.layers.clear();
    for (const LayerStack::State& layer : m_layerStack.state())
        doc.layers.push_back({layer.id.toStdString(), layer.visible, layer.opacity});
    doc.vegetation = m_engine.vegetation();
    if (m_canopy && !m_canopy->provenance.empty()) {
        doc.canopyImport.enabled = true;
        doc.canopyImport.sourceId = m_canopy->provenance.sourceId;
        doc.canopyImport.retrievedUtc = m_canopy->provenance.retrievedUtc;
        doc.canopyImport.assumedDevelopedCanopyM =
            m_canopyOptions.assumedDevelopedCanopyM;
        switch (m_canopyOptions.developed) {
            case landfire::DevelopedPolicy::AssumeHeight:
                doc.canopyImport.developedPolicy = "assume"; break;
            case landfire::DevelopedPolicy::UseSourceValue:
                doc.canopyImport.developedPolicy = "source"; break;
            case landfire::DevelopedPolicy::TreatAsBare:
                doc.canopyImport.developedPolicy = "bare"; break;
        }
    }
    return doc;
}

void MainWindow::autosaveTick() {
    // Nothing to protect, or the workspace is mid-operation and its state is
    // not coherent enough to copy.
    if (!isScenarioDirty() || m_solveInProgress || m_terrainLoadInProgress) return;
    RecoveryStore::write(currentScenarioDoc());
}

void MainWindow::offerRecovery() {
    if (!RecoveryStore::exists()) return;
    const QDateTime savedAt = RecoveryStore::savedAt();
    const QString when = savedAt.isValid()
        ? savedAt.toString("d MMMM, h:mm ap") : QStringLiteral("an earlier session");

    QMessageBox ask(this);
    ask.setObjectName("RecoveryPrompt");
    ask.setIcon(QMessageBox::Question);
    ask.setWindowTitle("Recover unsaved work");
    ask.setText(QStringLiteral("Ridgeline closed with unsaved changes.\n\n"
                               "A copy from %1 can be restored.").arg(when));
    QPushButton* restore = ask.addButton("Restore", QMessageBox::AcceptRole);
    ask.addButton("Discard", QMessageBox::DestructiveRole);
    ask.setDefaultButton(restore);
    ask.exec();

    if (ask.clickedButton() != restore) {
        // Declining is a decision, so the copy goes rather than being offered
        // again on every start.
        RecoveryStore::clear();
        return;
    }

    QProgressDialog progress("Restoring unsaved work…", "Cancel", 0, 0, this);
    progress.setWindowTitle("Recovering");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    bool terrainInstalled = false;
    try {
        applyScenarioDoc(RecoveryStore::read(), progress, terrainInstalled);
        // Recovered work is unsaved by definition: it has no file behind it,
        // so it must stay dirty and keep prompting until the user saves it.
        markScenarioDirty();
        m_statusLabel->setText("  Unsaved work restored · save it to keep it");
    } catch (const std::exception& e) {
        progress.hide();
        QMessageBox::warning(this, "Could not recover",
                             QStringLiteral("The recovery copy could not be restored:\n%1")
                                 .arg(e.what()));
    }
    progress.close();
}

void MainWindow::onSaveScenario() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Scenario", {},
        "Ridgeline scenario (*.json);;All files (*)");
    if (path.isEmpty()) return;

    const ScenarioDoc doc = currentScenarioDoc();

    try {
        ScenarioIO::save(doc, path.toStdString());
        markScenarioClean();
        WorkspaceSettings::addRecentScenario(path);
        refreshRecentMenus();
        m_statusLabel->setText("  Scenario saved");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

void MainWindow::onOpenScenario() {
    if (!confirmDiscardChanges()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Scenario", {},
        "Ridgeline scenario (*.json);;All files (*)");
    if (path.isEmpty()) return;
    openScenarioPath(path);
}

// Shared by the Open dialog and the Recent Scenarios menu, so a scenario
// reached either way loads through exactly the same path.
void MainWindow::openScenarioPath(const QString& path) {
    m_terrainLoadInProgress = true;
    const auto loadingGuard = qScopeGuard([this] {
        m_terrainLoadInProgress = false;
        if (m_viewport) m_viewport->setBasemapLoadCallbacks({}, {});
    });
    QProgressDialog progress("Reading scenario…", "Cancel", 0, 0, this);
    progress.setWindowTitle("Opening scenario");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumWidth(460);
    progress.show();
    bool terrainInstalled = false;
    try {
        m_statusLabel->setText("  Loading scenario…");
        QApplication::processEvents();
        if (progress.wasCanceled()) throw std::runtime_error("Scenario load cancelled");
        ScenarioDoc doc = ScenarioIO::load(path.toStdString());
        applyScenarioDoc(doc, progress, terrainInstalled);
        markScenarioClean();
        setWindowTitle(QString("Ridgeline — %1[*]").arg(QFileInfo(path).fileName()));
        // Recorded only on success, so a failed open never enters history.
        WorkspaceSettings::addRecentScenario(path);
        refreshRecentMenus();
        if (progress.wasCanceled())
            m_statusLabel->setText("  Scenario loaded · Satellite imagery cancelled");
        else if (m_engine.terrain().hasGeoReference() && m_viewport && !m_viewport->basemapReady())
            m_statusLabel->setText("  Scenario loaded · Satellite imagery unavailable — "
                                  + m_viewport->basemapError());
        else if (m_viewport && !m_viewport->basemapError().isEmpty())
            m_statusLabel->setText("  Scenario loaded · Imagery warning — " + m_viewport->basemapError());
        else
            m_statusLabel->setText("  Scenario loaded");
    } catch (const std::exception& e) {
        progress.hide();
        if (progress.wasCanceled()) {
            m_statusLabel->setText(terrainInstalled
                ? "  Scenario terrain installed · Map loading cancelled"
                : "  Scenario loading cancelled · Previous scenario preserved");
        } else {
            m_statusLabel->setText(terrainInstalled
                ? "  Scenario partially loaded · Review before saving"
                : "  Scenario load failed · Previous scenario preserved");
            QMessageBox::critical(this, "Open failed", e.what());
        }
    }
}

void MainWindow::applyScenarioDoc(const ScenarioDoc& doc, QProgressDialog& progress,
                                  bool& terrainInstalled) {
    auto cancelLoad = [&progress] { return progress.wasCanceled(); };
    auto elevationProgress = [&progress](int completed, int total) {
        if (progress.wasCanceled()) return;
        progress.setLabelText(QString("Elevation tiles · %1 / %2").arg(completed).arg(total));
        progress.setRange(0, std::max(total, 1));
        progress.setValue(completed);
    };
    progress.setLabelText("Loading scenario elevation…");
    QApplication::processEvents();
    if (cancelLoad()) throw std::runtime_error("Scenario load cancelled");
    TerrainModel t;
    const auto& ts = doc.terrain;
    switch (ts.type) {
        case TerrainSource::Type::Hgt:
            t = TerrainLoader::fromHGT(ts.hgtPath,
                                        static_cast<float>(ts.hgtLat),
                                        static_cast<float>(ts.hgtLon), ts.hgtSize);
            break;
        case TerrainSource::Type::Dted:
            t = TerrainLoader::fromDTED(ts.hgtPath); break;
        case TerrainSource::Type::GeoTiff:
            t = TerrainLoader::fromGeoTIFF(ts.hgtPath); break;
        case TerrainSource::Type::Global:
            t = GlobalTerrainLoader::load(ts.centerLat, ts.centerLon, ts.extentKm, 513,
                                          elevationProgress, cancelLoad); break;
        case TerrainSource::Type::Flat:
            t = TerrainLoader::makeFlat(ts.widthCells, ts.heightCells, ts.cellSize); break;
        case TerrainSource::Type::Hills:
        default:
            t = TerrainLoader::makeHills(ts.widthCells, ts.heightCells,
                                          ts.cellSize, ts.amplitude); break;
    }
    // Local decoders are synchronous; honor pending cancellation at the
    // boundary before any existing scenario or results are replaced.
    QApplication::processEvents();
    if (cancelLoad()) throw std::runtime_error("Scenario load cancelled");
    m_engine.setTerrain(std::move(t));
    terrainInstalled = true;
    m_placementActive = false;
    if (m_viewport) m_viewport->setPlacementMode(false);
    if (m_viewport) m_viewport->setVegetationDrawMode(false);
    markScenarioDirty();
    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    resetScenarioComparisonForTerrain();
    clearCalibrationMeasurements();
    if (m_undoStack) m_undoStack->clear();
    if (m_pointPanel) m_pointPanel->clear();
    if (m_pointDock) m_pointDock->hide();
    m_terrainSource = doc.terrain;
    m_locationName.clear();
    updateLocationSummary();

    m_engine.mode = doc.mode;
    m_engine.receiver = doc.receiver;
    m_engine.calibration = doc.calibration;
    if (!std::isfinite(m_engine.calibration.desiredPowerOffsetDb))
        m_engine.calibration = {};
    m_appliedCalibrationSourceName =
        QString::fromStdString(doc.calibrationSourceName);
    m_appliedCalibrationPairedSamples = doc.calibrationPairedSamples;
    m_appliedCalibrationRmseBeforeDb = doc.calibrationRmseBeforeDb;
    m_appliedCalibrationRmseAfterDb = doc.calibrationRmseAfterDb;
    if (m_calibrationPanel) {
        m_calibrationPanel->showNoMeasurements();
        m_calibrationPanel->setAppliedCorrection(
            m_engine.calibration.enabled
                ? m_engine.calibration.desiredPowerOffsetDb : 0.0);
    }
    if (doc.mode == SimEngine::Mode::Preview) m_modeCombo->setCurrentIndex(0);
    else if (doc.mode == SimEngine::Mode::Engineering) m_modeCombo->setCurrentIndex(2);
    else m_modeCombo->setCurrentIndex(1);
    m_engine.setAntennas(doc.antennas);
    // After setTerrain, so the areas rasterise onto the grid just installed.
    m_engine.setVegetation(doc.vegetation);
    // An imported canopy from a previous session, if any. Cleared first so a
    // scenario without one never inherits the last scenario's vegetation.
    m_canopy.reset();
    m_engine.clearImportedCanopy();
    if (doc.canopyImport.enabled) {
        progress.setLabelText("Restoring canopy · Scenario settings loaded");
        restoreCanopyImport(doc.canopyImport);
    }
    refreshCanopyActions();
    if (!doc.layers.empty()) {
        std::vector<LayerStack::State> layers;
        layers.reserve(doc.layers.size());
        for (const ScenarioLayerState& layer : doc.layers)
            layers.push_back({QString::fromStdString(layer.id), layer.visible, layer.opacity});
        m_layerStack.applyState(layers);
    }

    m_selectedIndex = -1;
    m_selectedVegetationIndex = -1;
    if (m_viewport) {
        progress.setLabelText("Loading satellite imagery · Scenario settings loaded");
        progress.setRange(0, 0);
        m_viewport->setBasemapLoadCallbacks([&progress](int completed, int total) {
            if (progress.wasCanceled()) return;
            progress.setLabelText(QString("Satellite imagery · %1 / %2").arg(completed).arg(total));
            progress.setRange(0, std::max(total, 1));
            progress.setValue(completed);
        }, cancelLoad);
        if (m_vegetationWashTimer) m_vegetationWashTimer->stop();
        m_viewport->reloadTerrain();
        m_viewport->setBasemapLoadCallbacks({}, {});
    }
    refreshTree();
    refreshPropsPanel();
    refreshBottomBar();
    refreshVegetationActions();
}

void MainWindow::onTerrainClicked(float worldX, float worldZ) {
    if (!m_placementActive) return;
    const auto& t = m_engine.terrain();
    Antenna a;
    a.position = {worldX, t.heightAtWorld(worldX, worldZ) + 10.0f, worldZ};
    a.frequencyMHz = 150.0f;
    a.powerW = 10.0f;
    a.gainDbi = 3.0f;
    a.isJammer = m_placingJammer;
    syncAntennaGeo(a);
    std::vector<Antenna> after = m_engine.antennas();
    after.push_back(a);
    const int selectedAfter = static_cast<int>(after.size()) - 1;
    pushSourceChange(std::move(after), selectedAfter,
                     a.isJammer ? "Add jammer" : "Add emitter");
    m_placementActive = false;
    m_viewport->setPlacementMode(false);
    const auto ll = t.worldToLatLon(worldX, worldZ);
    const QString role = a.isJammer ? "Jammer" : "Emitter";
    m_statusLabel->setText(t.hasGeoReference()
        ? QString("  %1 placed at %2°, %3°")
            .arg(role).arg(ll.latDeg, 0, 'f', 6).arg(ll.lonDeg, 0, 'f', 6)
        : QString("  %1 placed").arg(role));
}

void MainWindow::onTerrainInspected(float worldX, float worldZ) {
    if (!m_engine.fieldGrid().valid()) {
        m_statusLabel->setText("  Run coverage first, then click the map to inspect a result");
        return;
    }
    const PointAnalysisResult analysis = analyzeFieldPoint(
        m_engine.fieldGrid(), m_engine.terrain(), m_engine.antennas(),
        m_engine.receiver, worldX, worldZ);
    if (!analysis.valid) return;

    auto sourceName = [this](int sourceIndex) -> QString {
        const auto& antennas = m_engine.antennas();
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(antennas.size()))
            return {};
        const bool jammer = antennas[sourceIndex].isJammer;
        int ordinal = 0;
        for (int i = 0; i <= sourceIndex; ++i)
            if (antennas[i].isJammer == jammer) ++ordinal;
        return QString("%1 %2 · %3 MHz")
            .arg(jammer ? "Jammer" : "Emitter")
            .arg(ordinal)
            .arg(antennas[sourceIndex].frequencyMHz, 0, 'f', 1);
    };

    const TerrainModel& terrain = m_engine.terrain();
    const float sampledX = analysis.receiverPosition.x;
    const float sampledZ = analysis.receiverPosition.z;
    const QString cellPosition = QString("Result cell center\nX %1 m · Z %2 m\nReceiver elevation %3 m\n%4")
        .arg(sampledX, 0, 'f', 1)
        .arg(sampledZ, 0, 'f', 1)
        .arg(analysis.receiverPosition.y, 0, 'f', 1)
        .arg(TerrainPresentation::elevationReference(m_terrainSource.type));
    QString position = cellPosition;
    if (terrain.hasGeoReference()) {
        const auto coordinate = terrain.worldToLatLon(sampledX, sampledZ);
        std::string mgrs;
        std::string error;
        position = QString("%1°, %2°")
            .arg(coordinate.latDeg, 0, 'f', 6)
            .arg(coordinate.lonDeg, 0, 'f', 6);
        if (wgs84ToMgrs(coordinate.latDeg, coordinate.lonDeg, 5, mgrs, error))
            position += QString("\nMGRS %1").arg(QString::fromStdString(mgrs));
        position += "\n" + cellPosition;
    }

    m_pointPanel->setAnalysis(
        analysis, position, sourceName(analysis.servingSourceIndex),
        sourceName(analysis.strongestJammerIndex));
    m_pointDock->show();
    m_pointDock->raise();
    m_viewport->setAnalysisPoint(
        analysis.servingSourceIndex, analysis.receiverPosition);
    m_statusLabel->setText(analysis.inService
        ? "  Inspected point is in service · details and path shown at right"
        : "  Inspected point is out of service · causes shown at right");
}

void MainWindow::onExportPng() {
    if (!m_engine.fieldGrid().valid()) {
        QMessageBox::information(this, "No data", "Run Solve first to generate a coverage field.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Coverage PNG", {}, "PNG image (*.png);;All files (*)");
    if (path.isEmpty()) return;
    // QWidget::grab includes the OpenGL viewport and its child widgets,
    // preserving the selected overlays, legend, and provider attribution.
    // A pending vegetation rebuild would otherwise leave the exported map
    // showing the canopy as it was before the last edit.
    if (m_vegetationWashTimer && m_vegetationWashTimer->isActive()) flushVegetationWash();
    const QPixmap map = m_viewport->grab();
    if (Exporter::savePng(map.toImage(), path.toStdString()))
        m_statusLabel->setText("  Map PNG exported with current view and overlays");
    else QMessageBox::critical(this, "Export failed", "Could not write PNG file.");
}

void MainWindow::onExportCsv() {
    if (!m_engine.fieldGrid().valid()) {
        QMessageBox::information(this, "No data", "Run Solve first to generate a coverage field.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Coverage CSV", {}, "CSV file (*.csv);;All files (*)");
    if (path.isEmpty()) return;
    if (Exporter::exportCsv(m_engine.fieldGrid(), m_engine.terrain(), path.toStdString()))
        m_statusLabel->setText("  RF metrics CSV exported");
    else QMessageBox::critical(this, "Export failed", "Could not write CSV file.");
}

void MainWindow::onExportGeoTiff(CoverageGeoTiff::Metric metric) {
    if (!m_engine.fieldGrid().valid()) {
        QMessageBox::information(this, "No data", "Run Solve first to generate a coverage field.");
        return;
    }
    if (!CoverageGeoTiff::canEncode(m_engine.fieldGrid(), m_engine.terrain())) {
        QMessageBox::information(this, "No real-world location",
            "A GeoTIFF needs terrain with a real-world location. This terrain has "
            "none, so export the CSV, which gives positions in metres.");
        return;
    }
    const QString what = QString::fromUtf8(CoverageGeoTiff::description(metric));
    const QString path = QFileDialog::getSaveFileName(
        this, QString("Export %1 GeoTIFF").arg(what), {},
        "GeoTIFF (*.tif *.tiff);;All files (*)");
    if (path.isEmpty()) return;
    if (Exporter::exportGeoTiff(m_engine.fieldGrid(), m_engine.terrain(),
                                path.toStdString(), metric)) {
        const QString unit = QString::fromUtf8(CoverageGeoTiff::unit(metric));
        m_statusLabel->setText(QString("  %1 GeoTIFF exported (WGS 84%2)")
            .arg(what, unit.isEmpty() ? QString{} : QString(", %1").arg(unit)));
    }
    else QMessageBox::critical(this, "Export failed", "Could not write GeoTIFF file.");
}

void MainWindow::applyTheme() {
    setStyleSheet(WorkspaceTheme::styleSheet());
}
