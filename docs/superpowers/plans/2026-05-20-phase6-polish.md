# Phase 6 — Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bottom-bar spectrum display for the selected antenna and keyboard shortcuts for camera control.

**Architecture:** A pure-C++ `SpectrumCalc` module (no Qt, fully testable) computes Friis FSPL sweeps; `BottomBar` wraps it in a `QPainter` widget wired into `MainWindow`. `ViewportWidget` gains a `keyPressEvent` that drives its already-public `Camera` fields.

**Tech Stack:** C++17, Qt6 (QPainter, QLinearGradient, QPainterPath), CMake GLOB_RECURSE (main) / explicit sources (tests)

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/sim/SpectrumCalc.h` | Create | Pure Friis sweep struct + function — no deps |
| `src/sim/SpectrumCalc.cpp` | Create | `sweepSpectrum()` implementation |
| `src/ui/BottomBar.h` | Create | QPainter widget declaration |
| `src/ui/BottomBar.cpp` | Create | `paintEvent`, `setAntenna`, `clearAntenna` |
| `src/ui/ViewportWidget.h` | Modify | Add `keyPressEvent` declaration, `m_defaultCamera` |
| `src/ui/ViewportWidget.cpp` | Modify | Focus policy, keyboard handler, save default camera |
| `src/ui/MainWindow.h` | Modify | Add `BottomBar* m_bottomBar` member |
| `src/ui/MainWindow.cpp` | Modify | Bottom dock, wire to selection/change signals |
| `tests/test_main.cpp` | Modify | SpectrumCalc unit tests |
| `CMakeLists.txt` | Modify | Add `SpectrumCalc.cpp` to test binary |

---

### Task 1: SpectrumCalc — pure Friis sweep function

**Files:**
- Create: `src/sim/SpectrumCalc.h`
- Create: `src/sim/SpectrumCalc.cpp`
- Modify: `tests/test_main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

Add these includes and test functions to `tests/test_main.cpp`, just before the existing `main()` block:

```cpp
// ── SpectrumCalc ──────────────────────────────────────────────────────────────
#include "sim/SpectrumCalc.h"

static void test_spectrum_length() {
    auto pts = sweepSpectrum(50.0f, 3.0f, 1000.0f, 30.0f, 3000.0f, 60);
    assert(pts.size() == 60);
    std::cout << "[PASS] test_spectrum_length\n";
}

static void test_spectrum_freq_range() {
    auto pts = sweepSpectrum(50.0f, 3.0f, 1000.0f, 30.0f, 3000.0f, 60);
    assert(pts.front().freqMHz == 30.0f);
    assert(pts.back().freqMHz == 3000.0f);
    std::cout << "[PASS] test_spectrum_freq_range\n";
}

static void test_spectrum_higher_freq_lower_power() {
    // Higher frequency → more free-space path loss → lower received power.
    auto pts = sweepSpectrum(50.0f, 3.0f, 1000.0f, 30.0f, 3000.0f, 2);
    assert(pts[0].powerDbm > pts[1].powerDbm);
    std::cout << "[PASS] test_spectrum_higher_freq_lower_power\n";
}

static void test_spectrum_power_reasonable() {
    // 50 W + 3 dBi at 150 MHz / 1 km should land in a plausible range.
    // Friis: FSPL(150 MHz, 1 km) ≈ 93.5 dB; P_tx = 47 dBm; P_rx ≈ -43 dBm.
    auto pts = sweepSpectrum(50.0f, 3.0f, 1000.0f, 150.0f, 150.0f, 1);
    assert(pts[0].powerDbm > -80.0f && pts[0].powerDbm < -20.0f);
    std::cout << "[PASS] test_spectrum_power_reasonable\n";
}

static void test_spectrum_single_step() {
    // steps=1 must not divide by zero and must return exactly one point.
    auto pts = sweepSpectrum(10.0f, 0.0f, 500.0f, 100.0f, 100.0f, 1);
    assert(pts.size() == 1);
    assert(pts[0].freqMHz == 100.0f);
    std::cout << "[PASS] test_spectrum_single_step\n";
}
```

Then call them all in `main()`:

```cpp
    test_spectrum_length();
    test_spectrum_freq_range();
    test_spectrum_higher_freq_lower_power();
    test_spectrum_power_reasonable();
    test_spectrum_single_step();
```

- [ ] **Step 2: Run to confirm compile failure**

```powershell
$env:PATH += ";C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1310_64\bin"
cmake --build . -j8 2>&1 | Select-String "error:" | Select-Object -First 10
```

Expected: compile error — `sweepSpectrum` not declared.

- [ ] **Step 3: Create `src/sim/SpectrumCalc.h`**

```cpp
#pragma once
#include <vector>

struct SpectrumPoint {
    float freqMHz;
    float powerDbm;
};

// Friis FSPL sweep. Computes received signal power at referenceDistM
// across a linear frequency sweep from freqStartMHz to freqEndMHz.
//   powerW         – transmit power in watts
//   gainDbi        – Tx antenna gain in dBi (Rx assumed 0 dBi)
//   referenceDistM – link distance in metres
//   freqStartMHz / freqEndMHz – inclusive sweep endpoints
//   steps          – number of sample points (>= 1)
// Throws std::invalid_argument if steps < 1.
std::vector<SpectrumPoint> sweepSpectrum(
    float powerW, float gainDbi,
    float referenceDistM,
    float freqStartMHz, float freqEndMHz, int steps);
```

- [ ] **Step 4: Create `src/sim/SpectrumCalc.cpp`**

```cpp
#include "SpectrumCalc.h"
#include <cmath>
#include <stdexcept>

std::vector<SpectrumPoint> sweepSpectrum(
    float powerW, float gainDbi,
    float referenceDistM,
    float freqStartMHz, float freqEndMHz, int steps)
{
    if (steps < 1) throw std::invalid_argument("steps must be >= 1");

    // P_tx in dBm
    const float txDbm = 10.0f * std::log10(powerW * 1000.0f);

    // Friis FSPL (dB) = 20·log10(4π/c) + 20·log10(d_m) + 20·log10(f_Hz)
    // 20·log10(4π / 3×10⁸) ≈ −147.552
    const float K        = 20.0f * std::log10(4.0f * 3.14159265f / 3.0e8f);
    const float distTerm = 20.0f * std::log10(referenceDistM);

    std::vector<SpectrumPoint> out;
    out.reserve(steps);

    for (int i = 0; i < steps; ++i) {
        const float t    = (steps == 1) ? 0.0f
                                        : static_cast<float>(i) / (steps - 1);
        const float fMHz = freqStartMHz + t * (freqEndMHz - freqStartMHz);
        const float fHz  = fMHz * 1.0e6f;

        const float fspl  = K + distTerm + 20.0f * std::log10(fHz);
        const float rxDbm = txDbm + gainDbi - fspl;
        out.push_back({ fMHz, rxDbm });
    }
    return out;
}
```

- [ ] **Step 5: Add `SpectrumCalc.cpp` to the test binary in `CMakeLists.txt`**

Find the `add_executable(ridgeline_tests ...)` block (around line 66) and add `src/sim/SpectrumCalc.cpp`:

```cmake
add_executable(ridgeline_tests
    tests/test_main.cpp
    src/sim/SpectrumCalc.cpp         # ← add this line
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/FastSolver.cpp
    src/sim/FullSolver.cpp
    src/sim/SimEngine.cpp
    src/data/AntennaLibrary.cpp
    src/data/ScenarioIO.cpp
)
```

- [ ] **Step 6: Run tests to confirm they pass**

```powershell
cmake --build . -j8
.\ridgeline_tests.exe 2>&1 | Select-String "spectrum|FAIL" -CaseSensitive:$false
```

Expected: five `[PASS]` lines for spectrum tests, no FAILs.

- [ ] **Step 7: Commit**

```bash
git add src/sim/SpectrumCalc.h src/sim/SpectrumCalc.cpp tests/test_main.cpp CMakeLists.txt
git commit -m "feat: add SpectrumCalc Friis sweep with 5 unit tests"
```

---

### Task 2: BottomBar widget

**Files:**
- Create: `src/ui/BottomBar.h`
- Create: `src/ui/BottomBar.cpp`

