#include "MainWindow.h"
#include "data/TerrainLoader.h"
#include "data/GlobalTerrainLoader.h"
#include "data/ScenarioIO.h"
#include "data/Exporter.h"
#include "data/AntennaLibrary.h"
#include "LocationDialog.h"
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
#include <QtConcurrent/QtConcurrentRun>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Ridgeline");
    applyTheme();
    setupScene();

    // ── Antenna library ────────────────────────────────────────────────────────
    try {
        m_antennaLib.load("assets/antennas.json");
    } catch (...) {
        m_antennaLib.loadDefaults();
    }

    // ── File menu ─────────────────────────────────────────────────────────────
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

    QMenu* exportMenu = fileMenu->addMenu("&Export");
    QAction* exportPngAct = exportMenu->addAction("Coverage &PNG…");
    connect(exportPngAct, &QAction::triggered, this, &MainWindow::onExportPng);
    QAction* exportCsvAct = exportMenu->addAction("Coverage &CSV…");
    connect(exportCsvAct, &QAction::triggered, this, &MainWindow::onExportCsv);

    // ── Central viewport ──────────────────────────────────────────────────────
    m_viewport = new ViewportWidget(&m_engine, this);
    setCentralWidget(m_viewport);

    // ── Left dock — Sources and selected-source settings ─────────────────────
    m_scenarioTree = new ScenarioTree(this);
    m_propsPanel = new PropertiesPanel(this);
    m_propsPanel->setAntennaLibrary(&m_antennaLib);
    m_scenarioTree->setMinimumHeight(285);
    m_propsPanel->setMinimumHeight(285);

    auto* sourceWorkspace = new QSplitter(Qt::Vertical, this);
    sourceWorkspace->setChildrenCollapsible(false);
    sourceWorkspace->addWidget(m_scenarioTree);
    sourceWorkspace->addWidget(m_propsPanel);
    sourceWorkspace->setStretchFactor(0, 3);
    sourceWorkspace->setStretchFactor(1, 2);
    sourceWorkspace->setSizes({350, 290});
    sourceWorkspace->setStyleSheet(
        "QSplitter::handle { background: #1f2d42; height: 1px; }"
        "QSplitter::handle:hover { background: #2dd4bf; }"
    );

    auto* leftDock = new QDockWidget("Sources", this);
    leftDock->setMinimumWidth(270);
    leftDock->setMaximumWidth(370);
    leftDock->setWidget(sourceWorkspace);
    leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    leftDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    leftDock->setStyleSheet(
        "QDockWidget { color: #94a3b8; font-size: 10px; }"
        "QDockWidget::title { background: #0b1422; padding: 7px 10px; border-bottom: 1px solid #1f2d42; }"
    );
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

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

    // ── Main toolbar ──────────────────────────────────────────────────────────
    QToolBar* tb = addToolBar("Main");
    tb->setMovable(false);

    auto* brand = new QLabel("  RIDGELINE  ");
    brand->setStyleSheet("color: #f8fafc; font-size: 15px; font-weight: 750; letter-spacing: 2px;");
    tb->addWidget(brand);

    auto* locationButton = new QPushButton("⌖  Location");
    locationButton->setObjectName("LocationButton");
    locationButton->setToolTip("Choose any location worldwide (Ctrl+L)");
    connect(locationButton, &QPushButton::clicked, this, &MainWindow::onChooseLocation);
    tb->addWidget(locationButton);
    m_locationLabel = new QLabel("Loading analysis area…");
    m_locationLabel->setObjectName("LocationSummary");
    tb->addWidget(m_locationLabel);

    auto* toolbarSpacer = new QWidget();
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(toolbarSpacer);

    auto* qualityLabel = new QLabel("QUALITY");
    qualityLabel->setStyleSheet("color: #64748b; font-size: 9px; font-weight: 700; letter-spacing: 1px;");
    tb->addWidget(qualityLabel);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("Quick preview");
    m_modeCombo->addItem("Balanced");
    m_modeCombo->addItem("High accuracy");
    m_modeCombo->setCurrentIndex(1);
    m_modeCombo->setStyleSheet(
        "QComboBox {"
        "  background: #111c2e;"
        "  border: 1px solid #2a3b52;"
        "  border-radius: 7px;"
        "  color: #dbe7f5;"
        "  padding: 5px 12px;"
        "  min-width: 135px;"
        "}"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #111c2e;"
        "  color: #dbe7f5;"
        "  selection-background-color: #0f766e;"
        "}"
    );
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
                    m_statusLabel->setText("  Engineering RF Mode");
                }
            });
    tb->addWidget(m_modeCombo);

    m_solveBtn = new QPushButton("Run coverage");
    m_solveBtn->setObjectName("RunButton");
    m_solveBtn->setStyleSheet(
        "QPushButton {"
        "  background: #0f766e;"
        "  border: 1px solid #2dd4bf;"
        "  border-radius: 8px;"
        "  color: white;"
        "  padding: 7px 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: #0d9488; }"
        "QPushButton:disabled { background: #1e293b; border-color: #334155; color: #64748b; }"
    );
    connect(m_solveBtn, &QPushButton::clicked, this, &MainWindow::onSolve);
    tb->addWidget(m_solveBtn);

    // ── Overlay toolbar (second row) ──────────────────────────────────────────
    m_overlayBar = new OverlayToolbar(this);
    QToolBar* overlayTb = addToolBar("Overlays");
    overlayTb->setMovable(false);
    overlayTb->addWidget(m_overlayBar);
    connect(m_overlayBar, &OverlayToolbar::overlayToggled,
            m_viewport, &ViewportWidget::setOverlayVisible);
    connect(m_overlayBar, &OverlayToolbar::basemapOpacityChanged,
            m_viewport, &ViewportWidget::setBasemapOpacity);

    // ── Status bar ────────────────────────────────────────────────────────────
    m_statusLabel = new QLabel("  Ready");
    m_statusLabel->setStyleSheet("color: #94a3b8; font-size: 11px;");
    statusBar()->addWidget(m_statusLabel);
    statusBar()->setStyleSheet("background: #0a1220; border-top: 1px solid #1e2d42;");
    connect(m_viewport, &ViewportWidget::basemapInfoChanged, this,
            [this](const QString& provider, const QString& attribution, const QString& error) {
        if (m_locationLabel) {
            m_locationLabel->setToolTip(
                QString("Basemap: %1\n%2%3")
                    .arg(provider, attribution,
                         error.isEmpty() ? QString{} : QString("\n%1").arg(error)));
        }
    });

    // ── Bottom dock — Spectrum display ────────────────────────────────────────
    m_bottomBar = new BottomBar(this);
    auto* bottomDock = new QDockWidget("Spectrum", this);
    bottomDock->setWidget(m_bottomBar);
    bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    bottomDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    bottomDock->setStyleSheet(
        "QDockWidget { color: #94a3b8; font-size: 10px; }"
        "QDockWidget::title { background: #0b1422; padding: 7px 10px; border-bottom: 1px solid #1f2d42; }"
    );
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    bottomDock->hide();

    QTimer::singleShot(0, this, [this, leftDock]() {
        addDockWidget(Qt::LeftDockWidgetArea, leftDock);
        leftDock->show();
        resizeDocks({leftDock}, {320}, Qt::Horizontal);
    });

    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(leftDock->toggleViewAction());
    viewMenu->addAction(bottomDock->toggleViewAction());

    // Populate tree with default antennas
    refreshTree();

    // Connect click-to-place signal
    connect(m_viewport, &ViewportWidget::terrainClicked,
            this, &MainWindow::onTerrainClicked);
    connect(m_viewport, &ViewportWidget::placementCancelled, this, [this]() {
        m_placementActive = false;
        m_statusLabel->setText("  Placement cancelled");
    });

    // Start with a real global terrain selection. Deferred until OpenGL is ready.
    QTimer::singleShot(0, this, [this]() {
        loadGlobalLocation(35.5951, -82.5515, 15.0, "Asheville, North Carolina");
    });
}

