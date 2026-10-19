# Phase 4 — UI Panels Implementation Plan


**Goal:** Add interactive side panels — a scenario tree (left dock) listing all antennas, a properties panel (right dock) for editing selected antenna parameters, and an overlay toolbar for toggling each of the five render overlays on/off.

**Architecture:** Three new Qt widgets (ScenarioTree, PropertiesPanel, OverlayToolbar) are added as QDockWidgets / toolbar widgets. SimEngine gains three fine-grained antenna mutators (add/remove/update) instead of replacing the whole vector. ViewportWidget gains `setOverlayVisible()` so the toolbar can toggle overlays without touching overlay internals. MainWindow wires all signals together and owns the selection state (selected antenna index).

**Tech Stack:** C++17, Qt 6.8 (QDockWidget, QListWidget, QDoubleSpinBox, QCheckBox, QGroupBox), GLM, existing SimEngine / ViewportWidget / Antenna / FieldGrid types.

---

## Codebase Quick-Reference

```
src/sim/Types.h          — Antenna struct (position, frequencyMHz, powerW, gainDbi, isJammer)
src/sim/SimEngine.h/.cpp — owns m_antennas vector; setAntennas() replaces whole list
src/ui/MainWindow.h/.cpp — creates engine, viewport, toolbar; calls placeDefaultAntennas()
src/ui/ViewportWidget.h/.cpp — QOpenGLWidget; five overlay members each with bool visible
src/render/overlays/     — HeatmapOverlay, LOSRayOverlay, AntennaLobeOverlay,
                           JammerBubbleOverlay, FresnelOverlay — all have bool visible = true
tests/test_main.cpp      — CTest suite, no Qt/GL, uses SimEngine + TerrainModel
CMakeLists.txt           — ridgeline_tests target lists .cpp sources explicitly
```

Overlay draw order in `ViewportWidget::paintGL()`:
`m_terrain → m_heatmap → m_losRay → m_antennaLobe → m_jammerBubble → m_fresnel`

Each overlay already has `bool visible = true` (public). `paintGL` currently calls `draw()` unconditionally.

Build commands (run from `build/` with cmake + mingw in PATH):
```
cmake --build . -j8
ctest --output-on-failure
```

---

## File Map

| Action | File | Purpose |
|--------|------|---------|
| Modify | `src/sim/SimEngine.h` | Add `addAntenna`, `removeAntenna`, `updateAntenna` declarations |
| Modify | `src/sim/SimEngine.cpp` | Implement the three mutators |
| Modify | `CMakeLists.txt` | Add `FastSolver.cpp` + `SimEngine.cpp` to `ridgeline_tests` |
| Modify | `tests/test_main.cpp` | Tests for the three new mutators |
| Modify | `src/ui/ViewportWidget.h` | Add `Overlay` enum + `setOverlayVisible()` declaration |
| Modify | `src/ui/ViewportWidget.cpp` | Implement `setOverlayVisible()`; guard draws with `visible` flag |
| Create | `src/ui/OverlayToolbar.h` | Five QCheckBox toggles; emits `overlayToggled(int, bool)` |
| Create | `src/ui/OverlayToolbar.cpp` | Implementation |
| Create | `src/ui/ScenarioTree.h` | QListWidget + Add/Remove buttons; emits selection + request signals |
| Create | `src/ui/ScenarioTree.cpp` | Implementation |
| Create | `src/ui/PropertiesPanel.h` | QDoubleSpinBox group for antenna params; emits `antennaChanged(Antenna)` |
| Create | `src/ui/PropertiesPanel.cpp` | Implementation |
| Modify | `src/ui/MainWindow.h` | Add dock + panel members, selection index |
| Modify | `src/ui/MainWindow.cpp` | Wire all signals, create docks, handle add/remove/update flow |

---

## Task 1 — SimEngine Antenna Mutators

**Files:**
- Modify: `src/sim/SimEngine.h`
- Modify: `src/sim/SimEngine.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_main.cpp`

### Why
The UI needs to add, remove, and update individual antennas without replacing the whole vector — both to trigger targeted viewport refreshes and to keep the selected index stable after edits.

