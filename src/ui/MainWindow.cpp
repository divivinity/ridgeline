#include "MainWindow.h"
#include "data/TerrainLoader.h"
#include <QWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QApplication>
#include <QMenuBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <stdexcept>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Ridgeline");
    applyTheme();
    setupScene();

    m_viewport = new ViewportWidget(&m_engine, this);
    setCentralWidget(m_viewport);

    // File menu
    QMenu* fileMenu = menuBar()->addMenu("&File");
    QAction* loadAct = fileMenu->addAction("&Load Terrain…");
    loadAct->setShortcut(QKeySequence::Open);
    connect(loadAct, &QAction::triggered, this, &MainWindow::onLoadTerrain);

    // Toolbar
    QToolBar* tb = addToolBar("Main");
    tb->setMovable(false);

    QLabel* freqLabel = new QLabel("  150 MHz  |  50 W  ");
    freqLabel->setStyleSheet("color: rgba(255,255,255,0.7); font-family: monospace;");
    tb->addWidget(freqLabel);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("⚡ Fast Mode");
    m_modeCombo->addItem("\U0001F52C Full Physics");
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
                m_engine.mode = (idx == 0) ? SimEngine::Mode::Fast
                                           : SimEngine::Mode::Full;
                m_statusLabel->setText(idx == 0 ? "  Fast Mode"
                                                 : "  Full Physics Mode");
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

    m_statusLabel = new QLabel("  Ready");
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace;");
    statusBar()->addWidget(m_statusLabel);
    statusBar()->setStyleSheet("background: rgba(15,25,35,0.9);");
}

void MainWindow::setupScene() {
    TerrainModel terrain = TerrainLoader::makeHills(200, 200, 10.0f, 40.0f);
    m_engine.setTerrain(std::move(terrain));
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

        TerrainModel t = TerrainLoader::fromDTED(path.toStdString());
        m_engine.setTerrain(std::move(t));
        placeDefaultAntennas();

        if (m_viewport) m_viewport->reloadTerrain();

        setWindowTitle(QString("Ridgeline — %1")
                       .arg(QFileInfo(path).fileName()));
        m_statusLabel->setText("  Terrain loaded");
    } catch (const std::exception& e) {
        m_statusLabel->setText("  Ready");
        QMessageBox::critical(this, "Load failed", e.what());
    }
}

void MainWindow::onLoadTerrain() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Load DTED Terrain", {},
        "DTED files (*.dt0 *.dt1 *.dt2);;All files (*)");
    if (!path.isEmpty()) loadTerrain(path);
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

void MainWindow::applyTheme() {
    setStyleSheet(
        "QMainWindow, QWidget { background: #0f1923; color: rgba(255,255,255,0.85); }"
        "QToolBar {"
        "  background: rgba(15,25,35,0.75);"
        "  border-bottom: 1px solid rgba(255,255,255,0.08);"
        "  spacing: 8px; padding: 4px 8px;"
        "}"
        "QMenuBar { background: transparent; }"
    );
}