void MainWindow::setupScene() {
    TerrainModel terrain = TerrainLoader::makeHills(200, 200, 10.0f, 40.0f);
    m_engine.setTerrain(std::move(terrain));
    m_terrainSource = TerrainSource{};  // default: Type::Hills, 200×200, 10m cell, 40m amplitude
    placeDefaultAntennas();
    m_engine.loadMaterials("assets/materials.json");
}

void MainWindow::placeDefaultAntennas() {
    const auto& t = m_engine.terrain();
    const float w = t.worldSize().x;
    const float d = t.worldSize().z;

    Antenna a1;
    a1.position     = { w * 0.5f, t.heightAtWorld(w*0.5f, d*0.5f) + 10.0f, d * 0.5f };
    a1.frequencyMHz = 150.0f;
    a1.powerW       = 50.0f;
    a1.gainDbi      = 3.0f;

    m_engine.setAntennas({a1});
}

void MainWindow::loadGlobalLocation(double latitude, double longitude,
                                    double extentKm, const QString& name) {
    m_statusLabel->setText("  Loading real-world elevation…");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    try {
        m_placementActive = false;
        if (m_viewport) m_viewport->setPlacementMode(false);
        TerrainModel terrain = GlobalTerrainLoader::load(
            latitude, longitude, extentKm, 513);
        m_engine.setTerrain(std::move(terrain));
        m_terrainSource = TerrainSource{};
        m_terrainSource.type = TerrainSource::Type::Global;
        m_terrainSource.centerLat = latitude;
        m_terrainSource.centerLon = longitude;
        m_terrainSource.extentKm = extentKm;
        m_locationName = name;
        placeDefaultAntennas();

        if (m_viewport) m_viewport->reloadTerrain();
        m_selectedIndex = 0;
        refreshTree();
        m_scenarioTree->selectRow(m_selectedIndex);
        refreshPropsPanel();
        refreshBottomBar();
        updateLocationSummary();
        setWindowTitle(m_locationName.isEmpty()
            ? QString("Ridgeline — %1°, %2°")
                .arg(latitude, 0, 'f', 4).arg(longitude, 0, 'f', 4)
            : QString("Ridgeline — %1").arg(m_locationName.section(',', 0, 0)));
        m_statusLabel->setText(
            "  Real elevation and satellite imagery aligned · Place emitters or run coverage");
    } catch (const std::exception& e) {
        m_statusLabel->setText("  Location load failed");
        QMessageBox::critical(this, "Could not load location", e.what());
    }
    QApplication::restoreOverrideCursor();
}