---

- [ ] **Step 1: Add declarations to SimEngine.h**

Open `src/sim/SimEngine.h`. After the `setAntennas` line add:

```cpp
    void addAntenna(Antenna antenna);
    void removeAntenna(int index);   // no-op if index out of range
    void updateAntenna(int index, Antenna antenna);  // no-op if index out of range
```

Full updated public section (replace the existing one):

```cpp
public:
    enum class Mode { Fast, Full };

    Mode mode = Mode::Fast;

    void setTerrain(TerrainModel terrain);
    void setAntennas(std::vector<Antenna> antennas);
    void addAntenna(Antenna antenna);
    void removeAntenna(int index);
    void updateAntenna(int index, Antenna antenna);

    void loadMaterials(const std::string& path);
    void solve();

    const FieldGrid&            fieldGrid() const { return m_field; }
    const TerrainModel&         terrain()   const { return m_terrain; }
    const std::vector<Antenna>& antennas()  const { return m_antennas; }

    std::function<void()> onFieldUpdated;
```

- [ ] **Step 2: Implement mutators in SimEngine.cpp**

Append to the end of `src/sim/SimEngine.cpp`:

```cpp
void SimEngine::addAntenna(Antenna antenna) {
    m_antennas.push_back(std::move(antenna));
}

void SimEngine::removeAntenna(int index) {
    if (index < 0 || index >= static_cast<int>(m_antennas.size())) return;
    m_antennas.erase(m_antennas.begin() + index);
}

void SimEngine::updateAntenna(int index, Antenna antenna) {
    if (index < 0 || index >= static_cast<int>(m_antennas.size())) return;
    m_antennas[index] = std::move(antenna);
}
```

- [ ] **Step 3: Expand ridgeline_tests in CMakeLists.txt**

The tests will call `SimEngine::solve()` which needs `FastSolver.cpp` and `SimEngine.cpp`. Add them to the test target.

Find this block in `CMakeLists.txt`:

```cmake
add_executable(ridgeline_tests
    tests/test_main.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/FullSolver.cpp
)
```

Replace with:

```cmake
add_executable(ridgeline_tests
    tests/test_main.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/FastSolver.cpp
    src/sim/FullSolver.cpp
    src/sim/SimEngine.cpp
)
```

- [ ] **Step 4: Write tests for the mutators in test_main.cpp**

Add these three test functions after the existing FullSolver tests and before `main()`:

```cpp
// ── SimEngine mutator tests ──────────────────────────────────────────────────

static void test_add_antenna() {
    SimEngine engine;
    CHECK(engine.antennas().empty());

    Antenna a;
    a.frequencyMHz = 433.0f;
    a.powerW = 10.0f;
    engine.addAntenna(a);

    CHECK(engine.antennas().size() == 1u);
    CHECK(engine.antennas()[0].frequencyMHz == 433.0f);
}

static void test_remove_antenna() {
    SimEngine engine;
    Antenna a1, a2;
    a1.frequencyMHz = 100.0f;
    a2.frequencyMHz = 200.0f;
    engine.addAntenna(a1);
    engine.addAntenna(a2);

    engine.removeAntenna(0);

    CHECK(engine.antennas().size() == 1u);
    CHECK(engine.antennas()[0].frequencyMHz == 200.0f);

    // Out-of-range is a no-op
    engine.removeAntenna(99);
    CHECK(engine.antennas().size() == 1u);
}

static void test_update_antenna() {
    SimEngine engine;
    Antenna a;
    a.frequencyMHz = 150.0f;
    engine.addAntenna(a);

    Antenna updated = engine.antennas()[0];
    updated.frequencyMHz = 900.0f;
    updated.gainDbi = 12.0f;
    engine.updateAntenna(0, updated);

    CHECK(engine.antennas()[0].frequencyMHz == 900.0f);
    CHECK(engine.antennas()[0].gainDbi == 12.0f);

    // Out-of-range is a no-op
    engine.updateAntenna(99, updated);
    CHECK(engine.antennas().size() == 1u);
}
```

