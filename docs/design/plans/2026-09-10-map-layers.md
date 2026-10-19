# Map Layers Implementation Plan


**Goal:** Replace Ridgeline's fixed overlay checkboxes with an ordered layer stack, and add two layers to it: hillshade derived from the loaded elevation grid, and USGS topographic tiles.

**Architecture:** A Qt-free `LayerStack` holds ordered layer descriptors (visibility, opacity) and is testable headlessly. `BasemapOverlay` already assembles a mosaic `QImage` from tiles and uploads a single texture; visible raster layers composite into that one image with `QPainter` before upload, so no coplanar draws and no z-fighting. `ViewportWidget::paintGL` walks the stack instead of a hardcoded order.

**Tech Stack:** C++17, Qt 6 (Core, Gui, Widgets, Network, OpenGLWidgets), OpenGL 3.3 core, glm, nlohmann/json, CMake + CTest.

**Spec:** `docs/design/specs/2026-09-10-map-layers-design.md`

---

## File Structure

**Create:**
- `src/map/LayerStack.h` / `.cpp` — ordered layer descriptors, visibility, opacity, reorder. No Qt Widgets, no GL.
- `src/map/TileProviders.h` / `.cpp` — provider registry: URL template, axis order, cache namespace, attribution.
- `src/map/Hillshade.h` / `.cpp` — `QImage hillshade(const TerrainModel&, ...)`. Qt Gui only.
- `src/ui/LayersPanel.h` / `.cpp` — the Layers dock widget.
- `tests/test_layers.cpp` — headless tests for LayerStack, Hillshade, TileProviders.

**Modify:**
- `src/render/BasemapOverlay.h` / `.cpp` — composite N raster sources into one texture.
- `src/ui/ViewportWidget.h` / `.cpp` — walk the stack; split the inspected-point probe out of `LOSRayOverlay`.
- `src/ui/OverlayToolbar.h` / `.cpp` — drop the overlay checkboxes and Layers menu; keep the metric combo and view buttons.
- `src/ui/MainWindow.h` / `.cpp` — own the `LayerStack`, host the dock, drop `Overlay` wiring.
- `src/data/ScenarioIO.h` / `.cpp` — read/write the `layers` key.
- `CMakeLists.txt` — new sources, new `layer_tests` target.
- `tests/test_ui_interactions.cpp`, `tests/test_main_window_guard.cpp` — rewrite overlay-state coverage against the stack.

---

### Task 1: LayerStack

**Files:**
- Create: `src/map/LayerStack.h`, `src/map/LayerStack.cpp`
- Test: `tests/test_layers.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_layers.cpp`:

