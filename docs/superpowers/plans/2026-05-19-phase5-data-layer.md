# Phase 5 — Data Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add AntennaLibrary (JSON-loaded type table), antenna type picker in PropertiesPanel, ScenarioIO (JSON scenario save/load), PNG/CSV coverage export, and wire everything into MainWindow's File and Export menus.

**Architecture:** Four new data-layer modules (AntennaLibrary, ScenarioIO, Exporter) plus a UI extension (PropertiesPanel type combo) all compose through the existing SimEngine/Types interfaces. Exporter uses Qt's QImage and lives only in the main binary (not the test binary) because it links Qt. ScenarioIO and AntennaLibrary are pure C++17 + nlohmann/json and are added to the test binary.

**Tech Stack:** C++17, Qt6 (QImage for PNG, QFileDialog for paths), nlohmann/json (already fetched via CMakeLists), existing SimEngine/Types/TerrainLoader interfaces.

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `assets/antennas.json` | Antenna type definitions (name, default gain, beamwidth) |
| Create | `src/data/AntennaLibrary.h` | `AntennaType` struct + `AntennaLibrary` class |
| Create | `src/data/AntennaLibrary.cpp` | JSON load, find-by-name, built-in defaults |
| Modify | `src/sim/Types.h` | Add `antennaType` string field to `Antenna` |
| Modify | `src/ui/PropertiesPanel.h` | Add `m_typeCombo`, `setAntennaLibrary()` |
| Modify | `src/ui/PropertiesPanel.cpp` | Populate combo, update gain default on type change |
| Create | `src/data/ScenarioIO.h` | `TerrainSource`, `ScenarioDoc` structs + `ScenarioIO` class |
| Create | `src/data/ScenarioIO.cpp` | JSON save/load round-trip |
| Create | `src/data/Exporter.h` | `Exporter` class (static PNG + CSV methods) |
| Create | `src/data/Exporter.cpp` | QImage colormap for PNG, plain-text for CSV |
| Modify | `src/ui/MainWindow.h` | Add `m_antennaLib`, `m_terrainSource`, new slots |
| Modify | `src/ui/MainWindow.cpp` | Wire File ▸ Save/Open Scenario, Export ▸ PNG/CSV |
| Modify | `CMakeLists.txt` | Add AntennaLibrary.cpp + ScenarioIO.cpp to ridgeline_tests |
| Modify | `tests/test_main.cpp` | Tests for AntennaLibrary and ScenarioIO |

---

## Task 1: AntennaLibrary + `antennaType` field in Antenna

**Files:**
- Create: `assets/antennas.json`
- Create: `src/data/AntennaLibrary.h`
- Create: `src/data/AntennaLibrary.cpp`
- Modify: `src/sim/Types.h`

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_main.cpp` (after the existing `#include` block):

```cpp
#include "data/AntennaLibrary.h"
```

And add these test functions before `main()`:

```cpp
static void test_antenna_library_defaults() {
    const AntennaLibrary& lib = AntennaLibrary::defaults();
    CHECK(!lib.types().empty());

    const AntennaType* omni = lib.find("omni");
    CHECK(omni != nullptr);
    CHECK_NEAR(omni->defaultGainDbi, 3.0f, 0.01f);

    const AntennaType* missing = lib.find("nonexistent");
    CHECK(missing == nullptr);
}

static void test_antenna_library_load_json(const std::string& path) {
    AntennaLibrary lib;
    lib.load(path);

    CHECK(!lib.types().empty());
    const AntennaType* yagi = lib.find("yagi");
    CHECK(yagi != nullptr);
    CHECK_NEAR(yagi->defaultGainDbi, 10.0f, 0.01f);
    CHECK_NEAR(yagi->beamwidthDeg, 60.0f, 0.1f);

    const AntennaType* patch = lib.find("patch");
    CHECK(patch != nullptr);
    CHECK(patch->beamwidthDeg < 180.0f);  // directional
}
```

Add calls in `main()`:

```cpp
    test_antenna_library_defaults();
    test_antenna_library_load_json("assets/antennas.json");
```

- [ ] **Step 2: Run tests — expect link failure (AntennaLibrary not yet defined)**

```
cd <repo-root>\build
$env:PATH += ";C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1310_64\bin"
cmake --build . --target ridgeline_tests -j8 2>&1 | Select-String -Pattern "error|warning|undefined"
```

Expected: linker errors — `AntennaLibrary` not defined yet.

- [ ] **Step 3: Create `assets/antennas.json`**

