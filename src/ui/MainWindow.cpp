#include "MainWindow.h"
#include "CompactLocationLabel.h"
#include "WorkspaceTheme.h"
#include "LayersPanel.h"
#include "TerrainPresentation.h"
#include <QToolButton>
#include "RuntimePaths.h"
#include "data/TerrainLoader.h"
#include "data/GlobalTerrainLoader.h"
#include "data/ScenarioIO.h"
#include "data/MeasurementIO.h"
#include "data/Exporter.h"
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
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QPointer>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QUndoCommand>
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

class SourceStateCommand final : public QUndoCommand {
public:
    using Apply = std::function<void(const std::vector<Antenna>&, int)>;

    SourceStateCommand(std::vector<Antenna> before,
                       int selectedBefore,
                       std::vector<Antenna> after,
                       int selectedAfter,
                       QString description,
                       int mergeId,
                       Apply apply)
        : QUndoCommand(std::move(description)),
          m_before(std::move(before)),
          m_after(std::move(after)),
          m_selectedBefore(selectedBefore),
          m_selectedAfter(selectedAfter),
          m_mergeId(mergeId),
          m_timestampMs(QDateTime::currentMSecsSinceEpoch()),
          m_apply(std::move(apply)) {}

    void undo() override { m_apply(m_before, m_selectedBefore); }
    void redo() override { m_apply(m_after, m_selectedAfter); }
    int id() const override { return m_mergeId; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* newer = dynamic_cast<const SourceStateCommand*>(other);
        if (!newer || m_mergeId < 0 || newer->m_mergeId != m_mergeId ||
            newer->m_timestampMs - m_timestampMs > 900) {
            return false;
        }
        m_after = newer->m_after;
        m_selectedAfter = newer->m_selectedAfter;
        m_timestampMs = newer->m_timestampMs;
        return true;
    }

private:
    std::vector<Antenna> m_before;
    std::vector<Antenna> m_after;
    int m_selectedBefore = -1;
    int m_selectedAfter = -1;
    int m_mergeId = -1;
    qint64 m_timestampMs = 0;
    Apply m_apply;
};

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

    QMenu* editMenu = menuBar()->addMenu("&Edit");
    QAction* undoAction = m_undoStack->createUndoAction(this, "&Undo");
    undoAction->setObjectName("UndoSourceAction");
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    QAction* redoAction = m_undoStack->createRedoAction(this, "&Redo");
    redoAction->setObjectName("RedoSourceAction");
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);

    QMenu* analysisMenu = menuBar()->addMenu("&Analysis");
    QAction* receiverAct = analysisMenu->addAction("&Receiver / Service Criteria…");
    connect(receiverAct, &QAction::triggered, this, &MainWindow::onReceiverSettings);
    QAction* comparisonAct = analysisMenu->addAction("Scenario &Comparison…");
    comparisonAct->setObjectName("ScenarioComparisonAction");
    QAction* calibrationAct = analysisMenu->addAction("Measurement &Calibration…");
    calibrationAct->setObjectName("MeasurementCalibrationAction");

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
    auto* resetViewAction = viewMenu->addAction("Reset map view");
    connect(resetViewAction, &QAction::triggered, m_viewport, &ViewportWidget::resetView);

    // Bind the stack last, so the viewport and panel are both live before it
    // pushes its first state to them.
    m_viewport->setLayerStack(&m_layerStack);
    m_layersPanel->setStack(&m_layerStack);

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
        markScenarioDirty();
        m_engine.setFieldGrid({});
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
        m_statusLabel->setText("  Cancel the terrain load before closing");
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
    if (confirmDiscardChanges()) event->accept();
    else                         event->ignore();
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
        m_engine.setTerrain(std::move(t));
        terrainInstalled = true;
        markScenarioDirty();
        m_engine.setFieldGrid({});
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
        if (m_viewport) m_viewport->reloadTerrain();
        m_selectedIndex = m_engine.antennas().empty() ? -1 : 0;
        refreshTree();
        if (m_selectedIndex >= 0) m_scenarioTree->selectRow(m_selectedIndex);
        refreshPropsPanel();
        refreshBottomBar();
        markScenarioDirty();
        setWindowTitle(QString("Ridgeline — %1[*]").arg(QFileInfo(path).fileName()));
        m_statusLabel->setText("  Terrain loaded · Existing georeferenced sources preserved");
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

void MainWindow::onSaveScenario() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Scenario", {},
        "Ridgeline scenario (*.json);;All files (*)");
    if (path.isEmpty()) return;

    ScenarioDoc doc;
    doc.terrain = m_terrainSource;
    doc.mode = m_engine.mode;
    doc.receiver = m_engine.receiver;
    doc.calibration = m_engine.calibration;
    doc.calibrationSourceName =
        m_appliedCalibrationSourceName.toStdString();
    doc.calibrationPairedSamples = m_appliedCalibrationPairedSamples;
    doc.calibrationRmseBeforeDb = m_appliedCalibrationRmseBeforeDb;
    doc.calibrationRmseAfterDb = m_appliedCalibrationRmseAfterDb;
    doc.antennas = m_engine.antennas();
    doc.layers.clear();
    for (const LayerStack::State& layer : m_layerStack.state())
        doc.layers.push_back({layer.id.toStdString(), layer.visible, layer.opacity});

    try {
        ScenarioIO::save(doc, path.toStdString());
        markScenarioClean();
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
    markScenarioDirty();
    m_engine.setFieldGrid({});
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
    if (!doc.layers.empty()) {
        std::vector<LayerStack::State> layers;
        layers.reserve(doc.layers.size());
        for (const ScenarioLayerState& layer : doc.layers)
            layers.push_back({QString::fromStdString(layer.id), layer.visible, layer.opacity});
        m_layerStack.applyState(layers);
    }

    m_selectedIndex = -1;
    if (m_viewport) {
        progress.setLabelText("Loading satellite imagery · Scenario settings loaded");
        progress.setRange(0, 0);
        m_viewport->setBasemapLoadCallbacks([&progress](int completed, int total) {
            if (progress.wasCanceled()) return;
            progress.setLabelText(QString("Satellite imagery · %1 / %2").arg(completed).arg(total));
            progress.setRange(0, std::max(total, 1));
            progress.setValue(completed);
        }, cancelLoad);
        m_viewport->reloadTerrain();
        m_viewport->setBasemapLoadCallbacks({}, {});
    }
    refreshTree();
    refreshPropsPanel();
    refreshBottomBar();
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

void MainWindow::applyTheme() {
    setStyleSheet(WorkspaceTheme::styleSheet());
}