```cpp
#include "map/LayerStack.h"
#include <iostream>
#include <string>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

static void testDefaults() {
    LayerStack stack;
    check(stack.size() == 9, "stack has nine default layers");
    check(stack.at(0).id == "satellite", "satellite is the bottom layer");
    check(stack.at(8).id == "fresnel", "fresnel is the top layer");
    check(stack.at(0).visible, "satellite starts visible");
    check(!stack.find("usgs-topo")->visible, "topo starts hidden");
    check(!stack.find("hillshade")->visible, "hillshade starts hidden");
    check(stack.find("coverage")->visible, "coverage starts visible");
    check(!stack.find("los")->visible, "los starts hidden");
}

static void testVisibilityAndOpacity() {
    LayerStack stack;
    int changes = 0;
    stack.setOnChanged([&changes] { ++changes; });
    stack.setVisible("hillshade", true);
    check(stack.find("hillshade")->visible, "setVisible turns a layer on");
    check(changes == 1, "setVisible notifies once");
    stack.setVisible("hillshade", true);
    check(changes == 1, "setting the same value does not notify");
    stack.setOpacity("hillshade", 0.25f);
    check(std::abs(stack.find("hillshade")->opacity - 0.25f) < 1e-6f, "setOpacity stores the value");
    stack.setOpacity("hillshade", 5.0f);
    check(std::abs(stack.find("hillshade")->opacity - 1.0f) < 1e-6f, "opacity clamps to one");
    stack.setOpacity("hillshade", -1.0f);
    check(std::abs(stack.find("hillshade")->opacity) < 1e-6f, "opacity clamps to zero");
    check(stack.find("missing") == nullptr, "unknown id returns null");
}

static void testReorder() {
    LayerStack stack;
    stack.move("hillshade", 0);
    check(stack.at(0).id == "hillshade", "move places a layer at the requested index");
    check(stack.at(1).id == "satellite", "move shifts the displaced layer up");
    check(stack.size() == 9, "move does not change the layer count");
    stack.move("hillshade", 99);
    check(stack.at(8).id == "hillshade", "an out-of-range index clamps to the top");
}

static void testVisibleOfKind() {
    LayerStack stack;
    stack.setVisible("usgs-topo", true);
    const auto rasters = stack.visibleOfKind(LayerKind::BaseRaster);
    check(rasters.size() == 2, "both visible base rasters are returned");
    check(rasters[0].id == "satellite" && rasters[1].id == "usgs-topo",
          "visibleOfKind preserves stack order");
    stack.setVisible("satellite", false);
    check(stack.visibleOfKind(LayerKind::BaseRaster).size() == 1,
          "hiding a layer removes it from the result");
}

int main() {
    testDefaults();
    testVisibilityAndOpacity();
    testReorder();
    testVisibleOfKind();
    if (failures == 0) std::cout << "Layer tests passed.\n";
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Add the test target and run it to verify it fails**

Add to `CMakeLists.txt` after the `dted_decoding_tests` block:

```cmake
add_executable(ridgeline_layer_tests tests/test_layers.cpp src/map/LayerStack.cpp)
target_include_directories(ridgeline_layer_tests PRIVATE src)
target_link_libraries(ridgeline_layer_tests Qt6::Core glm::glm)
add_test(NAME layer_tests COMMAND ridgeline_layer_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
```

Run: `cmake -S . -B build2 -DCMAKE_BUILD_TYPE=Release -DRIDGELINE_GUI_TESTS=ON && cmake --build build2 --config Release --target ridgeline_layer_tests`
Expected: FAIL — `map/LayerStack.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `src/map/LayerStack.h`:

```cpp
#pragma once
#include <QString>
#include <functional>
#include <vector>

enum class LayerKind { BaseRaster, TerrainDerived, Analysis };

struct MapLayer {
    QString id;
    QString displayName;
    LayerKind kind = LayerKind::Analysis;
    bool visible = false;
    float opacity = 1.0f;
};

// Ordered map layers, bottom first. Paint order is stack order. Deliberately
// free of Qt Widgets and OpenGL so it can be tested without a display.
class LayerStack {
public:
    LayerStack();

    std::size_t size() const { return m_layers.size(); }
    const MapLayer& at(std::size_t index) const { return m_layers[index]; }
    const MapLayer* find(const QString& id) const;

    void setVisible(const QString& id, bool visible);
    void setOpacity(const QString& id, float opacity);
    void move(const QString& id, std::size_t toIndex);

    // Visible layers of one kind, in stack order.
    std::vector<MapLayer> visibleOfKind(LayerKind kind) const;

    void setOnChanged(std::function<void()> callback) {
        m_onChanged = std::move(callback);
    }

private:
    MapLayer* mutableFind(const QString& id);
    void notify();

    std::vector<MapLayer> m_layers;
    std::function<void()> m_onChanged;
};
```

Create `src/map/LayerStack.cpp`:

```cpp
#include "map/LayerStack.h"
#include <algorithm>

LayerStack::LayerStack() {
    m_layers = {
        {"satellite",  "Satellite",        LayerKind::BaseRaster,     true,  0.86f},
        {"usgs-topo",  "USGS topographic", LayerKind::BaseRaster,     false, 1.0f},
        {"hillshade",  "Hillshade",        LayerKind::TerrainDerived, false, 0.6f},
        {"coverage",   "Coverage",         LayerKind::Analysis,       true,  1.0f},
        {"comparison", "Scenario compare", LayerKind::Analysis,       false, 1.0f},
        {"los",        "Line of sight",    LayerKind::Analysis,       false, 1.0f},
        {"pattern",    "Antenna pattern",  LayerKind::Analysis,       false, 1.0f},
        {"jamming",    "Jamming range",    LayerKind::Analysis,       false, 1.0f},
        {"fresnel",    "Fresnel zone",     LayerKind::Analysis,       false, 1.0f},
    };
}

const MapLayer* LayerStack::find(const QString& id) const {
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
        [&id](const MapLayer& l) { return l.id == id; });
    return it == m_layers.end() ? nullptr : &*it;
}

MapLayer* LayerStack::mutableFind(const QString& id) {
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
        [&id](const MapLayer& l) { return l.id == id; });
    return it == m_layers.end() ? nullptr : &*it;
}

void LayerStack::notify() { if (m_onChanged) m_onChanged(); }

void LayerStack::setVisible(const QString& id, bool visible) {
    MapLayer* layer = mutableFind(id);
    if (!layer || layer->visible == visible) return;
    layer->visible = visible;
    notify();
}

void LayerStack::setOpacity(const QString& id, float opacity) {
    MapLayer* layer = mutableFind(id);
    if (!layer) return;
    const float clamped = std::clamp(opacity, 0.0f, 1.0f);
    if (layer->opacity == clamped) return;
    layer->opacity = clamped;
    notify();
}

void LayerStack::move(const QString& id, std::size_t toIndex) {
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
        [&id](const MapLayer& l) { return l.id == id; });
    if (it == m_layers.end()) return;
    MapLayer layer = *it;
    m_layers.erase(it);
    m_layers.insert(m_layers.begin() + std::min(toIndex, m_layers.size()), layer);
    notify();
}

std::vector<MapLayer> LayerStack::visibleOfKind(LayerKind kind) const {
    std::vector<MapLayer> result;
    for (const MapLayer& layer : m_layers)
        if (layer.kind == kind && layer.visible) result.push_back(layer);
    return result;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build2 --config Release --target ridgeline_layer_tests && ctest --test-dir build2 -C Release -R layer_tests --output-on-failure`
Expected: PASS, `Layer tests passed.`

- [ ] **Step 5: Commit**

```bash
git add src/map/LayerStack.h src/map/LayerStack.cpp tests/test_layers.cpp CMakeLists.txt
git commit -m "Add LayerStack: ordered map layers with visibility and opacity"
```

---

### Task 2: Hillshade from the elevation grid

**Files:**
- Create: `src/map/Hillshade.h`, `src/map/Hillshade.cpp`
- Modify: `tests/test_layers.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_layers.cpp`, before `main`:

```cpp
#include "map/Hillshade.h"
#include "data/TerrainLoader.h"

static void testHillshade() {
    // A ramp rising toward the north. Sun from the northwest at 45 degrees
    // must light the north-facing slope more than a flat reference.
    TerrainModel ramp = TerrainLoader::makeFlat(16, 16, 30.0f, 0.0f);
    for (int z = 0; z < ramp.height; ++z)
        for (int x = 0; x < ramp.width; ++x)
            ramp.heights[z * ramp.width + x] = static_cast<float>(z) * 10.0f;

    const QImage shaded = Hillshade::render(ramp, 315.0, 45.0);
    check(!shaded.isNull(), "hillshade produces an image");
    check(shaded.width() == ramp.width && shaded.height() == ramp.height,
          "hillshade matches the terrain grid size");

    TerrainModel flat = TerrainLoader::makeFlat(16, 16, 30.0f, 0.0f);
    const QImage flatShaded = Hillshade::render(flat, 315.0, 45.0);
    check(qGray(shaded.pixel(8, 8)) != qGray(flatShaded.pixel(8, 8)),
          "a slope shades differently from flat ground");

    // Shading must come from true elevations, so doubling the sample spacing
    // (a gentler real slope) must change the result.
    TerrainModel wide = TerrainLoader::makeFlat(16, 16, 60.0f, 0.0f);
    for (int z = 0; z < wide.height; ++z)
        for (int x = 0; x < wide.width; ++x)
            wide.heights[z * wide.width + x] = static_cast<float>(z) * 10.0f;
    check(qGray(Hillshade::render(wide, 315.0, 45.0).pixel(8, 8)) != qGray(shaded.pixel(8, 8)),
          "sample spacing changes the computed slope");
}
```

Add `testHillshade();` to `main` before the failure check.

- [ ] **Step 2: Run to verify it fails**

Update the test target in `CMakeLists.txt`:

```cmake
add_executable(ridgeline_layer_tests tests/test_layers.cpp
    src/map/LayerStack.cpp src/map/Hillshade.cpp
    src/sim/TerrainModel.cpp src/data/TerrainLoader.cpp)
target_include_directories(ridgeline_layer_tests PRIVATE src)
target_link_libraries(ridgeline_layer_tests Qt6::Core Qt6::Gui glm::glm nlohmann_json::nlohmann_json)
```

Run: `cmake -S . -B build2 -DCMAKE_BUILD_TYPE=Release -DRIDGELINE_GUI_TESTS=ON && cmake --build build2 --config Release --target ridgeline_layer_tests`
Expected: FAIL — `map/Hillshade.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `src/map/Hillshade.h`:

```cpp
#pragma once
#include <QImage>
class TerrainModel;

namespace Hillshade {
// Lambertian relief shading computed from true elevations and the model's real
// sample spacing in metres. Never use the viewport's vertical exaggeration:
// shading the stretched display mesh overstates every slope.
QImage render(const TerrainModel& terrain, double azimuthDeg, double altitudeDeg);
}
```

Create `src/map/Hillshade.cpp`:

```cpp
#include "map/Hillshade.h"
#include "sim/TerrainModel.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

QImage Hillshade::render(const TerrainModel& terrain,
                         double azimuthDeg, double altitudeDeg) {
    if (terrain.width < 2 || terrain.height < 2) return {};

    const double zenith = (90.0 - altitudeDeg) * kPi / 180.0;
    // Compass azimuth to math azimuth.
    const double azimuth = (360.0 - azimuthDeg + 90.0) * kPi / 180.0;
    const double cellX = terrain.metersPerCellX();
    const double cellZ = terrain.metersPerCellZ();
    if (!(cellX > 0.0) || !(cellZ > 0.0)) return {};

    QImage image(terrain.width, terrain.height, QImage::Format_Grayscale8);

    const auto heightAt = [&terrain](int x, int z) {
        const int cx = std::clamp(x, 0, terrain.width - 1);
        const int cz = std::clamp(z, 0, terrain.height - 1);
        return static_cast<double>(terrain.heights[cz * terrain.width + cx]);
    };

    for (int z = 0; z < terrain.height; ++z) {
        uchar* line = image.scanLine(z);
        for (int x = 0; x < terrain.width; ++x) {
            // Horn's method over the eight neighbours.
            const double a = heightAt(x - 1, z - 1), b = heightAt(x, z - 1), c = heightAt(x + 1, z - 1);
            const double d = heightAt(x - 1, z),                             f = heightAt(x + 1, z);
            const double g = heightAt(x - 1, z + 1), h = heightAt(x, z + 1), i = heightAt(x + 1, z + 1);

            const double dzdx = ((c + 2 * f + i) - (a + 2 * d + g)) / (8.0 * cellX);
            const double dzdy = ((g + 2 * h + i) - (a + 2 * b + c)) / (8.0 * cellZ);

            const double slope = std::atan(std::hypot(dzdx, dzdy));
            double aspect = std::atan2(dzdy, -dzdx);
            if (aspect < 0.0) aspect += 2.0 * kPi;

            double value = std::cos(zenith) * std::cos(slope) +
                           std::sin(zenith) * std::sin(slope) * std::cos(azimuth - aspect);
            value = std::clamp(value, 0.0, 1.0);
            // The terrain grid stores rows south to north; images run top down.
            line[x] = static_cast<uchar>(std::lround(value * 255.0));
        }
    }
    return image.mirrored(false, true);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build2 --config Release --target ridgeline_layer_tests && ctest --test-dir build2 -C Release -R layer_tests --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/map/Hillshade.h src/map/Hillshade.cpp tests/test_layers.cpp CMakeLists.txt
git commit -m "Add hillshade rendering from true terrain elevations"
```

---

### Task 3: Tile provider registry with axis ordering

**Files:**
- Create: `src/map/TileProviders.h`, `src/map/TileProviders.cpp`
- Modify: `tests/test_layers.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_layers.cpp`, before `main`:

```cpp
#include "map/TileProviders.h"

static void testProviders() {
    const MapTileProvider topo = TileProviders::usgsTopo();
    check(topo.id == "usgs-topo", "usgs provider has the layer id");
    check(topo.axisOrder == TileAxisOrder::ZYX, "usgs ArcGIS tiles are row before column");
    check(topo.attribution.contains("USGS"), "usgs provider credits USGS");

    // ArcGIS wants /{z}/{y}/{x}; a z/x/y substitution would swap 7 and 22.
    const QString url = TileProviders::tileUrl(topo, 5, 7, 22);
    check(url.endsWith("/5/22/7"), "usgs url puts row before column");

    const MapTileProvider esri = TileProviders::esriImagery();
    check(esri.axisOrder == TileAxisOrder::ZXY, "esri tiles are column before row");
    check(TileProviders::tileUrl(esri, 5, 7, 22).endsWith("/5/22/7") == false,
          "esri url is not in ArcGIS row-first order");

    check(TileProviders::cachePath(topo, 5, 7, 22) == "imagery/usgs-topo/5/7/22.tile",
          "cache path uses the provider namespace and logical x/y");
}
```

Add `testProviders();` to `main`.

- [ ] **Step 2: Run to verify it fails**

Update the target sources in `CMakeLists.txt` to add `src/map/TileProviders.cpp`.

Run: `cmake -S . -B build2 -DCMAKE_BUILD_TYPE=Release -DRIDGELINE_GUI_TESTS=ON && cmake --build build2 --config Release --target ridgeline_layer_tests`
Expected: FAIL — `map/TileProviders.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `src/map/TileProviders.h`:

```cpp
#pragma once
#include <QString>

// ArcGIS REST tile services address tiles row-first (/{z}/{y}/{x}); XYZ
// services address them column-first. Carrying the order per provider avoids
// silently transposing a whole basemap.
enum class TileAxisOrder { ZXY, ZYX };

struct MapTileProvider {
    QString id;
    QString displayName;
    QString urlTemplate;     // contains {z}, {x}, {y}
    QString attribution;
    TileAxisOrder axisOrder = TileAxisOrder::ZXY;
    int maxZoom = 19;
    bool usOnly = false;
};

namespace TileProviders {
MapTileProvider esriImagery();
MapTileProvider usgsTopo();

QString tileUrl(const MapTileProvider& provider, int z, int x, int y);
QString cachePath(const MapTileProvider& provider, int z, int x, int y);
}
```

Create `src/map/TileProviders.cpp`:

```cpp
#include "map/TileProviders.h"

MapTileProvider TileProviders::esriImagery() {
    MapTileProvider p;
    p.id = "satellite";
    p.displayName = "Satellite";
    p.urlTemplate = "https://server.arcgisonline.com/ArcGIS/rest/services/"
                    "World_Imagery/MapServer/tile/{z}/{y}/{x}";
    p.attribution = "Imagery © Esri, Maxar, Earthstar Geographics, and contributors";
    // The Esri template already spells the row-first order literally, so the
    // substitution below is the plain XYZ one.
    p.axisOrder = TileAxisOrder::ZXY;
    p.maxZoom = 19;
    return p;
}

MapTileProvider TileProviders::usgsTopo() {
    MapTileProvider p;
    p.id = "usgs-topo";
    p.displayName = "USGS topographic";
    p.urlTemplate = "https://basemap.nationalmap.gov/arcgis/rest/services/"
                    "USGSTopo/MapServer/tile/{z}/{y}/{x}";
    p.attribution = "Topographic map © USGS, The National Map";
    p.axisOrder = TileAxisOrder::ZYX;
    p.maxZoom = 16;
    p.usOnly = true;
    return p;
}

QString TileProviders::tileUrl(const MapTileProvider& provider, int z, int x, int y) {
    QString url = provider.urlTemplate;
    url.replace("{z}", QString::number(z));
    if (provider.axisOrder == TileAxisOrder::ZYX) {
        // Template names are literal: fill {y} with the row and {x} with the
        // column, which for these services already appear in row-first order.
        url.replace("{y}", QString::number(y));
        url.replace("{x}", QString::number(x));
    } else {
        url.replace("{x}", QString::number(x));
        url.replace("{y}", QString::number(y));
    }
    return url;
}

QString TileProviders::cachePath(const MapTileProvider& provider, int z, int x, int y) {
    return QStringLiteral("imagery/%1/%2/%3/%4.tile")
        .arg(provider.id).arg(z).arg(x).arg(y);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build2 --config Release --target ridgeline_layer_tests && ctest --test-dir build2 -C Release -R layer_tests --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/map/TileProviders.h src/map/TileProviders.cpp tests/test_layers.cpp CMakeLists.txt
git commit -m "Add tile provider registry with per-provider axis ordering"
```

---

### Task 4: Persist layer state in the scenario

**Files:**
- Modify: `src/data/ScenarioIO.h`, `src/data/ScenarioIO.cpp`, `tests/test_scenario_save.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_scenario_save.cpp`, inside its existing `main` before the success print:

```cpp
    {
        ScenarioDoc doc;
        doc.layers = {{"hillshade", true, 0.4f}, {"satellite", false, 1.0f}};
        const std::string path = (dir.path() + "/layers.json").toStdString();
        ScenarioIO::save(doc, path);
        const ScenarioDoc back = ScenarioIO::load(path);
        require(back.layers.size() == 2, "layer state round-trips");
        require(back.layers[0].id == "hillshade" && back.layers[0].visible &&
                std::abs(back.layers[0].opacity - 0.4f) < 1e-6f,
                "layer fields round-trip in order");

        // A file with no layers key must load, so older scenarios keep working.
        QFile file(QString::fromStdString(path));
        require(file.open(QIODevice::WriteOnly), "rewrite scenario without layers");
        file.write("{\"mode\":\"planner\"}");
        file.close();
        require(ScenarioIO::load(path).layers.empty(),
                "a scenario without a layers key loads with none");
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build2 --config Release --target ridgeline_scenario_save_tests`
Expected: FAIL — `'struct ScenarioDoc' has no member named 'layers'`

- [ ] **Step 3: Write the implementation**

In `src/data/ScenarioIO.h`, add above `struct ScenarioDoc`:

```cpp
struct ScenarioLayerState {
    std::string id;
    bool visible = false;
    float opacity = 1.0f;
};
```

and add to `ScenarioDoc`:

```cpp
    std::vector<ScenarioLayerState> layers;
```

In `src/data/ScenarioIO.cpp`, before the `j.dump(2)` call in `save`:

```cpp
    json layers = json::array();
    for (const auto& layer : doc.layers) {
        layers.push_back({{"id", layer.id},
                          {"visible", layer.visible},
                          {"opacity", layer.opacity}});
    }
    j["layers"] = std::move(layers);
```

and in `load`, inside the existing `try` block:

```cpp
        // Absent in scenarios written before layers existed; unknown ids are
        // kept here and ignored by the stack, so a newer file stays loadable.
        if (j.contains("layers") && j["layers"].is_array()) {
            for (const auto& entry : j["layers"]) {
                ScenarioLayerState layer;
                layer.id = entry.value("id", std::string{});
                layer.visible = entry.value("visible", false);
                layer.opacity = entry.value("opacity", 1.0f);
                if (!layer.id.empty()) doc.layers.push_back(layer);
            }
        }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build2 --config Release --target ridgeline_scenario_save_tests && ctest --test-dir build2 -C Release -R scenario_save --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/data/ScenarioIO.h src/data/ScenarioIO.cpp tests/test_scenario_save.cpp
git commit -m "Persist map layer state in the scenario file"
```

---

### Task 5: Apply a LayerStack to and from scenario state

**Files:**
- Modify: `src/map/LayerStack.h`, `src/map/LayerStack.cpp`, `tests/test_layers.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_layers.cpp`:

```cpp
static void testScenarioRoundTrip() {
    LayerStack stack;
    stack.setVisible("hillshade", true);
    stack.setOpacity("hillshade", 0.3f);
    stack.move("hillshade", 0);

    LayerStack restored;
    restored.applyState(stack.state());
    check(restored.at(0).id == "hillshade", "applyState restores order");
    check(restored.find("hillshade")->visible, "applyState restores visibility");
    check(std::abs(restored.find("hillshade")->opacity - 0.3f) < 1e-6f,
          "applyState restores opacity");

    LayerStack unknown;
    unknown.applyState({{"not-a-layer", true, 1.0f}});
    check(unknown.size() == 9, "an unknown id is ignored, not inserted");
    check(unknown.at(0).id == "satellite", "an unknown id leaves order intact");

    LayerStack empty;
    empty.applyState({});
    check(empty.find("satellite")->visible, "empty state leaves defaults alone");
}
```

Add `testScenarioRoundTrip();` to `main`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build2 --config Release --target ridgeline_layer_tests`
Expected: FAIL — `'class LayerStack' has no member named 'applyState'`

- [ ] **Step 3: Write the implementation**

Add to `src/map/LayerStack.h`, inside the class:

```cpp
    struct State { QString id; bool visible; float opacity; };
    std::vector<State> state() const;
    // Ordering follows the supplied list; ids absent from it keep their
    // relative order after those present. Unknown ids are ignored.
    void applyState(const std::vector<State>& state);
```

Add to `src/map/LayerStack.cpp`:

```cpp
std::vector<LayerStack::State> LayerStack::state() const {
    std::vector<State> out;
    out.reserve(m_layers.size());
    for (const MapLayer& layer : m_layers)
        out.push_back({layer.id, layer.visible, layer.opacity});
    return out;
}

void LayerStack::applyState(const std::vector<State>& state) {
    if (state.empty()) return;
    std::vector<MapLayer> ordered;
    ordered.reserve(m_layers.size());
    for (const State& entry : state) {
        MapLayer* layer = mutableFind(entry.id);
        if (!layer) continue;   // unknown id: ignore, do not insert
        layer->visible = entry.visible;
        layer->opacity = std::clamp(entry.opacity, 0.0f, 1.0f);
        ordered.push_back(*layer);
    }
    for (const MapLayer& layer : m_layers) {
        const bool alreadyPlaced = std::any_of(ordered.begin(), ordered.end(),
            [&layer](const MapLayer& l) { return l.id == layer.id; });
        if (!alreadyPlaced) ordered.push_back(layer);
    }
    m_layers = std::move(ordered);
    notify();
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build2 --config Release --target ridgeline_layer_tests && ctest --test-dir build2 -C Release -R layer_tests --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/map/LayerStack.h src/map/LayerStack.cpp tests/test_layers.cpp
git commit -m "Round-trip LayerStack state for scenario persistence"
```

---

### Task 6: Composite raster layers into one basemap texture

**Files:**
- Modify: `src/render/BasemapOverlay.h`, `src/render/BasemapOverlay.cpp`

Rationale: base rasters are textured onto the terrain surface. Two of them as separate coplanar passes z-fight. The mosaic is already a `QImage` uploaded as a single texture, so compositing happens on the CPU before upload.

- [ ] **Step 1: Add the layer inputs to the overlay interface**

In `src/render/BasemapOverlay.h`, add to the public section:

```cpp
    struct RasterRequest {
        QString providerId;   // matches MapTileProvider::id
        float opacity = 1.0f;
    };
    // Base rasters to composite, bottom first. Replaces the single implicit
    // provider. An empty list leaves the terrain untextured.
    void setRasterLayers(std::vector<RasterRequest> layers) {
        m_rasterLayers = std::move(layers);
    }
    // Multiplied over the composited rasters. Null disables relief shading.
    void setHillshade(const QImage& shade, float opacity) {
        m_hillshade = shade;
        m_hillshadeOpacity = opacity;
    }
```

and to the private section:

```cpp
    std::vector<RasterRequest> m_rasterLayers;
    QImage m_hillshade;
    float  m_hillshadeOpacity = 0.0f;
```

with `#include <QImage>` and `#include <vector>` at the top.

- [ ] **Step 2: Composite in loadTextureForTerrain**

In `src/render/BasemapOverlay.cpp`, replace the single-provider mosaic assembly so that it loops over `m_rasterLayers`, building one mosaic per provider via `TileProviders::tileUrl` / `TileProviders::cachePath`, then composites them in order:

```cpp
    QImage composite(mosaicWidth, mosaicHeight, QImage::Format_RGBA8888);
    composite.fill(Qt::transparent);
    {
        QPainter painter(&composite);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        for (std::size_t i = 0; i < mosaics.size(); ++i) {
            painter.setOpacity(m_rasterLayers[i].opacity);
            painter.drawImage(0, 0, mosaics[i]);
        }
        if (!m_hillshade.isNull() && m_hillshadeOpacity > 0.0f) {
            // Multiply keeps the base map's colour and darkens by relief.
            painter.setCompositionMode(QPainter::CompositionMode_Multiply);
            painter.setOpacity(m_hillshadeOpacity);
            painter.drawImage(QRect(0, 0, composite.width(), composite.height()),
                              m_hillshade.convertToFormat(QImage::Format_RGBA8888));
        }
    }
```

When `m_rasterLayers` is empty but a hillshade is present, fill `composite` with an opaque neutral grey (`QColor(150, 150, 150)`) before the multiply, so relief is usable alone. When both are empty, return `false` without a texture so the terrain draws with its existing untextured shading.

Add `#include <QPainter>` at the top.

- [ ] **Step 3: Build**

Run: `cmake --build build2 --config Release --target ridgeline`
Expected: builds with no errors

- [ ] **Step 4: Commit**

```bash
git add src/render/BasemapOverlay.h src/render/BasemapOverlay.cpp
git commit -m "Composite multiple raster layers and hillshade into one texture"
```

---

### Task 7: Drive the viewport from the stack

**Files:**
- Modify: `src/ui/ViewportWidget.h`, `src/ui/ViewportWidget.cpp`

- [ ] **Step 1: Give the viewport a stack pointer**

In `src/ui/ViewportWidget.h`, replace the `Overlay` enum and `setOverlayVisible` / `isOverlayVisible` with:

```cpp
    void setLayerStack(LayerStack* layers);   // not owned
    void refreshLayers();                     // re-read stack state and redraw
```

and store `LayerStack* m_layers = nullptr;`.

- [ ] **Step 2: Walk the stack in paintGL**

Replace the hardcoded body of `paintGL` after `m_terrain.draw(m_lastMVP)` with a loop over `m_layers`, dispatching by id:

```cpp
    for (std::size_t i = 0; i < m_layers->size(); ++i) {
        const MapLayer& layer = m_layers->at(i);
        if (!layer.visible) continue;
        if (layer.id == "coverage")        m_heatmap.draw(m_lastMVP);
        else if (layer.id == "comparison") m_comparisonHeatmap.draw(m_lastMVP);
        else if (layer.id == "los")        m_losRay.draw(m_lastMVP);
        else if (layer.id == "pattern")    m_antennaLobe.draw(m_lastMVP);
        else if (layer.id == "jamming")    m_jammerBubble.draw(m_lastMVP);
        else if (layer.id == "fresnel")    m_fresnel.draw(m_lastMVP);
    }
```

Base rasters and hillshade are not drawn here: they are composited into the basemap texture, which is drawn once before the loop when any is visible.

- [ ] **Step 3: Split the inspected-point probe out of the LOS overlay**

`paintGL` currently calls `m_losRay.draw` unconditionally so the probe line survives the LOS layer being hidden. Add a dedicated `drawProbe(const glm::mat4&)` to `LOSRayOverlay` that draws only the probe geometry, call it unconditionally after the loop, and remove the `m_hasProbe` special case from `draw` so `draw` honours visibility alone.

- [ ] **Step 4: Build and run the suite**

Run: `cmake --build build2 --config Release --parallel && ctest --test-dir build2 -C Release --output-on-failure`
Expected: overlay-related tests fail — they are rewritten in Task 9. Everything else passes.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ViewportWidget.h src/ui/ViewportWidget.cpp src/render/overlays/LOSRayOverlay.h src/render/overlays/LOSRayOverlay.cpp
git commit -m "Render viewport overlays by walking the layer stack"
```

---

### Task 8: Layers panel

**Files:**
- Create: `src/ui/LayersPanel.h`, `src/ui/LayersPanel.cpp`
- Modify: `src/ui/OverlayToolbar.h`, `src/ui/OverlayToolbar.cpp`, `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Write the panel**

`LayersPanel` is a `QWidget` holding a `QListWidget` with one row per layer. Each row is a custom widget: a drag handle, a `QCheckBox` bound to `visible`, the display name, and a `QSlider` (0–100) bound to `opacity`. Set `objectName` on each control as `LayerRow<id>`, `LayerVisible<id>`, `LayerOpacity<id>` so tests can address them. Enable `QAbstractItemView::InternalMove` for drag reordering and map row moves onto `LayerStack::move`.

The panel writes to the stack and re-reads it on `setOnChanged`, so external changes stay in sync.

- [ ] **Step 2: Remove the old controls**

Delete the Imagery and Coverage checkboxes, the opacity slider and the Layers menu from `OverlayToolbar`. Keep the coverage metric combo — it selects what Coverage shows, not whether it is visible — and keep Top view / Reset view.

- [ ] **Step 3: Host the dock in MainWindow**

Own a `LayerStack` in `MainWindow`, pass it to `ViewportWidget::setLayerStack`, add the `LayersPanel` in a `QDockWidget` named `LayersDock` tabified with the existing right-hand docks, and add its `toggleViewAction` to the View menu. On scenario save write `m_layers.state()`; on load call `applyState`.

- [ ] **Step 4: Build and check by eye**

Run: `cmake --build build2 --config Release --parallel && ./build2/ridgeline.exe`
Expected: the Layers dock lists nine layers; toggling Hillshade shades the terrain; toggling USGS topographic swaps the base map.

- [ ] **Step 5: Commit**

```bash
git add src/ui/LayersPanel.h src/ui/LayersPanel.cpp src/ui/OverlayToolbar.h src/ui/OverlayToolbar.cpp src/ui/MainWindow.h src/ui/MainWindow.cpp CMakeLists.txt
git commit -m "Add the Layers dock and remove the fixed overlay controls"
```

---

### Task 9: Rewrite the overlay regressions against the stack

**Files:**
- Modify: `tests/test_ui_interactions.cpp`, `tests/test_main_window_guard.cpp`

The existing tests drive `ViewportWidget::Overlay` and the `OverlayToggle%1` / `AnalysisLayer%1` object names, all removed by this work. They must be rewritten, not deleted: they assert that a layer's on-screen state matches its control, and that layer state survives a terrain load and a picker cancellation.

- [ ] **Step 1: Rewrite the panel test**

In `tests/test_ui_interactions.cpp`, replace the overlay-toggle coverage with a `LayersPanel` test: every layer has a row, clicking `LayerVisible<id>` flips `LayerStack::find(id)->visible`, and moving `LayerOpacity<id>` changes `opacity`.

- [ ] **Step 2: Rewrite the preservation test**

In `tests/test_main_window_guard.cpp`, rewrite `testTerrainFailureAndCancellationPreserveWorkspace`'s overlay section to set every layer to a known non-default state, cancel the terrain picker, and assert every layer's visibility and opacity survived. Keep testing both values per layer so a reset to defaults cannot pass by accident.

- [ ] **Step 3: Run the full suite**

Run: `ctest --test-dir build2 -C Release --output-on-failure`
Expected: all suites pass

- [ ] **Step 4: Commit**

```bash
git add tests/test_ui_interactions.cpp tests/test_main_window_guard.cpp
git commit -m "Rewrite overlay regressions against the layer stack"
```

---

### Task 10: Attribution, no-coverage state, and docs

**Files:**
- Modify: `src/ui/ViewportWidget.cpp`, `src/render/BasemapOverlay.cpp`, `README.md`, `docs/ROADMAP.md`

- [ ] **Step 1: Aggregate attribution**

`ViewportWidget::updateMapHud` builds a fixed two-case credit string. Replace it with a join of the attribution of every visible raster provider plus the elevation credit, deduplicated, separated by `  ·  `.

- [ ] **Step 2: Report absent topographic coverage**

USGS serves no tiles outside the United States. When the topo layer is visible and every tile request for the area returns empty, set the layer's error to `No topographic coverage for this area` and surface it in the status line rather than showing a blank base map.

- [ ] **Step 3: Document**

Add a Map layers section to `README.md` covering the stack, hillshade, USGS coverage limits and attribution requirements. Add a ROADMAP checkpoint recording what shipped, and state plainly what is not established: no non-US topographic coverage, no contours, no bring-your-own-provider.

- [ ] **Step 4: Full verification**

Run: `cmake --build build2 --config Release --parallel && ctest --test-dir build2 -C Release --output-on-failure && git diff --check`
Expected: all suites pass, whitespace clean

- [ ] **Step 5: Commit**

```bash
git add src/ui/ViewportWidget.cpp src/render/BasemapOverlay.cpp README.md docs/ROADMAP.md
git commit -m "Aggregate layer attribution and document map layers"
```