```json
{
  "antenna_types": [
    { "name": "omni",   "default_gain_dbi": 3.0,  "beamwidth_deg": 360.0 },
    { "name": "dipole", "default_gain_dbi": 2.15, "beamwidth_deg": 360.0 },
    { "name": "yagi",   "default_gain_dbi": 10.0, "beamwidth_deg": 60.0  },
    { "name": "patch",  "default_gain_dbi": 8.0,  "beamwidth_deg": 90.0  }
  ]
}
```

- [ ] **Step 4: Create `src/data/AntennaLibrary.h`**

```cpp
#pragma once
#include <string>
#include <vector>

struct AntennaType {
    std::string name;
    float defaultGainDbi = 3.0f;
    float beamwidthDeg   = 360.0f;  // 360 = omnidirectional
};

class AntennaLibrary {
public:
    // Load from JSON file; throws std::runtime_error on failure.
    void load(const std::string& path);

    // All loaded types in order.
    const std::vector<AntennaType>& types() const { return m_types; }

    // Find by name; returns nullptr if not found.
    const AntennaType* find(const std::string& name) const;

    // Built-in fallback — four hard-coded types, no file needed.
    static const AntennaLibrary& defaults();

private:
    std::vector<AntennaType> m_types;
};
```

- [ ] **Step 5: Create `src/data/AntennaLibrary.cpp`**

```cpp
#include "AntennaLibrary.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

void AntennaLibrary::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open antenna library: " + path);

    std::vector<AntennaType> tmp;
    try {
        json data = json::parse(f);
        for (const auto& entry : data["antenna_types"]) {
            AntennaType t;
            t.name           = entry["name"].get<std::string>();
            t.defaultGainDbi = entry["default_gain_dbi"].get<float>();
            t.beamwidthDeg   = entry["beamwidth_deg"].get<float>();
            tmp.push_back(std::move(t));
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("antennas.json parse error: ") + e.what());
    }
    m_types = std::move(tmp);
}

const AntennaType* AntennaLibrary::find(const std::string& name) const {
    for (const auto& t : m_types)
        if (t.name == name) return &t;
    return nullptr;
}

const AntennaLibrary& AntennaLibrary::defaults() {
    static const AntennaLibrary lib = [] {
        AntennaLibrary l;
        l.m_types = {
            {"omni",   3.0f,  360.0f},
            {"dipole", 2.15f, 360.0f},
            {"yagi",   10.0f,  60.0f},
            {"patch",   8.0f,  90.0f},
        };
        return l;
    }();
    return lib;
}
```

- [ ] **Step 6: Add `antennaType` field to `Antenna` in `src/sim/Types.h`**

```cpp
struct Antenna {
    glm::vec3   position;
    float       frequencyMHz = 150.0f;
    float       powerW       = 50.0f;
    float       gainDbi      = 3.0f;
    bool        isJammer     = false;
    std::string antennaType  = "omni";  // must match an AntennaLibrary entry name
};
```

Also add `#include <string>` at the top of `Types.h` if it's not already included (it isn't — the file only includes `<vector>` and `<glm/glm.hpp>`).

- [ ] **Step 7: Add AntennaLibrary.cpp to CMakeLists.txt test target**

In `CMakeLists.txt`, update the `ridgeline_tests` sources list:

```cmake
add_executable(ridgeline_tests
    tests/test_main.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/FastSolver.cpp
    src/sim/FullSolver.cpp
    src/sim/SimEngine.cpp
    src/data/AntennaLibrary.cpp
)
```

- [ ] **Step 8: Build and run tests — expect PASS**

```powershell
cmake .. -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/mingw_64" `
  -DCMAKE_BUILD_TYPE=Release
cmake --build . --target ridgeline_tests -j8
.\ridgeline_tests.exe assets/materials.json
```

Expected: all tests pass including `test_antenna_library_defaults` and `test_antenna_library_load_json`.

- [ ] **Step 9: Commit**

```bash
git add assets/antennas.json src/data/AntennaLibrary.h src/data/AntennaLibrary.cpp src/sim/Types.h CMakeLists.txt tests/test_main.cpp
git commit -m "feat: add AntennaLibrary and antennaType field on Antenna"
```

---

## Task 2: PropertiesPanel — Antenna Type Combo Box

**Files:**
- Modify: `src/ui/PropertiesPanel.h`
- Modify: `src/ui/PropertiesPanel.cpp`

- [ ] **Step 1: Update `src/ui/PropertiesPanel.h`**

Add includes and member:

```cpp
#pragma once
#include <QWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include "sim/Types.h"
#include "data/AntennaLibrary.h"

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    // Call once after construction to populate the type combo.
    // Safe to call again if the library reloads (rebuilds combo).
    void setAntennaLibrary(const AntennaLibrary* lib);

    // Populate controls from the given antenna. Pass nullptr to clear/disable.
    void load(const Antenna* antenna);