`BottomBar` uses Qt (QPainter) so it is **not** added to `ridgeline_tests`. The main binary picks it up automatically via `GLOB_RECURSE`.

- [ ] **Step 1: Create `src/ui/BottomBar.h`**

```cpp
#pragma once
#include <QWidget>
#include <vector>
#include "sim/SpectrumCalc.h"
#include "sim/Types.h"

// Bottom-dock spectrum display.
// Call setAntenna() when the user selects an antenna.
// Call clearAntenna() when nothing is selected.
// Paints a Friis FSPL sweep from 30–3000 MHz at a 1 km reference distance.
class BottomBar : public QWidget {
    Q_OBJECT
public:
    explicit BottomBar(QWidget* parent = nullptr);

    void setAntenna(const Antenna& antenna);
    void clearAntenna();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<SpectrumPoint> m_spectrum;
    bool m_hasAntenna = false;

    static constexpr float kMinDbm = -120.0f;
    static constexpr float kMaxDbm =  -30.0f;
};
```

- [ ] **Step 2: Create `src/ui/BottomBar.cpp`**

```cpp
#include "BottomBar.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <algorithm>  // std::clamp

BottomBar::BottomBar(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(90);
    setMaximumHeight(150);
    setStyleSheet("background: rgb(10, 18, 28);");
}

void BottomBar::setAntenna(const Antenna& antenna) {
    m_spectrum = sweepSpectrum(
        antenna.powerW, antenna.gainDbi,
        1000.0f,         // 1 km reference distance
        30.0f, 3000.0f,  // 30 MHz – 3 GHz sweep
        120);            // 120 sample points
    m_hasAntenna = true;
    update();
}

void BottomBar::clearAntenna() {
    m_spectrum.clear();
    m_hasAntenna = false;
    update();
}

void BottomBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(10, 18, 28));

    const int marginL = 46, marginR = 10, marginT = 8, marginB = 22;
    const QRect plot(marginL, marginT,
                     width()  - marginL - marginR,
                     height() - marginT - marginB);

    if (!m_hasAntenna || m_spectrum.empty()) {
        p.setPen(QColor(255, 255, 255, 60));
        p.setFont(QFont("monospace", 10));
        p.drawText(rect(), Qt::AlignCenter, "Select an antenna to view spectrum");
        return;
    }

    // Horizontal grid lines
    p.setPen(QPen(QColor(255, 255, 255, 18), 1));
    for (int i = 0; i <= 4; ++i) {
        int y = plot.top() + i * plot.height() / 4;
        p.drawLine(plot.left(), y, plot.right(), y);
    }

    // Y-axis dBm labels
    p.setPen(QColor(255, 255, 255, 80));
    p.setFont(QFont("monospace", 8));
    for (int i = 0; i <= 4; ++i) {
        float dbm = kMaxDbm - i * (kMaxDbm - kMinDbm) / 4.0f;
        int   y   = plot.top() + i * plot.height() / 4;
        p.drawText(0, y - 7, marginL - 4, 14,
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(static_cast<int>(dbm)));
    }

    // X-axis frequency labels
    const float fStart = m_spectrum.front().freqMHz;
    const float fEnd   = m_spectrum.back().freqMHz;
    for (int i = 0; i <= 4; ++i) {
        float fMHz = fStart + i * (fEnd - fStart) / 4.0f;
        int   x    = plot.left() + i * plot.width() / 4;
        QString label = (fMHz >= 1000.0f)
            ? QString::number(fMHz / 1000.0f, 'f', 1) + " GHz"
            : QString::number(static_cast<int>(fMHz))  + " MHz";
        p.drawText(x - 30, plot.bottom() + 4, 60, 16, Qt::AlignCenter, label);
    }

    // Coordinate mappers
    auto toX = [&](float fMHz) -> float {
        return plot.left() + (fMHz - fStart) / (fEnd - fStart) * plot.width();
    };
    auto toY = [&](float dbm) -> float {
        float t = (dbm - kMinDbm) / (kMaxDbm - kMinDbm); // 0=bottom, 1=top
        return static_cast<float>(plot.bottom()) - t * plot.height();
    };

    // Filled area under curve
    QPolygonF poly;
    poly << QPointF(toX(fStart), static_cast<float>(plot.bottom()));
    for (const auto& pt : m_spectrum) {
        float y = toY(std::clamp(pt.powerDbm, kMinDbm, kMaxDbm));
        poly << QPointF(toX(pt.freqMHz), y);
    }
    poly << QPointF(toX(fEnd), static_cast<float>(plot.bottom()));

    QLinearGradient grad(0, plot.top(), 0, plot.bottom());
    grad.setColorAt(0.0, QColor(79, 195, 247, 70));
    grad.setColorAt(1.0, QColor(79, 195, 247,  8));
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawPolygon(poly);

    // Spectrum line on top of fill
    QPainterPath path;
    bool first = true;
    for (const auto& pt : m_spectrum) {
        float x = toX(pt.freqMHz);
        float y = toY(std::clamp(pt.powerDbm, kMinDbm, kMaxDbm));
        if (first) { path.moveTo(x, y); first = false; }
        else          path.lineTo(x, y);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(79, 195, 247, 210), 1.5f));
    p.drawPath(path);

    // "dBm" rotated axis label
    p.save();
    p.setPen(QColor(255, 255, 255, 55));
    p.setFont(QFont("monospace", 8));
    p.translate(8, plot.center().y());
    p.rotate(-90);
    p.drawText(-20, -5, 40, 12, Qt::AlignCenter, "dBm");
    p.restore();
}
```

