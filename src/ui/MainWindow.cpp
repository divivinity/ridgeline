#include "MainWindow.h"
#include "data/TerrainLoader.h"
#include "data/ScenarioIO.h"
#include "data/Exporter.h"
#include "data/AntennaLibrary.h"
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
#include <stdexcept>
#include <algorithm>

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

    // ── Left dock — Scenario Tree ─────────────────────────────────────────────
    m_scenarioTree = new ScenarioTree(this);
    auto* leftDock = new QDockWidget("Scenario", this);
    leftDock->setWidget(m_scenarioTree);
    leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    leftDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    leftDock->setStyleSheet(
        "QDockWidget { color: rgba(255,255,255,0.7); font-family: monospace; font-size: 11px; }"
        "QDockWidget::title { background: rgba(15,25,35,0.9); padding: 4px; }"
    );
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    connect(m_scenarioTree, &ScenarioTree::addRequested,
            this, &MainWindow::onAddAntenna);
    connect(m_scenarioTree, &ScenarioTree::removeRequested,
            this, &MainWindow::onRemoveAntenna);
    connect(m_scenarioTree, &ScenarioTree::selectionChanged,
            this, &MainWindow::onAntennaSelected);

    // ── Right dock — Properties Panel ─────────────────────────────────────────
    m_propsPanel = new PropertiesPanel(this);
    m_propsPanel->setAntennaLibrary(&m_antennaLib);
    auto* rightDock = new QDockWidget("Properties", this);
    rightDock->setWidget(m_propsPanel);
    rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    rightDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    rightDock->setStyleSheet(leftDock->styleSheet());
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    connect(m_propsPanel, &PropertiesPanel::antennaChanged,
            this, &MainWindow::onAntennaChanged);

    // ── Main toolbar ──────────────────────────────────────────────────────────
    QToolBar* tb = addToolBar("Main");
    tb->setMovable(false);

    QLabel* freqLabel = new QLabel("  150 MHz  |  50 W  ");
    freqLabel->setStyleSheet("color: rgba(255,255,255,0.7); font-family: monospace;");
    tb->addWidget(freqLabel);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("Preview");
    m_modeCombo->addItem("Planner");
    m_modeCombo->addItem("Engineering");
    m_modeCombo->setCurrentIndex(1);
    m_modeCombo->setStyleSheet(
        "QComboBox {"
        "  background: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.2);"
        "  border-radius: 4px;"
        "  color: rgba(255,255,255,0.85);"
        "  padding: 2px 10px;"
        "  font-family: monospace;"
        "  min-width: 130px;"
        "}"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #1a2a3a;"
        "  color: rgba(255,255,255,0.85);"
        "  selection-background-color: rgba(79,195,247,0.3);"
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

    m_solveBtn = new QPushButton("  ⚡ Solve  ");
    m_solveBtn->setStyleSheet(
        "QPushButton {"
        "  background: rgba(79,195,247,0.2);"
        "  border: 1px solid rgba(79,195,247,0.5);"
        "  border-radius: 6px;"
        "  color: #4fc3f7;"
        "  padding: 4px 14px;"
        "  font-family: monospace;"
        "}"
        "QPushButton:hover { background: rgba(79,195,247,0.35); }"
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

    // ── Status bar ────────────────────────────────────────────────────────────
    m_statusLabel = new QLabel("  Ready");
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace;");
    statusBar()->addWidget(m_statusLabel);
    statusBar()->setStyleSheet("background: rgba(15,25,35,0.9);");

    // ── Bottom dock — Spectrum display ────────────────────────────────────────
    m_bottomBar = new BottomBar(this);
    auto* bottomDock = new QDockWidget("Spectrum", this);
    bottomDock->setWidget(m_bottomBar);
    bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    bottomDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    bottomDock->setStyleSheet(
        "QDockWidget { color: rgba(255,255,255,0.7); font-family: monospace; font-size: 11px; }"
        "QDockWidget::title { background: rgba(15,25,35,0.9); padding: 4px; }"
    );
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);

    // Populate tree with default antennas
    refreshTree();

    // Connect click-to-place signal
    connect(m_viewport, &ViewportWidget::terrainClicked,
            this, &MainWindow::onTerrainClicked);

    // Auto-load NC terrain for immediate geo-referenced basemap.
    // Deferred so the OpenGL context is fully initialised before reloadTerrain() runs.
    QTimer::singleShot(0, this, [this]() {
        // N35W083 = Asheville / Blue Ridge Parkway — dramatic mountain terrain
        QString ncTile = QCoreApplication::applicationDirPath()
                         + "/assets/terrain/nc/N35W083.hgt";
        if (QFileInfo::exists(ncTile))
            loadTerrain(ncTile);
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
    a1.position     = { w * 0.1f, t.heightAtWorld(w*0.1f, d*0.1f) + 10.0f, d * 0.1f };
    a1.frequencyMHz = 150.0f;
    a1.powerW       = 50.0f;
    a1.gainDbi      = 3.0f;

    Antenna a2;
    a2.position     = { w * 0.8f, t.heightAtWorld(w*0.8f, d*0.8f) + 10.0f, d * 0.8f };
    a2.frequencyMHz = 150.0f;
    a2.powerW       = 50.0f;
    a2.gainDbi      = 3.0f;

    m_engine.setAntennas({a1, a2});
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
    m_statusLabel->setText("  Solving...");
    m_solveBtn->setEnabled(false);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_engine.solve();
    m_viewport->refreshOverlays();
    m_statusLabel->setText("  Done");
    m_solveBtn->setEnabled(true);
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
    const auto& t = m_engine.terrain();
    const float cx = t.worldSize().x * 0.5f;
    const float cz = t.worldSize().z * 0.5f;
    Antenna a;
    a.position     = { cx, t.heightAtWorld(cx, cz) + 10.0f, cz };
    a.frequencyMHz = 150.0f;
    a.powerW       = 10.0f;
    a.gainDbi      = 3.0f;
    m_engine.addAntenna(a);
    m_selectedIndex = static_cast<int>(m_engine.antennas().size()) - 1;
    refreshTree();
    m_scenarioTree->selectRow(m_selectedIndex);
    refreshPropsPanel();
    refreshBottomBar();
    m_statusLabel->setText("  Antenna added");
}

void MainWindow::onRemoveAntenna(int index) {
    m_engine.removeAntenna(index);
    if (m_selectedIndex >= static_cast<int>(m_engine.antennas().size()))
        m_selectedIndex = static_cast<int>(m_engine.antennas().size()) - 1;
    refreshTree();
    refreshPropsPanel();
    refreshBottomBar();
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
    refreshBottomBar();
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
    const auto& t = m_engine.terrain();
    Antenna a;
    a.position     = { worldX, t.heightAtWorld(worldX, worldZ) + 10.0f, worldZ };
    a.frequencyMHz = 150.0f;
    a.powerW       = 10.0f;
    a.gainDbi      = 3.0f;
    m_engine.addAntenna(a);
    m_selectedIndex = static_cast<int>(m_engine.antennas().size()) - 1;
    refreshTree();
    m_scenarioTree->selectRow(m_selectedIndex);
    refreshPropsPanel();
    refreshBottomBar();
    m_statusLabel->setText(QString("  Antenna %1 placed")
                           .arg(m_engine.antennas().size()));
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

    if (Exporter::exportCsv(m_engine.fieldGrid(), path.toStdString())) {
        m_statusLabel->setText("  CSV exported");
    } else {
        QMessageBox::critical(this, "Export failed", "Could not write CSV file.");
    }
}

void MainWindow::applyTheme() {
    setStyleSheet(
        "QMainWindow, QWidget { background: #0f1923; color: rgba(255,255,255,0.85); }"
        "QToolBar {"
        "  background: rgba(15,25,35,0.75);"
        "  border-bottom: 1px solid rgba(255,255,255,0.08);"
        "  spacing: 8px; padding: 4px 8px;"
        "}"
        "QMenuBar { background: transparent; }"
        "QDockWidget { background: rgba(15,25,35,0.85); }"
    );
}