signals:
    void antennaChanged(Antenna updated);

private slots:
    void onValueChanged();
    void onTypeChanged(int index);

private:
    QLabel*         m_noSelLabel  = nullptr;
    QWidget*        m_form        = nullptr;
    QComboBox*      m_typeCombo   = nullptr;
    QDoubleSpinBox* m_freqSpin    = nullptr;
    QDoubleSpinBox* m_powerSpin   = nullptr;
    QDoubleSpinBox* m_gainSpin    = nullptr;
    QDoubleSpinBox* m_heightSpin  = nullptr;
    QCheckBox*      m_jammerCheck = nullptr;

    const AntennaLibrary* m_lib     = nullptr;
    bool                  m_loading = false;
    Antenna               m_current;

    QDoubleSpinBox* makeSpinBox(double min, double max, double step,
                                const QString& suffix);
};
```

- [ ] **Step 2: Read the current `src/ui/PropertiesPanel.cpp`** (needed before editing)

Read the current file to understand the existing constructor and `load()` body before making changes.

- [ ] **Step 3: Update `src/ui/PropertiesPanel.cpp`**

Add the following at the top of the file:

```cpp
#include "data/AntennaLibrary.h"
```

Add `setAntennaLibrary()` implementation:

```cpp
void PropertiesPanel::setAntennaLibrary(const AntennaLibrary* lib) {
    m_lib = lib;
    if (!lib || lib->types().empty()) return;

    m_loading = true;
    m_typeCombo->clear();
    for (const auto& t : lib->types())
        m_typeCombo->addItem(QString::fromStdString(t.name));
    m_loading = false;
}
```

In the constructor, after the existing `m_form` widget / form layout is created, insert the type row **before** the frequency row. The row should be labeled "Type" and contain `m_typeCombo`:

```cpp
    m_typeCombo = new QComboBox();
    m_typeCombo->setStyleSheet(
        "QComboBox {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 3px;"
        "  color: rgba(255,255,255,0.85);"
        "  font-family: monospace; font-size: 12px;"
        "  padding: 2px 6px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: #1a2a3a;"
        "  color: rgba(255,255,255,0.85);"
        "  selection-background-color: rgba(79,195,247,0.3);"
        "}"
    );
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertiesPanel::onTypeChanged);
    formLayout->addRow("Type", m_typeCombo);
```

(Insert this just before the `formLayout->addRow("Freq (MHz)", m_freqSpin);` call.)

Add `onTypeChanged()` slot implementation:

```cpp
void PropertiesPanel::onTypeChanged(int index) {
    if (m_loading || !m_lib || index < 0) return;
    const auto& types = m_lib->types();
    if (index >= static_cast<int>(types.size())) return;

    const AntennaType& t = types[index];
    m_current.antennaType = t.name;

    // Apply the type's default gain only if the user hasn't customised it
    // (i.e. the gain still matches the previous type's default).
    // For simplicity in v1, always apply the library default on type change.
    m_loading = true;
    m_gainSpin->setValue(static_cast<double>(t.defaultGainDbi));
    m_loading = false;
    m_current.gainDbi = t.defaultGainDbi;

    emit antennaChanged(m_current);
}
```

In `load(const Antenna* antenna)`, after the existing field population block, add type combo sync:

```cpp
    // Sync type combo
    if (m_lib) {
        const std::string& typeName = antenna->antennaType;
        for (int i = 0; i < m_typeCombo->count(); ++i) {
            if (m_typeCombo->itemText(i).toStdString() == typeName) {
                m_typeCombo->setCurrentIndex(i);
                break;
            }
        }
    }
```

(Insert this just before `m_loading = false;` at the end of the populated-antenna branch.)

- [ ] **Step 4: Build the full app — expect zero errors**

```powershell
cmake --build . -j8 2>&1 | Select-String -Pattern "error:"
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/PropertiesPanel.h src/ui/PropertiesPanel.cpp
git commit -m "feat: add antenna type combo box to PropertiesPanel"
```

---

## Task 3: ScenarioIO — JSON Save / Load

**Files:**
- Create: `src/data/ScenarioIO.h`
- Create: `src/data/ScenarioIO.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Write failing tests**