Add calls in `main()`:

```cpp
    test_add_antenna();
    test_remove_antenna();
    test_update_antenna();
```

`test_main.cpp` needs `SimEngine.h` — add this include near the top if not already present:

```cpp
#include "sim/SimEngine.h"
```

- [ ] **Step 5: Build and run tests**

```
cmake --build . -j8
ctest --output-on-failure
```

Expected: `100% tests passed` (now 32 assertions).

- [ ] **Step 6: Commit**

```bash
git add src/sim/SimEngine.h src/sim/SimEngine.cpp CMakeLists.txt tests/test_main.cpp
git commit -m "feat: add SimEngine antenna mutators (add/remove/update) with tests"
```

---

## Task 2 — ViewportWidget Overlay Visibility API

**Files:**
- Modify: `src/ui/ViewportWidget.h`
- Modify: `src/ui/ViewportWidget.cpp`

### Why
The OverlayToolbar needs a clean way to toggle each overlay. Rather than exposing the overlay members directly, ViewportWidget gets a typed `setOverlayVisible(Overlay, bool)` method that sets the flag and triggers a repaint.

---

- [ ] **Step 1: Add Overlay enum and declaration to ViewportWidget.h**

Add inside the `ViewportWidget` class (after `Q_OBJECT`, before `public:`):

```cpp
    enum Overlay { Heatmap = 0, LOS = 1, Lobe = 2, Jammer = 3, Fresnel = 4 };
```

Add to the `public:` section after `reloadTerrain()`:

```cpp
    void setOverlayVisible(Overlay overlay, bool visible);
```

Full updated class declaration:

```cpp
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
    void initializeGL()              override;
    void resizeGL(int w, int h)      override;
    void paintGL()                   override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void wheelEvent(QWheelEvent*)        override;

private:
    SimEngine*    m_engine;
    Camera        m_camera;
    TerrainMesh        m_terrain;
    HeatmapOverlay     m_heatmap;
    LOSRayOverlay      m_losRay;
    AntennaLobeOverlay m_antennaLobe;
    JammerBubbleOverlay m_jammerBubble;
    FresnelOverlay     m_fresnel;
    QPoint             m_lastMouse;
    bool          m_dragging = false;
};
```

- [ ] **Step 2: Implement setOverlayVisible in ViewportWidget.cpp**

Append to the end of `src/ui/ViewportWidget.cpp`:

```cpp
void ViewportWidget::setOverlayVisible(Overlay overlay, bool visible) {
    switch (overlay) {
        case Heatmap: m_heatmap.visible     = visible; break;
        case LOS:     m_losRay.visible      = visible; break;
        case Lobe:    m_antennaLobe.visible = visible; break;
        case Jammer:  m_jammerBubble.visible= visible; break;
        case Fresnel: m_fresnel.visible     = visible; break;
    }
    update();
}
```

- [ ] **Step 3: Guard each overlay draw call with its visible flag in paintGL**

Replace the current `paintGL` in `src/ui/ViewportWidget.cpp`:

```cpp
void ViewportWidget::paintGL() {
    if (height() == 0) return;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    float aspect = static_cast<float>(width()) / height();
    glm::mat4 mvp = m_camera.mvp(aspect);

    m_terrain.draw(mvp);
    if (m_heatmap.visible)      m_heatmap.draw(mvp);
    if (m_losRay.visible)       m_losRay.draw(mvp);
    if (m_antennaLobe.visible)  m_antennaLobe.draw(mvp);
    if (m_jammerBubble.visible) m_jammerBubble.draw(mvp);
    if (m_fresnel.visible)      m_fresnel.draw(mvp);
}
```

- [ ] **Step 4: Build (no tests for pure GL code — visual confirmation later)**

```
cmake --build . -j8
```