- [ ] **Step 3: Build the main binary to confirm BottomBar compiles**

```powershell
$env:PATH += ";C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1310_64\bin"
cmake --build . -j8 2>&1 | Select-String "error:" | Select-Object -First 20
```

Expected: zero errors. (BottomBar.cpp is picked up via GLOB_RECURSE automatically.)

- [ ] **Step 4: Commit**

```bash
git add src/ui/BottomBar.h src/ui/BottomBar.cpp
git commit -m "feat: add BottomBar spectrum display widget"
```

---

### Task 3: Camera keyboard shortcuts

**Files:**
- Modify: `src/ui/ViewportWidget.h` — add `keyPressEvent`, `m_defaultCamera`
- Modify: `src/ui/ViewportWidget.cpp` — focus policy, keyboard handler, save default camera

No new test file changes needed: `Camera::orbit()` is already covered in the existing suite, and Qt key events require a live event loop to exercise.

- [ ] **Step 1: Replace `src/ui/ViewportWidget.h` with the updated version**

```cpp
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QKeyEvent>
#include <QPoint>
#include "render/Camera.h"
#include "render/TerrainMesh.h"
#include "render/overlays/HeatmapOverlay.h"
#include "render/overlays/LOSRayOverlay.h"
#include "render/overlays/AntennaLobeOverlay.h"
#include "render/overlays/JammerBubbleOverlay.h"
#include "render/overlays/FresnelOverlay.h"
#include "sim/SimEngine.h"

class ViewportWidget : public QOpenGLWidget,
                       protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    enum Overlay { Heatmap = 0, LOS = 1, Lobe = 2, Jammer = 3, Fresnel = 4 };

    explicit ViewportWidget(SimEngine* engine, QWidget* parent = nullptr);
    void refreshOverlays();
    void reloadTerrain();
    void setOverlayVisible(Overlay overlay, bool visible);

protected:
    void initializeGL()                  override;
    void resizeGL(int w, int h)          override;
    void paintGL()                       override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void wheelEvent(QWheelEvent*)        override;
    void keyPressEvent(QKeyEvent*)       override;  // T=top-down, R=reset, arrows=orbit

private:
    SimEngine*          m_engine;
    Camera              m_camera;
    Camera              m_defaultCamera;  // saved at initializeGL/reloadTerrain for R key
    TerrainMesh         m_terrain;
    HeatmapOverlay      m_heatmap;
    LOSRayOverlay       m_losRay;
    AntennaLobeOverlay  m_antennaLobe;
    JammerBubbleOverlay m_jammerBubble;
    FresnelOverlay      m_fresnel;
    QPoint              m_lastMouse;
    bool                m_dragging = false;
};
```