Add include at the top of `tests/test_main.cpp`:

```cpp
#include "data/ScenarioIO.h"
```

Add test functions before `main()`:

```cpp
static void test_scenario_io_hills_round_trip() {
    ScenarioDoc doc;
    doc.terrain.type         = TerrainSource::Type::Hills;
    doc.mode                 = SimEngine::Mode::Full;

    Antenna a;
    a.position       = {100.0f, 15.0f, 200.0f};
    a.frequencyMHz   = 433.0f;
    a.powerW         = 20.0f;
    a.gainDbi        = 6.0f;
    a.isJammer       = false;
    a.antennaType    = "yagi";
    doc.antennas.push_back(a);

    Antenna j;
    j.position       = {500.0f, 10.0f, 500.0f};
    j.frequencyMHz   = 433.0f;
    j.powerW         = 100.0f;
    j.gainDbi        = 3.0f;
    j.isJammer       = true;
    j.antennaType    = "omni";
    doc.antennas.push_back(j);

    const std::string path = "test_scenario_tmp.json";
    ScenarioIO::save(doc, path);

    ScenarioDoc loaded = ScenarioIO::load(path);
    CHECK(loaded.terrain.type == TerrainSource::Type::Hills);
    CHECK(loaded.mode == SimEngine::Mode::Full);
    CHECK(loaded.antennas.size() == 2u);
    CHECK_NEAR(loaded.antennas[0].frequencyMHz, 433.0f, 0.01f);
    CHECK_NEAR(loaded.antennas[0].gainDbi, 6.0f, 0.01f);
    CHECK(loaded.antennas[0].antennaType == "yagi");
    CHECK(loaded.antennas[1].isJammer == true);
    CHECK_NEAR(loaded.antennas[0].position.x, 100.0f, 0.01f);
    CHECK_NEAR(loaded.antennas[0].position.y, 15.0f, 0.01f);
    CHECK_NEAR(loaded.antennas[0].position.z, 200.0f, 0.01f);

    std::remove(path.c_str());
}

static void test_scenario_io_hgt_round_trip() {
    ScenarioDoc doc;
    doc.terrain.type    = TerrainSource::Type::Hgt;
    doc.terrain.hgtPath = "/data/N35W079.hgt";
    doc.terrain.hgtLat  = 35.05f;
    doc.terrain.hgtLon  = -78.88f;
    doc.terrain.hgtSize = 512;
    doc.mode            = SimEngine::Mode::Fast;

    const std::string path = "test_scenario_hgt_tmp.json";
    ScenarioIO::save(doc, path);
    ScenarioDoc loaded = ScenarioIO::load(path);

    CHECK(loaded.terrain.type == TerrainSource::Type::Hgt);
    CHECK(loaded.terrain.hgtPath == "/data/N35W079.hgt");
    CHECK_NEAR(loaded.terrain.hgtLat, 35.05f, 0.001f);
    CHECK_NEAR(loaded.terrain.hgtLon, -78.88f, 0.001f);
    CHECK(loaded.terrain.hgtSize == 512);
    CHECK(loaded.mode == SimEngine::Mode::Fast);

    std::remove(path.c_str());
}
```

Add calls in `main()`:

```cpp
    test_scenario_io_hills_round_trip();
    test_scenario_io_hgt_round_trip();
```

Also add `#include <cstdio>` for `std::remove` if not already present.

- [ ] **Step 2: Run — expect compile failure (ScenarioIO not yet defined)**

```powershell
cmake --build . --target ridgeline_tests -j8 2>&1 | Select-String -Pattern "error:"
```

Expected: compile errors — `ScenarioIO`, `ScenarioDoc`, `TerrainSource` not defined.

- [ ] **Step 3: Create `src/data/ScenarioIO.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include "sim/Types.h"
#include "sim/SimEngine.h"

struct TerrainSource {
    enum class Type { Hills, Flat, Hgt };

    Type        type         = Type::Hills;
    std::string hgtPath;            // only for Hgt
    float       hgtLat       = 35.05f;
    float       hgtLon       = -78.88f;
    int         hgtSize      = 512;

    // Procedural terrain dimensions (Hills + Flat).
    int   widthCells  = 200;
    int   heightCells = 200;
    float cellSize    = 10.0f;
    float amplitude   = 40.0f;  // Hills only; ignored for Flat
};

struct ScenarioDoc {
    TerrainSource        terrain;
    SimEngine::Mode      mode = SimEngine::Mode::Fast;
    std::vector<Antenna> antennas;
};

class ScenarioIO {
public:
    // Serialize doc to a JSON file at path.
    // Throws std::runtime_error on failure.
    static void save(const ScenarioDoc& doc, const std::string& path);

    // Deserialize from a JSON file at path.
    // Throws std::runtime_error on failure.
    static ScenarioDoc load(const std::string& path);
};
```