Expected: zero errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ViewportWidget.h src/ui/ViewportWidget.cpp
git commit -m "feat: add ViewportWidget::setOverlayVisible() and guard draws with visible flag"
```

---

## Task 3 — OverlayToolbar Widget

**Files:**
- Create: `src/ui/OverlayToolbar.h`
- Create: `src/ui/OverlayToolbar.cpp`

### Why
A second toolbar row with five checkboxes gives the user one-click toggle for each overlay without needing to open a menu.

---

- [ ] **Step 1: Create src/ui/OverlayToolbar.h**

```cpp
#pragma once
#include <QWidget>
#include <QCheckBox>
#include "ViewportWidget.h"

// A row of five checkboxes that toggle the five render overlays.
// Connect overlayToggled() to ViewportWidget::setOverlayVisible().
class OverlayToolbar : public QWidget {
    Q_OBJECT
public:
    explicit OverlayToolbar(QWidget* parent = nullptr);

signals:
    void overlayToggled(ViewportWidget::Overlay overlay, bool visible);

private:
    void buildCheckbox(const QString& label, ViewportWidget::Overlay id,
                       QBoxLayout* layout);
};
```

- [ ] **Step 2: Create src/ui/OverlayToolbar.cpp**

```cpp
#include "OverlayToolbar.h"
#include <QHBoxLayout>
#include <QLabel>

OverlayToolbar::OverlayToolbar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(12);

    auto* overlaysLabel = new QLabel("Overlays:");
    overlaysLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace; font-size: 11px;");
    layout->addWidget(overlaysLabel);

    buildCheckbox("Heatmap",  ViewportWidget::Heatmap, layout);
    buildCheckbox("LOS Rays", ViewportWidget::LOS,     layout);
    buildCheckbox("Lobe",     ViewportWidget::Lobe,    layout);
    buildCheckbox("Jammer",   ViewportWidget::Jammer,  layout);
    buildCheckbox("Fresnel",  ViewportWidget::Fresnel, layout);

    layout->addStretch();
    setStyleSheet("background: rgba(15,25,35,0.6); border-bottom: 1px solid rgba(255,255,255,0.06);");
}

void OverlayToolbar::buildCheckbox(const QString& label,
                                   ViewportWidget::Overlay id,
                                   QBoxLayout* layout) {
    auto* cb = new QCheckBox(label);
    cb->setChecked(true);
    cb->setStyleSheet(
        "QCheckBox { color: rgba(255,255,255,0.75); font-family: monospace; font-size: 11px; }"
        "QCheckBox::indicator { width: 13px; height: 13px; }"
        "QCheckBox::indicator:checked { background: #4fc3f7; border: 1px solid #4fc3f7; border-radius: 2px; }"
        "QCheckBox::indicator:unchecked { background: transparent; border: 1px solid rgba(255,255,255,0.3); border-radius: 2px; }"
    );
    connect(cb, &QCheckBox::toggled, this, [this, id](bool checked) {
        emit overlayToggled(id, checked);
    });
    layout->addWidget(cb);
}
```

- [ ] **Step 3: Build**

```
cmake --build . -j8
```

Expected: zero errors. (OverlayToolbar isn't wired into MainWindow yet — that's Task 6.)

- [ ] **Step 4: Commit**

```bash
git add src/ui/OverlayToolbar.h src/ui/OverlayToolbar.cpp
git commit -m "feat: add OverlayToolbar widget with five overlay toggle checkboxes"
```

---

## Task 4 — ScenarioTree Widget

**Files:**
- Create: `src/ui/ScenarioTree.h`
- Create: `src/ui/ScenarioTree.cpp`

### Why
The scenario tree gives the user a list of all placed antennas and jammers so they can select one for editing or remove it. It does not own antenna data — it receives the full list from MainWindow via `refresh()` whenever the list changes.

---

- [ ] **Step 1: Create src/ui/ScenarioTree.h**

```cpp
#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <vector>
#include "sim/Types.h"

// Left-dock panel that lists all antennas in the scene.
// MainWindow calls refresh() whenever the antenna list changes.
// Signals propagate user actions back to MainWindow.
class ScenarioTree : public QWidget {
    Q_OBJECT
public:
    explicit ScenarioTree(QWidget* parent = nullptr);

    // Rebuild the list from the current antenna vector.
    // Tries to preserve the selected row; clears selection if list shrinks.
    void refresh(const std::vector<Antenna>& antennas);