- [ ] **Step 2: Update the constructor in `ViewportWidget.cpp` to request keyboard focus**

Change:

```cpp
ViewportWidget::ViewportWidget(SimEngine* engine, QWidget* parent)
    : QOpenGLWidget(parent), m_engine(engine) {}
```

To:

```cpp
ViewportWidget::ViewportWidget(SimEngine* engine, QWidget* parent)
    : QOpenGLWidget(parent), m_engine(engine)
{
    setFocusPolicy(Qt::StrongFocus);
}
```

- [ ] **Step 3: Save default camera at the end of `initializeGL()`**

The existing `initializeGL()` ends with:

```cpp
    m_camera.target    = { cx, terrain.heightAtWorld(cx, cz), cz };
    m_camera.radius    = terrain.worldSize().x * 0.8f;
    m_camera.elevation = 35.0f;
```

Add one line immediately after:

```cpp
    m_camera.target    = { cx, terrain.heightAtWorld(cx, cz), cz };
    m_camera.radius    = terrain.worldSize().x * 0.8f;
    m_camera.elevation = 35.0f;
    m_defaultCamera    = m_camera;   // ← save for R key
```

- [ ] **Step 4: Save default camera at the end of `reloadTerrain()`**

The existing `reloadTerrain()` ends with:

```cpp
    m_camera.elevation = 35.0f;
    doneCurrent();
    update();
```

Add one line between the elevation assignment and `doneCurrent()`:

```cpp
    m_camera.elevation = 35.0f;
    m_defaultCamera    = m_camera;   // ← save for R key
    doneCurrent();
    update();
```

- [ ] **Step 5: Add `keyPressEvent` at the end of `ViewportWidget.cpp`**

```cpp
void ViewportWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_T:
            m_camera.elevation = 89.0f;    // top-down
            break;
        case Qt::Key_R:
            m_camera = m_defaultCamera;    // reset to saved default
            break;
        case Qt::Key_Left:
            m_camera.orbit(-5.0f, 0.0f);
            break;
        case Qt::Key_Right:
            m_camera.orbit( 5.0f, 0.0f);
            break;
        case Qt::Key_Up:
            m_camera.orbit(0.0f, -5.0f);
            break;
        case Qt::Key_Down:
            m_camera.orbit(0.0f,  5.0f);
            break;
        default:
            QOpenGLWidget::keyPressEvent(e);
            return;
    }
    update();
}
```

- [ ] **Step 6: Build and smoke-test**

```powershell
cmake --build . -j8 2>&1 | Select-String "error:" | Select-Object -First 10
.\ridgeline.exe
```

Click once in the 3D viewport to give it keyboard focus, then verify:
- **T** → view snaps to top-down (looking straight down at the terrain)
- **R** → returns to 35° elevation / 45° azimuth
- **← →** → azimuth rotates (scene spins left/right)
- **↑ ↓** → elevation tilts

- [ ] **Step 7: Commit**

```bash
git add src/ui/ViewportWidget.h src/ui/ViewportWidget.cpp
git commit -m "feat: camera keyboard shortcuts — T top-down, R reset, arrows orbit"
```

---

### Task 4: Wire BottomBar into MainWindow

**Files:**
- Modify: `src/ui/MainWindow.h` — add `BottomBar* m_bottomBar`
- Modify: `src/ui/MainWindow.cpp` — bottom dock, `onAntennaSelected`, `onAntennaChanged`

- [ ] **Step 1: Replace `src/ui/MainWindow.h` with the updated version**