- [ ] **Step 4: Create `src/data/ScenarioIO.cpp`**

```cpp
#include "ScenarioIO.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

// ── terrain type tag strings ──────────────────────────────────────────────────
static const char* typeToStr(TerrainSource::Type t) {
    switch (t) {
        case TerrainSource::Type::Hills: return "hills";
        case TerrainSource::Type::Flat:  return "flat";
        case TerrainSource::Type::Hgt:   return "hgt";
    }
    return "hills";
}

static TerrainSource::Type strToType(const std::string& s) {
    if (s == "flat") return TerrainSource::Type::Flat;
    if (s == "hgt")  return TerrainSource::Type::Hgt;
    return TerrainSource::Type::Hills;
}

// ── save ──────────────────────────────────────────────────────────────────────
void ScenarioIO::save(const ScenarioDoc& doc, const std::string& path) {
    json j;
    j["version"] = 1;
    j["mode"]    = (doc.mode == SimEngine::Mode::Fast) ? "fast" : "full";

    auto& jt = j["terrain"];
    jt["type"]         = typeToStr(doc.terrain.type);
    jt["width_cells"]  = doc.terrain.widthCells;
    jt["height_cells"] = doc.terrain.heightCells;
    jt["cell_size"]    = doc.terrain.cellSize;
    jt["amplitude"]    = doc.terrain.amplitude;
    if (doc.terrain.type == TerrainSource::Type::Hgt) {
        jt["hgt_path"] = doc.terrain.hgtPath;
        jt["hgt_lat"]  = doc.terrain.hgtLat;
        jt["hgt_lon"]  = doc.terrain.hgtLon;
        jt["hgt_size"] = doc.terrain.hgtSize;
    }

    json antennas = json::array();
    for (const auto& a : doc.antennas) {
        json ja;
        ja["x"]            = a.position.x;
        ja["y"]            = a.position.y;
        ja["z"]            = a.position.z;
        ja["freq_mhz"]     = a.frequencyMHz;
        ja["power_w"]      = a.powerW;
        ja["gain_dbi"]     = a.gainDbi;
        ja["is_jammer"]    = a.isJammer;
        ja["antenna_type"] = a.antennaType;
        antennas.push_back(std::move(ja));
    }
    j["antennas"] = std::move(antennas);

    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write scenario file: " + path);
    f << j.dump(2);
}

// ── load ──────────────────────────────────────────────────────────────────────
ScenarioDoc ScenarioIO::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open scenario file: " + path);

    ScenarioDoc doc;
    try {
        json j = json::parse(f);

        const std::string modeStr = j.value("mode", "fast");
        doc.mode = (modeStr == "full") ? SimEngine::Mode::Full : SimEngine::Mode::Fast;

        if (j.contains("terrain")) {
            const auto& jt    = j["terrain"];
            doc.terrain.type  = strToType(jt.value("type", "hills"));
            doc.terrain.widthCells  = jt.value("width_cells",  200);
            doc.terrain.heightCells = jt.value("height_cells", 200);
            doc.terrain.cellSize    = jt.value("cell_size",   10.0f);
            doc.terrain.amplitude   = jt.value("amplitude",   40.0f);
            if (doc.terrain.type == TerrainSource::Type::Hgt) {
                doc.terrain.hgtPath = jt.value("hgt_path", std::string{});
                doc.terrain.hgtLat  = jt.value("hgt_lat",  35.05f);
                doc.terrain.hgtLon  = jt.value("hgt_lon", -78.88f);
                doc.terrain.hgtSize = jt.value("hgt_size",  512);
            }
        }

        if (j.contains("antennas")) {
            for (const auto& ja : j["antennas"]) {
                Antenna a;
                a.position.x   = ja.value("x",            0.0f);
                a.position.y   = ja.value("y",            0.0f);
                a.position.z   = ja.value("z",            0.0f);
                a.frequencyMHz = ja.value("freq_mhz",   150.0f);
                a.powerW       = ja.value("power_w",     50.0f);
                a.gainDbi      = ja.value("gain_dbi",     3.0f);
                a.isJammer     = ja.value("is_jammer",   false);
                a.antennaType  = ja.value("antenna_type", std::string{"omni"});
                doc.antennas.push_back(std::move(a));
            }
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Scenario parse error: ") + e.what());
    }
    return doc;
}
```