    // Returns the currently selected antenna index, or -1 if none.
    int selectedIndex() const;

signals:
    void selectionChanged(int index);   // -1 = deselected
    void addRequested();                // user clicked Add
    void removeRequested(int index);    // user clicked Remove; index is valid

private:
    QListWidget* m_list   = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;

    void onSelectionChanged();
    void onRemoveClicked();
};
```

- [ ] **Step 2: Create src/ui/ScenarioTree.cpp**

```cpp
#include "ScenarioTree.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

ScenarioTree::ScenarioTree(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* title = new QLabel("Scenario");
    title->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace; font-size: 11px; padding: 2px 4px;");
    layout->addWidget(title);

    m_list = new QListWidget();
    m_list->setStyleSheet(
        "QListWidget {"
        "  background: rgba(0,0,0,0.25);"
        "  border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 4px;"
        "  color: rgba(255,255,255,0.85);"
        "  font-family: monospace;"
        "  font-size: 12px;"
        "}"
        "QListWidget::item:selected {"
        "  background: rgba(79,195,247,0.25);"
        "  color: #4fc3f7;"
        "}"
        "QListWidget::item:hover {"
        "  background: rgba(255,255,255,0.05);"
        "}"
    );
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &ScenarioTree::onSelectionChanged);
    layout->addWidget(m_list);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);

    const QString btnStyle =
        "QPushButton {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 4px;"
        "  color: rgba(255,255,255,0.75);"
        "  font-family: monospace;"
        "  font-size: 12px;"
        "  padding: 3px 10px;"
        "}"
        "QPushButton:hover { background: rgba(255,255,255,0.15); }";

    m_addBtn = new QPushButton("+ Add");
    m_addBtn->setStyleSheet(btnStyle);
    connect(m_addBtn, &QPushButton::clicked, this, &ScenarioTree::addRequested);
    btnRow->addWidget(m_addBtn);

    m_removeBtn = new QPushButton("− Remove");
    m_removeBtn->setStyleSheet(btnStyle);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &ScenarioTree::onRemoveClicked);
    btnRow->addWidget(m_removeBtn);

    layout->addLayout(btnRow);

    setMinimumWidth(160);
    setMaximumWidth(260);
    setStyleSheet("background: rgba(15,25,35,0.85);");
}

void ScenarioTree::refresh(const std::vector<Antenna>& antennas) {
    const int prevRow = m_list->currentRow();
    m_list->blockSignals(true);
    m_list->clear();

    int antennaIdx = 1, jammerIdx = 1;
    for (const auto& a : antennas) {
        const QString label = a.isJammer
            ? QString("🔴 Jammer %1").arg(jammerIdx++)
            : QString("📡 Antenna %1").arg(antennaIdx++);
        m_list->addItem(label);
    }

    // Restore selection if still valid.
    if (prevRow >= 0 && prevRow < m_list->count())
        m_list->setCurrentRow(prevRow);

    m_list->blockSignals(false);
    m_removeBtn->setEnabled(m_list->currentRow() >= 0);
}

int ScenarioTree::selectedIndex() const {
    return m_list->currentRow();
}

void ScenarioTree::onSelectionChanged() {
    const int row = m_list->currentRow();
    m_removeBtn->setEnabled(row >= 0);
    emit selectionChanged(row);
}

void ScenarioTree::onRemoveClicked() {
    const int row = m_list->currentRow();
    if (row >= 0) emit removeRequested(row);
}
```

- [ ] **Step 3: Build**

```
cmake --build . -j8
```

Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ScenarioTree.h src/ui/ScenarioTree.cpp
git commit -m "feat: add ScenarioTree left-dock panel with antenna list and add/remove buttons"
```

---

## Task 5 — PropertiesPanel Widget

**Files:**
- Create: `src/ui/PropertiesPanel.h`
- Create: `src/ui/PropertiesPanel.cpp`

### Why
Selecting an antenna in the ScenarioTree should let the user edit its parameters immediately. PropertiesPanel shows spinboxes for frequency, power, gain, height (position.y), and a checkbox for isJammer. It emits `antennaChanged(Antenna)` whenever a value changes.