void MainWindow::onChooseLocation() {
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

void MainWindow::updateLocationSummary() {
    if (!m_locationLabel) return;
    const QString area = QString("%1 × %1 km").arg(m_terrainSource.extentKm, 0, 'f', 0);
    if (!m_locationName.isEmpty()) {
        m_locationLabel->setText(QString("%1  ·  %2")
            .arg(m_locationName.section(',', 0, 0), area));
    } else {
        m_locationLabel->setText(QString("%1°, %2°  ·  %3")
            .arg(m_terrainSource.centerLat, 0, 'f', 4)
            .arg(m_terrainSource.centerLon, 0, 'f', 4)
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

void MainWindow::loadTerrain(const QString& path) {
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
        if (ext == "hgt") {
            m_terrainSource.type    = TerrainSource::Type::Hgt;
            m_terrainSource.hgtPath = path.toStdString();
        } else if (ext == "tif" || ext == "tiff") {
            m_terrainSource.type    = TerrainSource::Type::GeoTiff;
            m_terrainSource.hgtPath = path.toStdString();
        } else {
            // All non-HGT paths go through fromDTED — record accordingly.
            m_terrainSource.type    = TerrainSource::Type::Dted;
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

        m_selectedIndex = -1;
        refreshTree();
        refreshPropsPanel();
        refreshBottomBar();

        setWindowTitle(QString("Ridgeline — %1").arg(QFileInfo(path).fileName()));
        m_statusLabel->setText("  Terrain loaded");
    } catch (const std::exception& e) {
        m_statusLabel->setText("  Ready");
        QMessageBox::critical(this, "Load failed", e.what());
    }
}

void MainWindow::onSolve() {
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

    m_statusLabel->setText("  Calculating terrain-aware coverage…");
    m_solveBtn->setEnabled(false);
    m_solveBtn->setText("Calculating…");
    QElapsedTimer timer;
    timer.start();

    QProgressDialog progress(this);
    progress.setWindowTitle("Calculating coverage");
    progress.setLabelText("Preparing terrain paths…");
    progress.setRange(0, 0);
    progress.setCancelButton(nullptr);
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

    QPointer<QProgressDialog> progressGuard(&progress);
    const QString quality = m_modeCombo->currentText();
    auto reportProgress = [progressGuard, quality, &timer](int completed, int total) {
        QMetaObject::invokeMethod(qApp, [progressGuard, quality, completed, total, &timer]() {
            if (!progressGuard) return;
            if (progressGuard->maximum() != total) progressGuard->setRange(0, total);
            progressGuard->setValue(completed);
            const int percent = total > 0 ? completed * 100 / total : 0;
            progressGuard->setLabelText(
                QString("%1 analysis · %2% complete · %3 s elapsed")
                    .arg(quality).arg(percent).arg(timer.elapsed() / 1000.0, 0, 'f', 1));
        }, Qt::QueuedConnection);
    };

    std::exception_ptr solveError;
    QFutureWatcher<void> watcher;
    QEventLoop waitLoop;
    connect(&watcher, &QFutureWatcher<void>::finished, &waitLoop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([this, reportProgress, &solveError]() {
        try {
            m_engine.solve(reportProgress);
        } catch (...) {
            solveError = std::current_exception();
        }
    }));
    progress.show();
    waitLoop.exec();
    progress.close();

    try {
        if (solveError) std::rethrow_exception(solveError);
        m_viewport->refreshOverlays();
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
        m_statusLabel->setText(QString("  Coverage ready in %1 s · %2 × %3 cells%4")
            .arg(timer.elapsed() / 1000.0, 0, 'f', 1)
            .arg(grid.width).arg(grid.height).arg(signalRange));
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
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Terrain", {},
        "Terrain files (*.dt0 *.dt1 *.dt2 *.hgt *.tif *.tiff);;"
        "GeoTIFF DEM files (*.tif *.tiff);;"
        "DTED files (*.dt0 *.dt1 *.dt2);;"
        "SRTM HGT files (*.hgt);;"
        "All files (*)");
    if (!path.isEmpty()) loadTerrain(path);
}

void MainWindow::onAddAntenna() {
    startPlacement(false);
}

void MainWindow::onAddJammer() {
    startPlacement(true);
}

void MainWindow::onRemoveAntenna(int index) {
    m_engine.removeAntenna(index);
    if (m_selectedIndex >= static_cast<int>(m_engine.antennas().size()))
        m_selectedIndex = static_cast<int>(m_engine.antennas().size()) - 1;
    refreshTree();
    refreshPropsPanel();
    refreshBottomBar();
    m_viewport->refreshOverlays();
    m_statusLabel->setText("  Antenna removed");
}

void MainWindow::onAntennaSelected(int index) {
    m_selectedIndex = index;
    refreshPropsPanel();
    refreshBottomBar();
}

void MainWindow::onAntennaChanged(Antenna updated) {
    if (m_selectedIndex < 0) return;
    const float ground = m_engine.terrain().heightAtWorld(
        updated.position.x, updated.position.z);
    updated.position.y = ground + std::max(0.0f, updated.position.y);
    m_engine.updateAntenna(m_selectedIndex, updated);
    refreshTree();
    m_scenarioTree->selectRow(m_selectedIndex);
    refreshBottomBar();
    m_viewport->refreshOverlays();
}

void MainWindow::refreshTree() {
    m_scenarioTree->refresh(m_engine.antennas());
}

void MainWindow::refreshPropsPanel() {
    if (m_selectedIndex >= 0 &&
        m_selectedIndex < static_cast<int>(m_engine.antennas().size())) {
        m_propsAntennaView = m_engine.antennas()[m_selectedIndex];
        const float ground = m_engine.terrain().heightAtWorld(
            m_propsAntennaView.position.x, m_propsAntennaView.position.z);
        m_propsAntennaView.position.y =
            std::max(0.0f, m_propsAntennaView.position.y - ground);
        m_propsPanel->load(&m_propsAntennaView);
    } else {
        m_propsPanel->load(nullptr);
    }
}

void MainWindow::refreshBottomBar() {
    const auto& antennas = m_engine.antennas();
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(antennas.size()))
        m_bottomBar->setAntenna(antennas[m_selectedIndex]);
    else
        m_bottomBar->clearAntenna();
}

void MainWindow::onSaveScenario() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Scenario", {},
        "Ridgeline scenario (*.json);;All files (*)");
    if (path.isEmpty()) return;

    ScenarioDoc doc;
    doc.terrain  = m_terrainSource;
    doc.mode     = m_engine.mode;
    doc.antennas = m_engine.antennas();

    try {
        ScenarioIO::save(doc, path.toStdString());
        m_statusLabel->setText("  Scenario saved");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

void MainWindow::onOpenScenario() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Scenario", {},
        "Ridgeline scenario (*.json);;All files (*)");
    if (path.isEmpty()) return;

    try {
        m_statusLabel->setText("  Loading scenario…");
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        ScenarioDoc doc = ScenarioIO::load(path.toStdString());
        applyScenarioDoc(doc);
        setWindowTitle(QString("Ridgeline — %1").arg(QFileInfo(path).fileName()));
        m_statusLabel->setText("  Scenario loaded");
    } catch (const std::exception& e) {
        m_statusLabel->setText("  Ready");
        QMessageBox::critical(this, "Open failed", e.what());
    }
}

void MainWindow::applyScenarioDoc(const ScenarioDoc& doc) {
    TerrainModel t;
    const auto& ts = doc.terrain;
    switch (ts.type) {
        case TerrainSource::Type::Hgt:
            t = TerrainLoader::fromHGT(ts.hgtPath,
                                        static_cast<float>(ts.hgtLat),
                                        static_cast<float>(ts.hgtLon),
                                        ts.hgtSize);
            break;
        case TerrainSource::Type::Dted:
            t = TerrainLoader::fromDTED(ts.hgtPath);
            break;
        case TerrainSource::Type::GeoTiff:
            t = TerrainLoader::fromGeoTIFF(ts.hgtPath);
            break;
        case TerrainSource::Type::Global:
            t = GlobalTerrainLoader::load(ts.centerLat, ts.centerLon,
                                          ts.extentKm, 513);
            break;
        case TerrainSource::Type::Flat:
            t = TerrainLoader::makeFlat(ts.widthCells, ts.heightCells, ts.cellSize);
            break;
        case TerrainSource::Type::Hills:
        default:
            t = TerrainLoader::makeHills(ts.widthCells, ts.heightCells,
                                          ts.cellSize, ts.amplitude);
            break;
    }
    m_engine.setTerrain(std::move(t));
    m_terrainSource = doc.terrain;  // only update after successful terrain build
    m_locationName.clear();
    if (m_terrainSource.type == TerrainSource::Type::Global)
        updateLocationSummary();

    m_engine.mode = doc.mode;
    if (doc.mode == SimEngine::Mode::Preview)
        m_modeCombo->setCurrentIndex(0);
    else if (doc.mode == SimEngine::Mode::Engineering)
        m_modeCombo->setCurrentIndex(2);
    else
        m_modeCombo->setCurrentIndex(1);
    m_engine.setAntennas(doc.antennas);

    m_selectedIndex = -1;
    if (m_viewport) m_viewport->reloadTerrain();
    refreshTree();
    refreshPropsPanel();
    refreshBottomBar();
}

void MainWindow::onTerrainClicked(float worldX, float worldZ) {
    if (!m_placementActive) return;
    const auto& t = m_engine.terrain();
    Antenna a;
    a.position     = { worldX, t.heightAtWorld(worldX, worldZ) + 10.0f, worldZ };
    a.frequencyMHz = 150.0f;
    a.powerW       = 10.0f;
    a.gainDbi      = 3.0f;
    a.isJammer     = m_placingJammer;
    m_engine.addAntenna(a);
    m_selectedIndex = static_cast<int>(m_engine.antennas().size()) - 1;
    refreshTree();
    m_scenarioTree->selectRow(m_selectedIndex);
    refreshPropsPanel();
    refreshBottomBar();
    m_viewport->refreshOverlays();
    m_placementActive = false;
    m_viewport->setPlacementMode(false);
    const auto ll = t.worldToLatLon(worldX, worldZ);
    const QString role = a.isJammer ? "Jammer" : "Emitter";
    m_statusLabel->setText(t.hasGeoReference()
        ? QString("  %1 placed at %2°, %3°")
            .arg(role).arg(ll.latDeg, 0, 'f', 6).arg(ll.lonDeg, 0, 'f', 6)
        : QString("  %1 placed").arg(role));
}

void MainWindow::onExportPng() {
    if (!m_engine.fieldGrid().valid()) {
        QMessageBox::information(this, "No data",
            "Run Solve first to generate a coverage field.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Coverage PNG", {},
        "PNG image (*.png);;All files (*)");
    if (path.isEmpty()) return;

    if (Exporter::exportPng(m_engine.fieldGrid(), path.toStdString())) {
        m_statusLabel->setText("  PNG exported");
    } else {
        QMessageBox::critical(this, "Export failed", "Could not write PNG file.");
    }
}

void MainWindow::onExportCsv() {
    if (!m_engine.fieldGrid().valid()) {
        QMessageBox::information(this, "No data",
            "Run Solve first to generate a coverage field.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Coverage CSV", {},
        "CSV file (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    if (Exporter::exportCsv(m_engine.fieldGrid(), m_engine.terrain(), path.toStdString())) {
        m_statusLabel->setText("  CSV exported");
    } else {
        QMessageBox::critical(this, "Export failed", "Could not write CSV file.");
    }
}

void MainWindow::applyTheme() {
    setStyleSheet(
        "QMainWindow { background: #08111f; color: #dbe7f5; }"
        "QWidget { color: #dbe7f5; font-family: 'Segoe UI'; }"
        "QToolBar {"
        "  background: #0b1422;"
        "  border: none; border-bottom: 1px solid #1e2d42;"
        "  spacing: 10px; padding: 7px 10px;"
        "}"
        "QMenuBar { background: #08111f; color: #94a3b8; padding: 2px; }"
        "QMenuBar::item:selected { background: #172338; color: #f8fafc; border-radius: 4px; }"
        "QMenu { background: #101b2d; border: 1px solid #2a3a51; padding: 5px; }"
        "QMenu::item { padding: 6px 24px; border-radius: 4px; }"
        "QMenu::item:selected { background: #0f766e; color: white; }"
        "QDockWidget { background: #0b1422; }"
        "QPushButton#LocationButton { background: #132238; border: 1px solid #2a3b52;"
        "  border-radius: 7px; color: #ccfbf1; padding: 6px 12px; font-weight: 650; }"
        "QPushButton#LocationButton:hover { background: #17404a; border-color: #2dd4bf; }"
        "QLabel#LocationSummary { color: #94a3b8; font-size: 11px; padding-right: 8px; }"
    );
}