- [ ] **Step 5: Add ScenarioIO.cpp to the test target in `CMakeLists.txt`**

```cmake
add_executable(ridgeline_tests
    tests/test_main.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/FastSolver.cpp
    src/sim/FullSolver.cpp
    src/sim/SimEngine.cpp
    src/data/AntennaLibrary.cpp
    src/data/ScenarioIO.cpp
)
```

- [ ] **Step 6: Build and run tests — expect all pass**

```powershell
cmake .. -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/mingw_64" `
  -DCMAKE_BUILD_TYPE=Release
cmake --build . --target ridgeline_tests -j8
.\ridgeline_tests.exe assets/materials.json
```

Expected output ends with e.g. `15 passed, 0 failed.`

- [ ] **Step 7: Commit**

```bash
git add src/data/ScenarioIO.h src/data/ScenarioIO.cpp CMakeLists.txt tests/test_main.cpp
git commit -m "feat: add ScenarioIO JSON save/load with round-trip tests"
```

---

## Task 4: Exporter — PNG Coverage Map + CSV

**Files:**
- Create: `src/data/Exporter.h`
- Create: `src/data/Exporter.cpp`

> Note: Exporter uses `QImage` (Qt) and is compiled into the main binary automatically via `GLOB_RECURSE`. It is **not** added to the test binary.

- [ ] **Step 1: Create `src/data/Exporter.h`**

```cpp
#pragma once
#include <string>
#include "sim/Types.h"

// Static helpers to export a FieldGrid to file.
// Both methods return true on success, false on write failure.
class Exporter {
public:
    // Write a false-color PNG where strong signal (high dBm) = green,
    // medium = yellow, weak = red, NaN cells = transparent black.
    // Requires a valid (non-empty) FieldGrid.
    static bool exportPng(const FieldGrid& grid, const std::string& path);

    // Write a CSV file: header row "x_m,z_m,dbm" followed by one row per cell.
    // NaN cells are written as the literal string "nan".
    static bool exportCsv(const FieldGrid& grid, const std::string& path);
};
```

- [ ] **Step 2: Create `src/data/Exporter.cpp`**

```cpp
#include "Exporter.h"
#include <QImage>
#include <QString>
#include <fstream>
#include <cmath>
#include <algorithm>

// ── colormap ─────────────────────────────────────────────────────────────────
// Maps t in [0,1] (0 = weak, 1 = strong) to RGB.
// 0.0 = red, 0.5 = yellow, 1.0 = green.
static void colormapRgb(float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    t = std::max(0.0f, std::min(1.0f, t));
    if (t < 0.5f) {
        // red → yellow
        float s = t * 2.0f;
        r = 255;
        g = static_cast<uint8_t>(255.0f * s);
        b = 0;
    } else {
        // yellow → green
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(255.0f * (1.0f - s));
        g = 255;
        b = 0;
    }
}

// ── PNG export ────────────────────────────────────────────────────────────────
bool Exporter::exportPng(const FieldGrid& grid, const std::string& path) {
    if (!grid.valid()) return false;

    // Find finite min/max for normalization.
    float minDbm =  1e30f;
    float maxDbm = -1e30f;
    for (float v : grid.dbm) {
        if (std::isfinite(v)) {
            minDbm = std::min(minDbm, v);
            maxDbm = std::max(maxDbm, v);
        }
    }
    const float range = (maxDbm > minDbm) ? (maxDbm - minDbm) : 1.0f;

    QImage img(grid.width, grid.height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            const float v = grid.at(x, y);
            if (!std::isfinite(v)) continue;
            uint8_t r, g, b;
            colormapRgb((v - minDbm) / range, r, g, b);
            img.setPixel(x, y, qRgba(r, g, b, 255));
        }
    }

    return img.save(QString::fromStdString(path));
}