---

- [ ] **Step 1: Create src/ui/PropertiesPanel.h**

```cpp
#pragma once
#include <QWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include "sim/Types.h"

// Right-dock panel for editing a selected antenna's parameters.
// MainWindow calls load() when selection changes.
// When the user edits a field, antennaChanged() fires with the full updated Antenna.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    // Populate controls from the given antenna.  Pass nullptr to clear/disable.
    void load(const Antenna* antenna);

signals:
    void antennaChanged(Antenna updated);

private slots:
    void onValueChanged();

private:
    QLabel*         m_noSelLabel  = nullptr;
    QWidget*        m_form        = nullptr;
    QDoubleSpinBox* m_freqSpin    = nullptr;
    QDoubleSpinBox* m_powerSpin   = nullptr;
    QDoubleSpinBox* m_gainSpin    = nullptr;
    QDoubleSpinBox* m_heightSpin  = nullptr;
    QCheckBox*      m_jammerCheck = nullptr;

    bool m_loading = false;   // suppress signals while loading values
    Antenna m_current;        // last loaded antenna (position preserved from here)

    QDoubleSpinBox* makeSpinBox(double min, double max, double step,
                                const QString& suffix);
};
```

- [ ] **Step 2: Create src/ui/PropertiesPanel.cpp**

```cpp
#include "PropertiesPanel.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QStackedWidget>

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(4);

    auto* title = new QLabel("Properties");
    title->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace; font-size: 11px; padding: 2px 4px;");
    outerLayout->addWidget(title);

    // "Nothing selected" placeholder
    m_noSelLabel = new QLabel("No antenna selected.");
    m_noSelLabel->setAlignment(Qt::AlignCenter);
    m_noSelLabel->setStyleSheet("color: rgba(255,255,255,0.3); font-family: monospace; font-size: 11px;");
    outerLayout->addWidget(m_noSelLabel);

    // Actual form (hidden when nothing is selected)
    m_form = new QWidget();
    auto* form = new QFormLayout(m_form);
    form->setContentsMargins(4, 4, 4, 4);
    form->setSpacing(6);
    form->setLabelAlignment(Qt::AlignRight);

    const QString labelStyle = "color: rgba(255,255,255,0.6); font-family: monospace; font-size: 11px;";

    m_freqSpin = makeSpinBox(1.0, 6000.0, 1.0, " MHz");
    m_powerSpin = makeSpinBox(0.001, 100000.0, 1.0, " W");
    m_gainSpin = makeSpinBox(0.0, 40.0, 0.5, " dBi");
    m_heightSpin = makeSpinBox(0.0, 3000.0, 1.0, " m");

    auto* freqLabel  = new QLabel("Frequency:");  freqLabel->setStyleSheet(labelStyle);
    auto* powerLabel = new QLabel("Power:");      powerLabel->setStyleSheet(labelStyle);
    auto* gainLabel  = new QLabel("Gain:");       gainLabel->setStyleSheet(labelStyle);
    auto* heightLabel= new QLabel("Height:");     heightLabel->setStyleSheet(labelStyle);
    auto* jammerLabel= new QLabel("Is Jammer:");  jammerLabel->setStyleSheet(labelStyle);

    form->addRow(freqLabel,   m_freqSpin);
    form->addRow(powerLabel,  m_powerSpin);
    form->addRow(gainLabel,   m_gainSpin);
    form->addRow(heightLabel, m_heightSpin);

    m_jammerCheck = new QCheckBox();
    m_jammerCheck->setStyleSheet(
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:checked { background: #ef5350; border: 1px solid #ef5350; border-radius: 2px; }"
        "QCheckBox::indicator:unchecked { background: transparent; border: 1px solid rgba(255,255,255,0.3); border-radius: 2px; }"
    );
    form->addRow(jammerLabel, m_jammerCheck);

    outerLayout->addWidget(m_form);
    outerLayout->addStretch();

    m_form->hide();

    // Wire all controls to onValueChanged
    connect(m_freqSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_powerSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_gainSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_heightSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_jammerCheck, &QCheckBox::toggled,
            this, &PropertiesPanel::onValueChanged);

    setMinimumWidth(180);
    setMaximumWidth(280);
    setStyleSheet("background: rgba(15,25,35,0.85);");
}

void PropertiesPanel::load(const Antenna* antenna) {
    if (!antenna) {
        m_form->hide();
        m_noSelLabel->show();
        return;
    }
    m_current = *antenna;
    m_loading = true;
    m_freqSpin->setValue(static_cast<double>(antenna->frequencyMHz));
    m_powerSpin->setValue(static_cast<double>(antenna->powerW));
    m_gainSpin->setValue(static_cast<double>(antenna->gainDbi));
    m_heightSpin->setValue(static_cast<double>(antenna->position.y));
    m_jammerCheck->setChecked(antenna->isJammer);
    m_loading = false;
    m_noSelLabel->hide();
    m_form->show();
}

void PropertiesPanel::onValueChanged() {
    if (m_loading) return;
    m_current.frequencyMHz = static_cast<float>(m_freqSpin->value());
    m_current.powerW       = static_cast<float>(m_powerSpin->value());
    m_current.gainDbi      = static_cast<float>(m_gainSpin->value());
    m_current.position.y   = static_cast<float>(m_heightSpin->value());
    m_current.isJammer     = m_jammerCheck->isChecked();
    emit antennaChanged(m_current);
}

QDoubleSpinBox* PropertiesPanel::makeSpinBox(double min, double max,
                                              double step, const QString& suffix) {
    auto* sb = new QDoubleSpinBox();
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setSuffix(suffix);
    sb->setDecimals(suffix == " MHz" || suffix == " m" ? 1 : 3);
    sb->setStyleSheet(
        "QDoubleSpinBox {"
        "  background: rgba(0,0,0,0.3);"
        "  border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 3px;"
        "  color: rgba(255,255,255,0.85);"
        "  font-family: monospace;"
        "  font-size: 11px;"
        "  padding: 2px 4px;"
        "}"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  background: rgba(255,255,255,0.05);"
        "  border: none;"
        "}"
    );
    return sb;
}
```

