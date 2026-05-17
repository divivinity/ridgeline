#include "MainWindow.h"
#include "data/TerrainLoader.h"
#include <QWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QApplication>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Atomic Biscuit");
    applyTheme();
    setupScene();

    m_viewport = new ViewportWidget(&m_engine, this);
    setCentralWidget(m_viewport);

    // Toolbar (glassmorphism strip)
    QToolBar* tb = addToolBar("Main");
    tb->setMovable(false);

    QLabel* freqLabel = new QLabel("  150 MHz  |  50 W  |  Fast Mode  ");
    freqLabel->setStyleSheet("color: rgba(255,255,255,0.7); font-family: monospace;");
    tb->addWidget(freqLabel);

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
    // Demo: rolling hills terrain, 200x200 cells at 10m each = 2km x 2km
    TerrainModel terrain = TerrainLoader::makeHills(200, 200, 10.0f, 40.0f);
    m_engine.setTerrain(std::move(terrain));

    // Two antennas placed in scene
    Antenna a1;
    a1.position      = {200.0f, m_engine.terrain().heightAtWorld(200,200)+10.0f, 200.0f};
    a1.frequencyMHz  = 150.0f;
    a1.powerW        = 50.0f;
    a1.gainDbi       = 3.0f;

    Antenna a2;
    a2.position      = {1600.0f, m_engine.terrain().heightAtWorld(1600,1600)+10.0f, 1600.0f};
    a2.frequencyMHz  = 150.0f;
    a2.powerW        = 50.0f;
    a2.gainDbi       = 3.0f;

    m_engine.setAntennas({a1, a2});
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