// ── CSV export ────────────────────────────────────────────────────────────────
bool Exporter::exportCsv(const FieldGrid& grid, const std::string& path) {
    if (!grid.valid()) return false;

    std::ofstream f(path);
    if (!f) return false;

    f << "x_m,z_m,dbm\n";
    for (int y = 0; y < grid.height; ++y) {
        const float z_m = grid.originXZ.y + y * grid.cellSize;
        for (int x = 0; x < grid.width; ++x) {
            const float x_m = grid.originXZ.x + x * grid.cellSize;
            const float v   = grid.at(x, y);
            f << x_m << ',' << z_m << ',';
            if (std::isfinite(v)) f << v;
            else                  f << "nan";
            f << '\n';
        }
    }
    return f.good();
}
```

- [ ] **Step 3: Build the full app — expect zero errors**

```powershell
cmake --build . -j8 2>&1 | Select-String -Pattern "error:"
```

Expected: no errors. (Exporter.cpp is picked up automatically by `GLOB_RECURSE`.)

- [ ] **Step 4: Commit**

```bash
git add src/data/Exporter.h src/data/Exporter.cpp
git commit -m "feat: add Exporter — PNG colormap and CSV coverage export"
```

---

## Task 5: Wire into MainWindow — File and Export Menus

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Update `src/ui/MainWindow.h`**

Add new includes and members:

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
    TerrainSource    m_terrainSource;   // tracks what is currently loaded
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
    void refreshTree();
    void refreshPropsPanel();
    void applyScenarioDoc(const ScenarioDoc& doc);   // shared by Open + load
};
```

- [ ] **Step 2: Update `src/ui/MainWindow.cpp`**

**2a — New includes** at the top of the file:

```cpp
#include "data/ScenarioIO.h"
#include "data/Exporter.h"
#include "data/TerrainLoader.h"
#include <QStandardPaths>
```

**2b — Load antenna library in the constructor**, just before `setupScene()`:

```cpp
    try {
        m_antennaLib.load("assets/antennas.json");
    } catch (const std::exception&) {
        // Use built-in defaults silently — non-fatal.
        m_antennaLib = AntennaLibrary::defaults();  // copy from static
    }
```

> `AntennaLibrary::defaults()` returns a `const AntennaLibrary&` so we need to copy it. Add a defaulted copy constructor — or simply expose a copy: since `AntennaLibrary` contains only `std::vector<AntennaType>`, the compiler-generated copy constructor works fine. The load() path above already populates `m_antennaLib` directly; only the catch branch needs the copy from defaults. Implement it as:

```cpp
    } catch (const std::exception&) {
        for (const auto& t : AntennaLibrary::defaults().types())
            m_antennaLib.load_type_for_fallback(t);  // NOT this — just call load() with
                                                       // a path that won't exist, catch, done.
    }
```

Actually, the cleanest way without adding any new API: try to load, catch, and simply skip (the combo will start empty; it will still work). Or add a public method. To avoid touching AntennaLibrary, use:

```cpp
    // Load antenna library; silently fall back to four built-in types on failure.
    try {
        m_antennaLib.load("assets/antennas.json");
    } catch (...) {
        // Populate from hard-coded defaults so PropertiesPanel always has types.
        m_antennaLib.load_defaults_fallback();
    }
```

This requires adding one method to AntennaLibrary. The simplest approach: expose the fallback as a `loadDefaults()` method that just calls `m_types = defaults().types()` internally. Add to `AntennaLibrary.h`:

```cpp
    // Populate from built-in defaults without reading any file.
    void loadDefaults();
```

And to `AntennaLibrary.cpp`:

```cpp
void AntennaLibrary::loadDefaults() {
    m_types = defaults().types();
}
```

Then in the constructor:

```cpp
    try {
        m_antennaLib.load("assets/antennas.json");
    } catch (...) {
        m_antennaLib.loadDefaults();
    }
```

**2c — Wire PropertiesPanel to library**, just after `m_propsPanel = new PropertiesPanel(this);`:

```cpp
    m_propsPanel->setAntennaLibrary(&m_antennaLib);
```

**2d — Track terrain source in `setupScene()`**. After `TerrainModel terrain = TerrainLoader::makeHills(...)`:

```cpp
    m_terrainSource = TerrainSource{};   // default-constructed = Type::Hills, 200x200, 10m, 40m
```

**2e — Track terrain source in `loadTerrain()`**. Inside the try-block after the terrain is loaded:

```cpp
        if (ext == "hgt") {
            m_terrainSource = TerrainSource{};
            m_terrainSource.type    = TerrainSource::Type::Hgt;
            m_terrainSource.hgtPath = path.toStdString();
            // Keep default lat/lon/size — the user accepted the crop defaults.
            // In the future this could be extracted from the file dialog filter.
        } else {
            m_terrainSource = TerrainSource{};
            m_terrainSource.type = TerrainSource::Type::Flat;
        }
```

**2f — Add File menu items** to the existing `fileMenu` block in the constructor:

```cpp
    fileMenu->addSeparator();
    QAction* openScenAct = fileMenu->addAction("&Open Scenario…");
    openScenAct->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openScenAct, &QAction::triggered, this, &MainWindow::onOpenScenario);

    QAction* saveScenAct = fileMenu->addAction("&Save Scenario…");
    saveScenAct->setShortcut(QKeySequence::Save);
    connect(saveScenAct, &QAction::triggered, this, &MainWindow::onSaveScenario);

    // Export submenu
    QMenu* exportMenu = fileMenu->addMenu("&Export");
    QAction* exportPngAct = exportMenu->addAction("Coverage &PNG…");
    connect(exportPngAct, &QAction::triggered, this, &MainWindow::onExportPng);
    QAction* exportCsvAct = exportMenu->addAction("Coverage &CSV…");
    connect(exportCsvAct, &QAction::triggered, this, &MainWindow::onExportCsv);
```

**2g — Add new slot implementations**:

```cpp
void MainWindow::onSaveScenario() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Scenario", {},
        "Ridgeline scenario (*.json);;All files (*)");
    if (path.isEmpty()) return;

    ScenarioDoc doc;
    doc.terrain  = m_terrainSource;
    doc.mode     = m_engine.mode;
    doc.antennas = m_engine.antennas();  // copies const& into vector

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
    m_terrainSource = doc.terrain;

    // Rebuild terrain from source descriptor.
    TerrainModel t;
    const auto& ts = doc.terrain;
    switch (ts.type) {
        case TerrainSource::Type::Hgt:
            t = TerrainLoader::fromHGT(ts.hgtPath, ts.hgtLat, ts.hgtLon, ts.hgtSize);
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

    m_engine.mode = doc.mode;
    m_modeCombo->setCurrentIndex(doc.mode == SimEngine::Mode::Fast ? 0 : 1);
    m_engine.setAntennas(doc.antennas);

    m_selectedIndex = -1;
    if (m_viewport) m_viewport->reloadTerrain();
    refreshTree();
    refreshPropsPanel();
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
```

- [ ] **Step 3: Add `loadDefaults()` to AntennaLibrary**

In `src/data/AntennaLibrary.h`, add:

```cpp
    // Populate from built-in defaults without reading any file.
    void loadDefaults();
```

In `src/data/AntennaLibrary.cpp`, add:

```cpp
void AntennaLibrary::loadDefaults() {
    m_types = defaults().types();
}
```

- [ ] **Step 4: Build full app — expect zero errors**

```powershell
cmake --build . -j8 2>&1 | Select-String -Pattern "error:"
```

Expected: no errors.

- [ ] **Step 5: Smoke test**

Launch the app:

```powershell
.\ridgeline.exe
```

Verify:
1. File menu shows: Load Terrain, separator, Open Scenario, Save Scenario, Export ▸ (Coverage PNG, Coverage CSV)
2. PropertiesPanel shows "Type" combo with omni/dipole/yagi/patch when an antenna is selected
3. Solve runs; then Export → Coverage PNG → save a file → file appears on disk
4. Save Scenario → saves a `.json` → open with a text editor to confirm readable JSON
5. Open Scenario with the saved file → scene restores

- [ ] **Step 6: Run test suite — confirm no regressions**

```powershell
.\ridgeline_tests.exe assets/materials.json
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp src/data/AntennaLibrary.h src/data/AntennaLibrary.cpp
git commit -m "feat: wire AntennaLibrary, ScenarioIO, and Exporter into MainWindow"
```

---

## Self-Review

**Spec coverage:**
- AntennaLibrary JSON-loaded gain patterns ✅ Task 1
- Antenna type picker in PropertiesPanel ✅ Task 2
- ScenarioIO JSON save ✅ Task 3
- ScenarioIO JSON load ✅ Task 3
- PNG coverage export ✅ Task 4
- CSV coverage export ✅ Task 4
- Wire into MainWindow File/Export menus ✅ Task 5
- Tests for AntennaLibrary ✅ Task 1
- Tests for ScenarioIO round-trip ✅ Task 3

**Placeholder scan:** None found. All code blocks are complete.

**Type consistency:**
- `TerrainSource` defined in `ScenarioIO.h`, used in `MainWindow.h` — consistent.
- `AntennaLibrary::loadDefaults()` added in Task 5 Step 3 and referenced in Task 5 Step 2 — consistent.
- `Antenna::antennaType` field added in Task 1 Step 6, serialized in ScenarioIO Task 4, populated in PropertiesPanel Task 2 — consistent.
- `FieldGrid::originXZ` is `glm::vec2`; accessed as `.x` and `.y` in Exporter.cpp — matches `Types.h` definition.