- [ ] **Step 3: Build**

```
cmake --build . -j8
```

Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add src/ui/PropertiesPanel.h src/ui/PropertiesPanel.cpp
git commit -m "feat: add PropertiesPanel right-dock widget for editing antenna parameters"
```

---

## Task 6 — Wire MainWindow

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

### Why
MainWindow is the coordinator. It creates QDockWidgets for ScenarioTree and PropertiesPanel, adds OverlayToolbar as a second toolbar row, wires all signals together, and handles the Add-antenna flow (placing the new antenna at terrain center).

---

- [ ] **Step 1: Rewrite MainWindow.h**

```cpp
#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QDockWidget>
#include "sim/SimEngine.h"
#include "ui/ViewportWidget.h"
#include "ui/ScenarioTree.h"
#include "ui/PropertiesPanel.h"
#include "ui/OverlayToolbar.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void loadTerrain(const QString& path);

private slots:
    void onSolve();
    void onLoadTerrain();
    void onAddAntenna();
    void onRemoveAntenna(int index);
    void onAntennaSelected(int index);
    void onAntennaChanged(Antenna updated);

private:
    SimEngine        m_engine;
    ViewportWidget*  m_viewport      = nullptr;
    ScenarioTree*    m_scenarioTree  = nullptr;
    PropertiesPanel* m_propsPanel    = nullptr;
    OverlayToolbar*  m_overlayBar    = nullptr;
    QLabel*          m_statusLabel   = nullptr;
    QPushButton*     m_solveBtn      = nullptr;
    QComboBox*       m_modeCombo     = nullptr;
    int              m_selectedIndex = -1;

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
    void refreshTree();          // rebuild ScenarioTree list from engine antennas
    void refreshPropsPanel();    // push selected antenna (or nullptr) into PropertiesPanel
};
```

- [ ] **Step 2: Rewrite MainWindow.cpp**

Replace the entire file content:

```cpp
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

    // Populate tree with default antennas
    refreshTree();
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

        const QString ext = QFileInfo(path).suffix().toLower();
        TerrainModel t = (ext == "hgt")
                       ? TerrainLoader::fromHGT(path.toStdString())
                       : TerrainLoader::fromDTED(path.toStdString());
        m_engine.setTerrain(std::move(t));
        placeDefaultAntennas();

        if (m_viewport) m_viewport->reloadTerrain();

        m_selectedIndex = -1;
        refreshTree();
        refreshPropsPanel();

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
        "Terrain files (*.dt0 *.dt1 *.dt2 *.hgt);;"
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
    refreshTree();
    // Select the newly added antenna
    m_selectedIndex = static_cast<int>(m_engine.antennas().size()) - 1;
    m_scenarioTree->refresh(m_engine.antennas());  // re-refresh to apply selection
    refreshPropsPanel();
    m_statusLabel->setText("  Antenna added");
}