```cpp
#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QDockWidget>
#include "sim/SimEngine.h"
#include "ViewportWidget.h"
#include "ScenarioTree.h"
#include "PropertiesPanel.h"
#include "OverlayToolbar.h"
#include "BottomBar.h"                // ← add
#include "data/AntennaLibrary.h"
#include "data/ScenarioIO.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void loadTerrain(const QString& path);

private slots:
    void onSolve();
    void onLoadTerrain();
    void onSaveScenario();
    void onOpenScenario();
    void onExportPng();
    void onExportCsv();
    void onAddAntenna();
    void onRemoveAntenna(int index);
    void onAntennaSelected(int index);
    void onAntennaChanged(Antenna updated);

private:
    SimEngine        m_engine;
    AntennaLibrary   m_antennaLib;
    TerrainSource    m_terrainSource;
    ViewportWidget*  m_viewport      = nullptr;
    ScenarioTree*    m_scenarioTree  = nullptr;
    PropertiesPanel* m_propsPanel    = nullptr;
    OverlayToolbar*  m_overlayBar    = nullptr;
    BottomBar*       m_bottomBar     = nullptr;  // ← add
    QLabel*          m_statusLabel   = nullptr;
    QPushButton*     m_solveBtn      = nullptr;
    QComboBox*       m_modeCombo     = nullptr;
    int              m_selectedIndex = -1;

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
    void refreshTree();
    void refreshPropsPanel();
    void applyScenarioDoc(const ScenarioDoc& doc);
};
```

- [ ] **Step 2: Add the bottom dock to `MainWindow::MainWindow()` in `MainWindow.cpp`**

Find the status bar setup block (the lines ending with `statusBar()->setStyleSheet(...)`):

```cpp
    m_statusLabel = new QLabel("  Ready");
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace;");
    statusBar()->addWidget(m_statusLabel);
    statusBar()->setStyleSheet("background: rgba(15,25,35,0.9);");
```

Add the bottom dock immediately after those four lines:

```cpp
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
```

- [ ] **Step 3: Update `onAntennaSelected` to refresh the spectrum**

Current implementation (around line 263):

```cpp
void MainWindow::onAntennaSelected(int index) {
    m_selectedIndex = index;
    refreshPropsPanel();
}
```

Replace with:

```cpp
void MainWindow::onAntennaSelected(int index) {
    m_selectedIndex = index;
    refreshPropsPanel();

    const auto& antennas = m_engine.antennas();
    if (index >= 0 && index < static_cast<int>(antennas.size()))
        m_bottomBar->setAntenna(antennas[index]);
    else
        m_bottomBar->clearAntenna();
}
```

- [ ] **Step 4: Update `onAntennaChanged` to refresh the spectrum**

Current implementation (around line 268):

```cpp
void MainWindow::onAntennaChanged(Antenna updated) {
    if (m_selectedIndex < 0) return;
    m_engine.updateAntenna(m_selectedIndex, updated);
}
```

Replace with:

```cpp
void MainWindow::onAntennaChanged(Antenna updated) {
    if (m_selectedIndex < 0) return;
    m_engine.updateAntenna(m_selectedIndex, updated);
    m_bottomBar->setAntenna(updated);
}
```

- [ ] **Step 5: Build and run**

```powershell
cmake --build . -j8 2>&1 | Select-String "error:" | Select-Object -First 10
.\ridgeline.exe
```

Verify:
1. A "Spectrum" dock appears at the bottom showing "Select an antenna to view spectrum"
2. Click an antenna in the Scenario Tree → spectrum chart appears showing a downward-sloping curve (30 MHz left, 3 GHz right)
3. Change Power or Gain in the Properties Panel → chart updates immediately
4. Click away to deselect → placeholder text returns

- [ ] **Step 6: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: wire BottomBar into MainWindow — spectrum updates on select/change"
```

---

## Self-Review

**Spec coverage:**
- BottomBar spectrum display → Tasks 1 (SpectrumCalc), 2 (widget), 4 (wiring) ✓
- Camera keyboard shortcuts → Task 3 ✓

**Placeholder scan:** No TBDs, no vague steps. All code blocks are complete.

**Type consistency:**
- `sweepSpectrum()` signature: defined Task 1, consumed Task 2 ✓
- `SpectrumPoint` struct: defined Task 1 header, used in Task 2 header via `#include "sim/SpectrumCalc.h"` ✓
- `BottomBar::setAntenna(const Antenna&)` / `clearAntenna()`: defined Task 2, called Task 4 ✓
- `m_defaultCamera` Camera field: declared Task 3 header, assigned Task 3 cpp ✓
- `m_bottomBar` BottomBar* field: declared Task 4 header, constructed Task 4 cpp ✓