void MainWindow::onRemoveAntenna(int index) {
    m_engine.removeAntenna(index);
    if (m_selectedIndex >= static_cast<int>(m_engine.antennas().size()))
        m_selectedIndex = static_cast<int>(m_engine.antennas().size()) - 1;
    refreshTree();
    refreshPropsPanel();
    m_statusLabel->setText("  Antenna removed");
}

void MainWindow::onAntennaSelected(int index) {
    m_selectedIndex = index;
    refreshPropsPanel();
}

void MainWindow::onAntennaChanged(Antenna updated) {
    if (m_selectedIndex < 0) return;
    m_engine.updateAntenna(m_selectedIndex, updated);
    // Don't refresh tree (label doesn't change on param edit) or re-solve automatically.
    // User clicks Solve when ready.
}

void MainWindow::refreshTree() {
    m_scenarioTree->refresh(m_engine.antennas());
}

void MainWindow::refreshPropsPanel() {
    if (m_selectedIndex >= 0 &&
        m_selectedIndex < static_cast<int>(m_engine.antennas().size())) {
        m_propsPanel->load(&m_engine.antennas()[m_selectedIndex]);
    } else {
        m_propsPanel->load(nullptr);
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
```

- [ ] **Step 3: Build and run tests**

```
cmake --build . -j8
ctest --output-on-failure
```

Expected: zero build errors, `100% tests passed`.

- [ ] **Step 4: Smoke test the UI**

Run `.\ridgeline.exe` from the build directory:
- Left dock shows "📡 Antenna 1" and "📡 Antenna 2"
- Clicking "📡 Antenna 1" shows its params in the right panel (150 MHz, 50 W, 3 dBi)
- Changing frequency in the right panel updates the antenna (click Solve to see the effect)
- Clicking "+ Add" adds "📡 Antenna 3" to the list
- Selecting it and clicking "− Remove" removes it
- Unchecking "Heatmap" in the overlay toolbar makes the heatmap disappear after Solve

- [ ] **Step 5: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: wire ScenarioTree, PropertiesPanel, OverlayToolbar into MainWindow"
```

---

## Self-Review

**Spec coverage:**
- ✅ ScenarioTree (left panel) — Task 4
- ✅ PropertiesPanel (right panel) with freq, power, gain, height, isJammer — Task 5
- ✅ Overlay toggles for all 5 overlays — Tasks 2 + 3
- ✅ Add/Remove antenna — Tasks 1 + 4 + 6
- ✅ Selection drives property editing — Tasks 4, 5, 6
- ⬜ Timeline/spectrum bottom bar — deferred to Phase 5 (it requires a sim history concept that doesn't exist yet)

**Placeholder scan:** None found.

**Type consistency:**
- `Antenna` used uniformly throughout (from `src/sim/Types.h`)
- `ViewportWidget::Overlay` enum defined in Task 2, used in Task 3 and Task 6
- `ScenarioTree::refresh()` called `refreshTree()` in MainWindow (wrapper, not a naming inconsistency)
- `PropertiesPanel::load(const Antenna*)` — nullable pointer for "no selection" state — used consistently

All tasks consistent and complete.
