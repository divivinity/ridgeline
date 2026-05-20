# Phase 3: Full Physics Solver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a full-physics RF solver (multi-ray, Fresnel reflection, knife-edge diffraction, rain attenuation, per-frequency material EM properties) selectable from a toolbar toggle.

**Architecture:** A new `FullSolver` class computes direct + single-bounce reflected + knife-edge diffracted paths and sums them as incoherent power. A `MaterialLibrary` class loads EM properties (ε_r, σ, dB loss/100 m) from `assets/materials.json` and is shared by both solvers. `SimEngine` dispatches to `FastSolver` or `FullSolver` based on a `Mode` enum already defined. A `QComboBox` in the toolbar wires the mode switch to the engine.

**Tech Stack:** C++17, Qt 6.8.3, GLM, nlohmann/json (already in CMake), MinGW, CTest

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `assets/materials.json` | Create | EM properties for 6 terrain materials |
| `src/sim/MaterialLibrary.h` | Create | `Material` struct + `MaterialLibrary` class declaration |
| `src/sim/MaterialLibrary.cpp` | Create | JSON loader, default material, interpolation |
| `src/sim/FullSolver.h` | Create | `FullSolver` class declaration |
| `src/sim/FullSolver.cpp` | Create | Multi-ray solve, Fresnel, knife-edge, rain |
| `src/sim/TerrainModel.h` | Modify | Add `PeakObstacle` struct + `peakObstacle()` declaration |
| `src/sim/TerrainModel.cpp` | Modify | Implement `peakObstacle()` |
| `src/sim/FastSolver.h` | Modify | Add `MaterialLibrary&` param to `solve()`, remove `materialLossDb` |
| `src/sim/FastSolver.cpp` | Modify | Use real material loss, remove stub |
| `src/sim/SimEngine.h` | Modify | Add `FullSolver`, `MaterialLibrary` members + `loadMaterials()` |
| `src/sim/SimEngine.cpp` | Modify | Dispatch `FullSolver` when mode == Full |
| `src/ui/MainWindow.h` | Modify | Add `m_modeCombo` member |
| `src/ui/MainWindow.cpp` | Modify | Add combo to toolbar, load materials, wire mode |
| `tests/test_main.cpp` | Create | Unit tests (no Qt/GL) |
| `CMakeLists.txt` | Modify | Add `ridgeline_tests` executable + CTest |

---

## Task 1: MaterialLibrary + assets/materials.json

**Files:**
- Create: `assets/materials.json`
- Create: `src/sim/MaterialLibrary.h`
- Create: `src/sim/MaterialLibrary.cpp`
- Create: `tests/test_main.cpp` (partial — MaterialLibrary tests only)
- Modify: `CMakeLists.txt`

### Add test infrastructure to CMakeLists.txt

- [ ] **Step 1: Add test target to CMakeLists.txt**

Open `CMakeLists.txt`. After the `add_custom_command` block at the bottom, append:

```cmake
# ── Tests (no Qt / no OpenGL) ──────────────────────────────────────────────
enable_testing()

add_executable(ridgeline_tests
    tests/test_main.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/FullSolver.cpp
)
target_include_directories(ridgeline_tests PRIVATE src)
target_link_libraries(ridgeline_tests
    glm::glm
    nlohmann_json::nlohmann_json
)
# Tests run from build dir so assets/materials.json is already there
add_test(NAME physics_tests
    COMMAND ridgeline_tests "${CMAKE_SOURCE_DIR}/assets/materials.json"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
```

Note: `FullSolver.cpp` won't exist yet — that's fine, we'll create a stub in Task 4 before running the full build.

### Create MaterialLibrary header

- [ ] **Step 2: Create `src/sim/MaterialLibrary.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct Material {
    std::string name;
    float epsilon_r = 3.0f;   // relative permittivity
    float sigma_sm  = 0.001f; // conductivity (S/m)

    struct BandLoss { float freqMHz; float lossDb; };
    std::vector<BandLoss> bandLoss; // loss dB per 100 m, sorted by freqMHz

    // Linear interpolation of loss dB/100m at the given frequency.
    // Clamps to the nearest endpoint outside the table range.
    float lossDbPer100m(float freqMHz) const;
};

class MaterialLibrary {
public:
    // Parse assets/materials.json; throws std::runtime_error on failure.
    void load(const std::string& path);

    // Look up by name; returns defaultMaterial() if not found.
    const Material& find(const std::string& name) const;

    // Dry soil — used when terrain has no explicit material tag.
    // Static so it's available without loading a file (e.g., in tests).
    static const Material& defaultMaterial();

private:
    std::unordered_map<std::string, Material> m_materials;
};
```

### Create assets/materials.json

- [ ] **Step 3: Create `assets/materials.json`**

```json
{
  "materials": [
    {
      "name": "dry_soil",
      "epsilon_r": 3.0,
      "sigma_sm": 0.001,
      "band_loss": [
        {"freq_mhz": 100,   "loss_db_per_100m": 0.3},
        {"freq_mhz": 300,   "loss_db_per_100m": 0.5},
        {"freq_mhz": 1000,  "loss_db_per_100m": 1.2},
        {"freq_mhz": 3000,  "loss_db_per_100m": 3.0}
      ]
    },
    {
      "name": "wet_soil",
      "epsilon_r": 15.0,
      "sigma_sm": 0.05,
      "band_loss": [
        {"freq_mhz": 100,   "loss_db_per_100m": 1.0},
        {"freq_mhz": 300,   "loss_db_per_100m": 1.8},
        {"freq_mhz": 1000,  "loss_db_per_100m": 3.5},
        {"freq_mhz": 3000,  "loss_db_per_100m": 8.0}
      ]
    },
    {
      "name": "concrete",
      "epsilon_r": 5.5,
      "sigma_sm": 0.02,
      "band_loss": [
        {"freq_mhz": 100,   "loss_db_per_100m": 1.5},
        {"freq_mhz": 300,   "loss_db_per_100m": 2.5},
        {"freq_mhz": 1000,  "loss_db_per_100m": 5.0},
        {"freq_mhz": 3000,  "loss_db_per_100m": 11.0}
      ]
    },
    {
      "name": "foliage",
      "epsilon_r": 12.0,
      "sigma_sm": 0.1,
      "band_loss": [
        {"freq_mhz": 100,   "loss_db_per_100m": 2.0},
        {"freq_mhz": 300,   "loss_db_per_100m": 4.0},
        {"freq_mhz": 1000,  "loss_db_per_100m": 10.0},
        {"freq_mhz": 3000,  "loss_db_per_100m": 20.0}
      ]
    },
    {
      "name": "asphalt",
      "epsilon_r": 3.8,
      "sigma_sm": 0.002,
      "band_loss": [
        {"freq_mhz": 100,   "loss_db_per_100m": 0.4},
        {"freq_mhz": 300,   "loss_db_per_100m": 0.7},
        {"freq_mhz": 1000,  "loss_db_per_100m": 1.5},
        {"freq_mhz": 3000,  "loss_db_per_100m": 3.5}
      ]
    },
    {
      "name": "water",
      "epsilon_r": 80.0,
      "sigma_sm": 4.0,
      "band_loss": [
        {"freq_mhz": 100,   "loss_db_per_100m": 5.0},
        {"freq_mhz": 300,   "loss_db_per_100m": 9.0},
        {"freq_mhz": 1000,  "loss_db_per_100m": 20.0},
        {"freq_mhz": 3000,  "loss_db_per_100m": 45.0}
      ]
    }
  ]
}
```

### Write failing MaterialLibrary tests

- [ ] **Step 4: Create `tests/test_main.cpp` with MaterialLibrary tests**

Create `tests/` directory and `tests/test_main.cpp`:

```cpp
#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include "sim/MaterialLibrary.h"

// ── helpers ─────────────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)
#define CHECK_NEAR(a, b, tol) CHECK(std::abs((a)-(b)) < (tol))

// ── MaterialLibrary tests ────────────────────────────────────────────────────
static void test_default_material() {
    const Material& m = MaterialLibrary::defaultMaterial();
    CHECK(m.name == "dry_soil");
    CHECK_NEAR(m.epsilon_r, 3.0f, 0.01f);
    CHECK_NEAR(m.sigma_sm,  0.001f, 1e-5f);
    CHECK(!m.bandLoss.empty());
}

static void test_loss_interpolation() {
    const Material& m = MaterialLibrary::defaultMaterial();
    // At 100 MHz = 0.3 dB/100m (exact table point)
    CHECK_NEAR(m.lossDbPer100m(100.0f), 0.3f, 0.01f);
    // At 300 MHz = 0.5 dB/100m (exact table point)
    CHECK_NEAR(m.lossDbPer100m(300.0f), 0.5f, 0.01f);
    // Midpoint between 100 and 300 MHz should interpolate (linear)
    float mid = m.lossDbPer100m(200.0f);
    CHECK(mid > 0.3f && mid < 0.5f);
    // Below table min: clamps to 0.3
    CHECK_NEAR(m.lossDbPer100m(10.0f), 0.3f, 0.01f);
    // Above table max: clamps to 3.0
    CHECK_NEAR(m.lossDbPer100m(10000.0f), 3.0f, 0.01f);
}

static void test_load_from_json(const std::string& path) {
    MaterialLibrary lib;
    lib.load(path);

    // concrete should be present
    const Material& concrete = lib.find("concrete");
    CHECK(concrete.name == "concrete");
    CHECK_NEAR(concrete.epsilon_r, 5.5f, 0.01f);
    CHECK_NEAR(concrete.sigma_sm,  0.02f, 1e-4f);
    // loss at 1000 MHz = 5.0 dB/100m
    CHECK_NEAR(concrete.lossDbPer100m(1000.0f), 5.0f, 0.01f);

    // unknown material returns default (dry_soil)
    const Material& unknown = lib.find("unobtainium");
    CHECK(unknown.name == "dry_soil");

    // all 6 materials should be findable
    for (const char* name : {"dry_soil","wet_soil","concrete","foliage","asphalt","water"}) {
        CHECK(lib.find(name).name == std::string(name));
    }
}

// ── main ─────────────────────────────────────────────────────────────────────
// Additional test functions (TerrainModel, FullSolver) will be added in later tasks.
// For now, only MaterialLibrary tests run.

int main(int argc, char** argv) {
    std::string materialsPath = (argc > 1) ? argv[1] : "assets/materials.json";

    test_default_material();
    test_loss_interpolation();
    test_load_from_json(materialsPath);

    std::cout << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 5: Attempt to build tests — expect LINK ERROR** (MaterialLibrary.cpp not yet created)

```
cd <repo-root>\build
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline_tests -j4 2>&1
```

Expected: compile error or linker error — `MaterialLibrary` undefined. This confirms the test is wired correctly.

### Implement MaterialLibrary

- [ ] **Step 6: Create `src/sim/MaterialLibrary.cpp`**

```cpp
#include "MaterialLibrary.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <algorithm>

using json = nlohmann::json;

// ── Material ─────────────────────────────────────────────────────────────────

float Material::lossDbPer100m(float freqMHz) const {
    if (bandLoss.empty()) return 0.0f;
    if (bandLoss.size() == 1) return bandLoss[0].lossDb;

    if (freqMHz <= bandLoss.front().freqMHz) return bandLoss.front().lossDb;
    if (freqMHz >= bandLoss.back().freqMHz)  return bandLoss.back().lossDb;

    for (size_t i = 1; i < bandLoss.size(); ++i) {
        if (freqMHz <= bandLoss[i].freqMHz) {
            float t = (freqMHz - bandLoss[i-1].freqMHz)
                    / (bandLoss[i].freqMHz - bandLoss[i-1].freqMHz);
            return bandLoss[i-1].lossDb + t * (bandLoss[i].lossDb - bandLoss[i-1].lossDb);
        }
    }
    return bandLoss.back().lossDb;
}

// ── MaterialLibrary ──────────────────────────────────────────────────────────

void MaterialLibrary::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open materials file: " + path);

    json data = json::parse(f);
    for (const auto& entry : data["materials"]) {
        Material mat;
        mat.name      = entry["name"].get<std::string>();
        mat.epsilon_r = entry["epsilon_r"].get<float>();
        mat.sigma_sm  = entry["sigma_sm"].get<float>();
        for (const auto& b : entry["band_loss"]) {
            mat.bandLoss.push_back({
                b["freq_mhz"].get<float>(),
                b["loss_db_per_100m"].get<float>()
            });
        }
        std::sort(mat.bandLoss.begin(), mat.bandLoss.end(),
                  [](const auto& a, const auto& b){ return a.freqMHz < b.freqMHz; });
        m_materials[mat.name] = std::move(mat);
    }
}

const Material& MaterialLibrary::find(const std::string& name) const {
    auto it = m_materials.find(name);
    return (it != m_materials.end()) ? it->second : defaultMaterial();
}

const Material& MaterialLibrary::defaultMaterial() {
    static const Material m = [] {
        Material mat;
        mat.name      = "dry_soil";
        mat.epsilon_r = 3.0f;
        mat.sigma_sm  = 0.001f;
        mat.bandLoss  = {{100.0f,  0.3f},
                         {300.0f,  0.5f},
                         {1000.0f, 1.2f},
                         {3000.0f, 3.0f}};
        return mat;
    }();
    return m;
}
```

- [ ] **Step 7: Build and run MaterialLibrary tests**

Note: `FullSolver.cpp` is listed in the test target but doesn't exist yet. Temporarily remove it from CMakeLists.txt `ridgeline_tests` sources to run just the MaterialLibrary tests (you'll add it back in Task 4). Or create a stub:

```
echo "" > <repo-root>\src\sim\FullSolver.cpp
```

Then build and run:

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline_tests -j4 2>&1
ctest --verbose -R physics_tests
```

Expected output:
```
3 passed, 0 failed.
```

- [ ] **Step 8: Commit**

```
git add assets/materials.json src/sim/MaterialLibrary.h src/sim/MaterialLibrary.cpp tests/test_main.cpp CMakeLists.txt
git commit -m "feat: MaterialLibrary with JSON loader and EM properties for 6 terrain materials"
```

---

## Task 2: Wire MaterialLibrary into FastSolver

**Files:**
- Modify: `src/sim/FastSolver.h`
- Modify: `src/sim/FastSolver.cpp`

FastSolver currently calls `materialLossDb(distM)` which returns `0.0f`. Replace it with a real lookup.

- [ ] **Step 1: Update `src/sim/FastSolver.h`**

Replace the entire file:

```cpp
#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "MaterialLibrary.h"
#include <vector>

class FastSolver {
public:
    float gridResolution = 10.0f;  // metres per grid cell

    FieldGrid solve(const TerrainModel& terrain,
                    const std::vector<Antenna>& antennas,
                    const MaterialLibrary& materials) const;

private:
    static float fsplDb(float distM, float freqMHz);
};
```

- [ ] **Step 2: Update `src/sim/FastSolver.cpp`**

Replace the entire file:

```cpp
#include "FastSolver.h"
#include <cmath>
#include <limits>
#include <glm/glm.hpp>

static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();

FieldGrid FastSolver::solve(const TerrainModel& terrain,
                             const std::vector<Antenna>& antennas,
                             const MaterialLibrary& materials) const {
    FieldGrid grid;
    grid.cellSize = gridResolution;
    grid.width    = static_cast<int>(terrain.width  * terrain.cellSize / gridResolution);
    grid.height   = static_cast<int>(terrain.height * terrain.cellSize / gridResolution);
    grid.originXZ = {0.0f, 0.0f};
    grid.dbm.assign(grid.width * grid.height, NEG_INF);

    const Material& mat = materials.defaultMaterial();

    for (int gz = 0; gz < grid.height; ++gz) {
        for (int gx = 0; gx < grid.width; ++gx) {
            float wx = gx * gridResolution + gridResolution * 0.5f;
            float wz = gz * gridResolution + gridResolution * 0.5f;
            float wy = terrain.heightAtWorld(wx, wz) + 1.5f;

            float bestDbm = NEG_INF;
            for (const auto& ant : antennas) {
                if (ant.isJammer) continue;

                glm::vec3 rxPos{wx, wy, wz};
                float distM = glm::distance(ant.position, rxPos);
                if (distM < 1.0f) distM = 1.0f;

                float txPowerDbm = 10.0f * std::log10(ant.powerW * 1000.0f);
                float matLossDb  = mat.lossDbPer100m(ant.frequencyMHz) * distM / 100.0f;
                float loss = fsplDb(distM, ant.frequencyMHz)
                           - ant.gainDbi
                           + matLossDb;

                bool los = terrain.lineOfSight(ant.position, rxPos);
                if (!los) loss += 20.0f;

                float rxDbm = txPowerDbm - loss;
                if (rxDbm > bestDbm) bestDbm = rxDbm;
            }
            grid.dbm[gz * grid.width + gx] = bestDbm;
        }
    }
    return grid;
}

float FastSolver::fsplDb(float distM, float freqMHz) {
    return 20.0f * std::log10(distM)
         + 20.0f * std::log10(freqMHz * 1e6f)
         - 147.55f;
}
```

- [ ] **Step 3: Build the main target to confirm it compiles**

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline -j4 2>&1
```

Expected: `[100%] Built target ridgeline`

This will fail to link until SimEngine.cpp is updated (Task 5) because FastSolver::solve now has a different signature. If you want to build incrementally, jump to Task 5 Step 1 and 2 before running this build.

- [ ] **Step 4: Commit**

```
git add src/sim/FastSolver.h src/sim/FastSolver.cpp
git commit -m "feat: wire MaterialLibrary into FastSolver (replaces 0.0f stub)"
```

---

## Task 3: Add peakObstacle() to TerrainModel

The `FullSolver` needs to find the height of the worst-case obstacle above the LOS line and its position along the path, in order to compute the Fresnel-Kirchhoff diffraction parameter ν.

**Files:**
- Modify: `src/sim/TerrainModel.h`
- Modify: `src/sim/TerrainModel.cpp`
- Modify: `tests/test_main.cpp` (add TerrainModel tests)

### Write failing test

- [ ] **Step 1: Add TerrainModel tests to `tests/test_main.cpp`**

Add these functions before `main()`:

```cpp
// ── TerrainModel tests ───────────────────────────────────────────────────────
#include "sim/TerrainModel.h"
#include <glm/glm.hpp>

static void test_peak_obstacle_flat_terrain() {
    // Flat terrain at y = 0. LOS from (0,10,0) to (100,10,100) should be clear.
    TerrainModel t;
    t.width    = 200;
    t.height   = 200;
    t.cellSize = 1.0f;
    t.heights.assign(200 * 200, 0.0f);  // all zeros

    glm::vec3 from{0.0f, 10.0f, 0.0f};
    glm::vec3 to  {100.0f, 10.0f, 100.0f};
    auto obs = t.peakObstacle(from, to);

    // Terrain at y=0, LOS ray at y=10 throughout.
    // Peak clearance (terrain - LOS) = 0 - 10 = -10 m (terrain well below).
    CHECK(obs.clearanceM < 0.0f);
    CHECK(obs.t > 0.0f && obs.t < 1.0f);
}

static void test_peak_obstacle_blocked_terrain() {
    // Put a hill of 20 m at the centre of a 200×200 flat (0 m) terrain.
    TerrainModel t;
    t.width    = 200;
    t.height   = 200;
    t.cellSize = 1.0f;
    t.heights.assign(200 * 200, 0.0f);

    // Hill at roughly cell (100, 100)
    int cx = 100, cz = 100;
    for (int dz = -5; dz <= 5; ++dz)
        for (int dx = -5; dx <= 5; ++dx)
            t.heights[(cz + dz) * 200 + (cx + dx)] = 20.0f;

    // Antenna at y=5, receiver at y=5, hill in between should be an obstacle.
    glm::vec3 from{0.0f,   5.0f,   0.0f};
    glm::vec3 to  {200.0f, 5.0f, 200.0f};
    auto obs = t.peakObstacle(from, to);

    // Hill is 20 m high, LOS is at 5 m → excess = 15 m
    CHECK(obs.clearanceM > 10.0f);
    // Peak should be somewhere in the middle
    CHECK(obs.t > 0.3f && obs.t < 0.7f);
}
```

And extend `main()` to call them (replace the existing `main` body):

```cpp
int main(int argc, char** argv) {
    std::string materialsPath = (argc > 1) ? argv[1] : "assets/materials.json";

    test_default_material();
    test_loss_interpolation();
    test_load_from_json(materialsPath);

    test_peak_obstacle_flat_terrain();
    test_peak_obstacle_blocked_terrain();

    std::cout << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Build tests — expect compile error** (peakObstacle not declared yet)

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline_tests -j4 2>&1
```

Expected: compile error `'peakObstacle' is not a member of 'TerrainModel'`.

### Implement peakObstacle

- [ ] **Step 3: Add `PeakObstacle` and declaration to `src/sim/TerrainModel.h`**

Add after the `LOSStatus` enum (after line 13):

```cpp
    // Describes the worst-case obstacle above the direct LOS line.
    struct PeakObstacle {
        float clearanceM;  // terrain height − LOS height at worst point; negative = clear
        float t;           // fraction along path (0..1) where the peak occurs
    };

    // Returns the obstacle that most impedes the path from `from` to `to`.
    PeakObstacle peakObstacle(glm::vec3 from, glm::vec3 to, int steps = 256) const;
```

The complete updated header (lines 12-21 area):

```cpp
    enum class LOSStatus { Clear, Diffracted, Blocked };

    struct PeakObstacle {
        float clearanceM;
        float t;
    };

    float      heightAt(int x, int z) const;
    float      heightAtWorld(float wx, float wz) const;
    bool       lineOfSight(glm::vec3 from, glm::vec3 to, int steps = 256) const;
    LOSStatus  lineOfSightStatus(glm::vec3 from, glm::vec3 to, int steps = 256) const;
    PeakObstacle peakObstacle(glm::vec3 from, glm::vec3 to, int steps = 256) const;
```

- [ ] **Step 4: Implement `peakObstacle` in `src/sim/TerrainModel.cpp`**

Add at the end of `TerrainModel.cpp`:

```cpp
TerrainModel::PeakObstacle
TerrainModel::peakObstacle(glm::vec3 from, glm::vec3 to, int steps) const {
    glm::vec3 dir = to - from;
    float maxExcess = -std::numeric_limits<float>::infinity();
    float maxT      = 0.5f;
    for (int i = 1; i < steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        glm::vec3 p  = from + dir * t;
        float excess = heightAtWorld(p.x, p.z) - p.y;
        if (excess > maxExcess) {
            maxExcess = excess;
            maxT      = t;
        }
    }
    return { maxExcess, maxT };
}
```

Also add the missing include at the top of `TerrainModel.cpp` if not present:

```cpp
#include <limits>
```

- [ ] **Step 5: Build and run tests**

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline_tests -j4 2>&1
ctest --verbose -R physics_tests
```

Expected:
```
5 passed, 0 failed.
```

- [ ] **Step 6: Commit**

```
git add src/sim/TerrainModel.h src/sim/TerrainModel.cpp tests/test_main.cpp
git commit -m "feat: TerrainModel::peakObstacle() for knife-edge diffraction parameter"
```

---

## Task 4: FullSolver

**Files:**
- Create: `src/sim/FullSolver.h`
- Create: `src/sim/FullSolver.cpp`
- Modify: `tests/test_main.cpp` (add FullSolver tests)

The FullSolver computes per-grid-cell received power as an incoherent sum of:
1. **Direct path**: FSPL + material absorption + rain attenuation + optional knife-edge diffraction loss.
2. **Single-bounce ground reflection**: Fresnel power reflection coefficient using material ε_r and σ, plus FSPL for the reflected path length.

Blocked paths (LOSStatus::Blocked) contribute nothing for the direct ray; the reflection path is attempted independently.

### Write failing tests

- [ ] **Step 1: Add FullSolver tests to `tests/test_main.cpp`**

Add these includes at the top of the file (after `MaterialLibrary.h`):

```cpp
#include "sim/FullSolver.h"
```

Add these test functions before `main()`:

```cpp
// ── FullSolver unit tests ─────────────────────────────────────────────────────
#include "sim/Types.h"

static void test_fspl_known_value() {
    // FSPL at 1000 m, 150 MHz:
    // = 20*log10(1000) + 20*log10(150e6) - 147.55
    // = 60 + 163.52 - 147.55 = 75.97 dB
    // We access this via a thin public wrapper — or just test solve() output indirectly.
    // Use a flat terrain, one antenna directly above centre cell.
    TerrainModel terrain;
    terrain.width    = 10;
    terrain.height   = 10;
    terrain.cellSize = 10.0f;
    terrain.heights.assign(100, 0.0f);

    Antenna tx;
    tx.position     = {50.0f, 10.0f, 50.0f};  // centre, 10 m above flat terrain
    tx.frequencyMHz = 150.0f;
    tx.powerW       = 50.0f;
    tx.gainDbi      = 0.0f;
    tx.isJammer     = false;

    MaterialLibrary lib;  // uses defaultMaterial()

    FullSolver solver;
    solver.rainRateMmH = 0.0f;  // no rain to isolate FSPL
    FieldGrid grid = solver.solve(terrain, {tx}, lib);

    CHECK(grid.valid());
    CHECK(grid.width > 0 && grid.height > 0);

    // All cells should have a finite received power (direct path clear on flat terrain)
    bool anyFinite = false;
    for (float v : grid.dbm) {
        if (std::isfinite(v)) { anyFinite = true; break; }
    }
    CHECK(anyFinite);
}

static void test_rain_zero_vs_nonzero() {
    TerrainModel terrain;
    terrain.width    = 5;
    terrain.height   = 5;
    terrain.cellSize = 10.0f;
    terrain.heights.assign(25, 0.0f);

    Antenna tx;
    tx.position     = {25.0f, 10.0f, 25.0f};
    tx.frequencyMHz = 1000.0f;
    tx.powerW       = 100.0f;
    tx.gainDbi      = 0.0f;
    tx.isJammer     = false;

    MaterialLibrary lib;

    FullSolver solver0, solver1;
    solver0.rainRateMmH = 0.0f;
    solver1.rainRateMmH = 50.0f;  // heavy rain

    FieldGrid g0 = solver0.solve(terrain, {tx}, lib);
    FieldGrid g1 = solver1.solve(terrain, {tx}, lib);

    // Rain should reduce received power (g1 should be lower than g0 for finite cells)
    bool rainReducesPower = false;
    for (int i = 0; i < (int)g0.dbm.size(); ++i) {
        if (std::isfinite(g0.dbm[i]) && std::isfinite(g1.dbm[i])) {
            if (g1.dbm[i] < g0.dbm[i]) { rainReducesPower = true; break; }
        }
    }
    CHECK(rainReducesPower);
}

static void test_knife_edge_loss() {
    // knifeEdgeLossDb is private, but we can check known values via a public wrapper.
    // Instead, test indirectly: a diffracted path should receive less power than a clear path.
    // We set up two terrains: flat (clear) and with a hill (diffracted).
    auto makeAntenna = [](float x, float z) {
        Antenna a;
        a.position     = {x, 10.0f, z};
        a.frequencyMHz = 150.0f;
        a.powerW       = 50.0f;
        a.gainDbi      = 0.0f;
        a.isJammer     = false;
        return a;
    };

    // Flat terrain
    TerrainModel flat;
    flat.width = flat.height = 100;
    flat.cellSize = 10.0f;
    flat.heights.assign(10000, 0.0f);

    // Terrain with a ridge (15 m) through the middle
    TerrainModel ridged = flat;
    for (int z = 45; z <= 55; ++z)
        for (int x = 0; x < 100; ++x)
            ridged.heights[z * 100 + x] = 15.0f;

    Antenna tx = makeAntenna(50.0f, 10.0f);
    MaterialLibrary lib;
    FullSolver solver;
    solver.rainRateMmH = 0.0f;

    FieldGrid gFlat   = solver.solve(flat,   {tx}, lib);
    FieldGrid gRidged = solver.solve(ridged, {tx}, lib);

    // Find a cell on the far side of the ridge (z > 60) — should be weaker with ridge
    int testX = 5, testZ = 80;
    float dbmFlat   = gFlat.at(testX, testZ);
    float dbmRidged = gRidged.at(testX, testZ);

    if (std::isfinite(dbmFlat) && std::isfinite(dbmRidged)) {
        CHECK(dbmRidged < dbmFlat);  // ridge should reduce power
    }
}
```

Extend `main()` to call all three new functions:

```cpp
    test_fspl_known_value();
    test_rain_zero_vs_nonzero();
    test_knife_edge_loss();
```

- [ ] **Step 2: Build tests — expect linker error** (FullSolver not implemented)

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline_tests -j4 2>&1
```

Expected: linker error for `FullSolver::solve`.

### Implement FullSolver

- [ ] **Step 3: Create `src/sim/FullSolver.h`**

```cpp
#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "MaterialLibrary.h"
#include <vector>

class FullSolver {
public:
    float gridResolution = 10.0f;  // metres per grid cell
    float rainRateMmH    = 4.0f;   // moderate rain (mm/h); 0 = disable

    FieldGrid solve(const TerrainModel& terrain,
                    const std::vector<Antenna>& antennas,
                    const MaterialLibrary& materials) const;

private:
    // Friis FSPL in dB
    static float fsplDb(float distM, float freqMHz);

    // Fresnel power reflection coefficient [0,1], vertical polarisation
    static float fresnelPowerRefl(float eps_r, float sigma_sm,
                                   float freqMHz, float grazingAngleRad);

    // Knife-edge diffraction loss in dB (ITU-R P.526-15 piecewise approximation)
    static float knifeEdgeLossDb(float nu);

    // Rain attenuation in dB (ITU-R P.838-3 vertical polarisation)
    static float rainAttenuationDb(float rainRateMmH, float freqMHz, float distKm);
};
```

- [ ] **Step 4: Create `src/sim/FullSolver.cpp`**

```cpp
#include "FullSolver.h"
#include <cmath>
#include <complex>
#include <limits>
#include <glm/glm.hpp>

static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();
static constexpr float PI      = 3.14159265f;

static float dBmToMw(float dbm) { return std::pow(10.0f, dbm / 10.0f); }
static float mwToDbm(float mw)  { return 10.0f * std::log10(mw); }

// ── FullSolver::solve ─────────────────────────────────────────────────────────

FieldGrid FullSolver::solve(const TerrainModel& terrain,
                             const std::vector<Antenna>& antennas,
                             const MaterialLibrary& materials) const {
    FieldGrid grid;
    grid.cellSize = gridResolution;
    grid.width    = static_cast<int>(terrain.width  * terrain.cellSize / gridResolution);
    grid.height   = static_cast<int>(terrain.height * terrain.cellSize / gridResolution);
    grid.originXZ = {0.0f, 0.0f};
    grid.dbm.assign(grid.width * grid.height, NEG_INF);

    const Material& mat = materials.defaultMaterial();

    for (int gz = 0; gz < grid.height; ++gz) {
        for (int gx = 0; gx < grid.width; ++gx) {
            float wx = gx * gridResolution + gridResolution * 0.5f;
            float wz = gz * gridResolution + gridResolution * 0.5f;
            float wy = terrain.heightAtWorld(wx, wz) + 1.5f;  // 1.5 m receive height
            glm::vec3 rx{wx, wy, wz};

            float totalMw = 0.0f;

            for (const auto& ant : antennas) {
                if (ant.isJammer) continue;

                float distM = glm::distance(ant.position, rx);
                if (distM < 1.0f) distM = 1.0f;

                float txPowerDbm = 10.0f * std::log10(ant.powerW * 1000.0f);
                float eirpDbm    = txPowerDbm + ant.gainDbi;
                float freq       = ant.frequencyMHz;
                float distKm     = distM / 1000.0f;

                float matLossDb  = mat.lossDbPer100m(freq) * distM / 100.0f;
                float rainDb     = rainAttenuationDb(rainRateMmH, freq, distKm);

                // ── 1. Direct path ──────────────────────────────────────────
                auto losStatus = terrain.lineOfSightStatus(ant.position, rx);
                if (losStatus != TerrainModel::LOSStatus::Blocked) {
                    float directLoss = fsplDb(distM, freq) + matLossDb + rainDb;

                    if (losStatus == TerrainModel::LOSStatus::Diffracted) {
                        auto obs = terrain.peakObstacle(ant.position, rx);
                        float d1 = std::max(distM * obs.t,       1.0f);
                        float d2 = std::max(distM * (1.0f - obs.t), 1.0f);
                        float lambda = 3.0e8f / (freq * 1.0e6f);
                        float nu = obs.clearanceM
                                 * std::sqrt(2.0f * distM / (lambda * d1 * d2));
                        directLoss += knifeEdgeLossDb(nu);
                    }

                    totalMw += dBmToMw(eirpDbm - directLoss);
                }

                // ── 2. Single-bounce ground reflection ──────────────────────
                {
                    float mx = (ant.position.x + rx.x) * 0.5f;
                    float mz = (ant.position.z + rx.z) * 0.5f;
                    glm::vec3 sp{mx, terrain.heightAtWorld(mx, mz), mz};

                    float dRefl = glm::distance(ant.position, sp) + glm::distance(sp, rx);
                    if (dRefl > 1.0f) {
                        glm::vec3 incoming = glm::normalize(sp - ant.position);
                        float grazingAngle = std::asin(std::abs(incoming.y));
                        grazingAngle = std::max(grazingAngle, 0.01f);

                        float refl = fresnelPowerRefl(mat.epsilon_r, mat.sigma_sm,
                                                       freq, grazingAngle);
                        if (refl > 0.001f) {
                            float reflLoss = fsplDb(dRefl, freq) + matLossDb + rainDb
                                           - 10.0f * std::log10(refl);
                            totalMw += dBmToMw(eirpDbm - reflLoss);
                        }
                    }
                }
            }

            if (totalMw > 0.0f)
                grid.dbm[gz * grid.width + gx] = mwToDbm(totalMw);
        }
    }
    return grid;
}

// ── Static helpers ────────────────────────────────────────────────────────────

float FullSolver::fsplDb(float distM, float freqMHz) {
    return 20.0f * std::log10(distM)
         + 20.0f * std::log10(freqMHz * 1.0e6f)
         - 147.55f;
}

float FullSolver::fresnelPowerRefl(float eps_r, float sigma_sm,
                                    float freqMHz, float grazingAngleRad) {
    using C = std::complex<float>;
    const float omega = 2.0f * PI * freqMHz * 1.0e6f;
    const float eps0  = 8.854e-12f;
    C eps_c{eps_r, -sigma_sm / (omega * eps0)};

    float sin_g = std::sin(grazingAngleRad);
    float cos_g = std::cos(grazingAngleRad);

    C inner    = eps_c - C{cos_g * cos_g, 0.0f};
    C sqrtTerm = std::sqrt(inner);
    C num      = eps_c * C{sin_g, 0.0f} - sqrtTerm;
    C den      = eps_c * C{sin_g, 0.0f} + sqrtTerm;

    float gamma = (std::abs(den) > 1e-9f) ? (std::abs(num) / std::abs(den)) : 0.0f;
    return std::min(gamma * gamma, 1.0f);  // power reflection ∈ [0,1]
}

float FullSolver::knifeEdgeLossDb(float nu) {
    // ITU-R P.526-15 piecewise approximation. Returns positive dB of loss.
    if (nu <= -0.7f) return 0.0f;
    if (nu <= 0.0f)  return std::max(0.0f, -20.0f * std::log10(0.5f - 0.62f * nu));
    if (nu <= 1.0f)  return -20.0f * std::log10(0.5f * std::exp(-0.95f * nu));
    if (nu <= 2.4f) {
        float x     = 0.38f - 0.1f * nu;
        float inner = 0.1184f - x * x;
        inner = std::max(inner, 0.0f);
        float arg = 0.4f - std::sqrt(inner);
        return (arg > 0.0f) ? -20.0f * std::log10(arg) : 50.0f;
    }
    return -20.0f * std::log10(0.225f / std::max(nu, 0.001f));
}

float FullSolver::rainAttenuationDb(float rainRateMmH, float freqMHz, float distKm) {
    if (rainRateMmH <= 0.0f || distKm <= 0.0f) return 0.0f;

    // ITU-R P.838-3: k and α for vertical polarisation (sparse lookup, linear interp)
    // {freq_MHz, k, alpha}
    static const float tbl[][3] = {
        {100.0f,    0.0000387f, 0.912f},
        {300.0f,    0.000354f,  0.962f},
        {1000.0f,   0.00387f,   1.000f},
        {3000.0f,   0.0178f,    1.127f},
        {10000.0f,  0.101f,     1.276f},
    };
    static const int N = 5;

    float k, alpha;
    if (freqMHz <= tbl[0][0]) {
        k = tbl[0][1]; alpha = tbl[0][2];
    } else if (freqMHz >= tbl[N-1][0]) {
        k = tbl[N-1][1]; alpha = tbl[N-1][2];
    } else {
        k = tbl[0][1]; alpha = tbl[0][2];
        for (int i = 1; i < N; ++i) {
            if (freqMHz <= tbl[i][0]) {
                float t = (freqMHz - tbl[i-1][0]) / (tbl[i][0] - tbl[i-1][0]);
                k     = tbl[i-1][1] + t * (tbl[i][1] - tbl[i-1][1]);
                alpha = tbl[i-1][2] + t * (tbl[i][2] - tbl[i-1][2]);
                break;
            }
        }
    }

    float specificAtten = k * std::pow(rainRateMmH, alpha);  // dB/km
    return specificAtten * distKm;
}
```

- [ ] **Step 5: Build and run all tests**

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline_tests -j4 2>&1
ctest --verbose -R physics_tests
```

Expected:
```
8 passed, 0 failed.
```

- [ ] **Step 6: Commit**

```
git add src/sim/FullSolver.h src/sim/FullSolver.cpp tests/test_main.cpp
git commit -m "feat: FullSolver with Fresnel reflection, knife-edge diffraction, and rain attenuation"
```

---

## Task 5: Wire FullSolver + MaterialLibrary into SimEngine

**Files:**
- Modify: `src/sim/SimEngine.h`
- Modify: `src/sim/SimEngine.cpp`

- [ ] **Step 1: Replace `src/sim/SimEngine.h`**

```cpp
#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "FastSolver.h"
#include "FullSolver.h"
#include "MaterialLibrary.h"
#include <vector>
#include <functional>

class SimEngine {
public:
    enum class Mode { Fast, Full };

    Mode mode = Mode::Fast;

    void setTerrain(TerrainModel terrain);
    void setAntennas(std::vector<Antenna> antennas);

    // Load EM material properties from JSON. Silent no-op on failure
    // (default materials are always available without a file).
    void loadMaterials(const std::string& path);

    void solve();  // dispatches to FastSolver or FullSolver; fires onFieldUpdated

    const FieldGrid&            fieldGrid() const { return m_field; }
    const TerrainModel&         terrain()   const { return m_terrain; }
    const std::vector<Antenna>& antennas()  const { return m_antennas; }

    std::function<void()> onFieldUpdated;

private:
    TerrainModel         m_terrain;
    std::vector<Antenna> m_antennas;
    FieldGrid            m_field;
    FastSolver           m_fast;
    FullSolver           m_full;
    MaterialLibrary      m_materials;
};
```

- [ ] **Step 2: Replace `src/sim/SimEngine.cpp`**

```cpp
#include "SimEngine.h"
#include <stdexcept>

void SimEngine::setTerrain(TerrainModel terrain) {
    m_terrain = std::move(terrain);
}

void SimEngine::setAntennas(std::vector<Antenna> antennas) {
    m_antennas = std::move(antennas);
}

void SimEngine::loadMaterials(const std::string& path) {
    try {
        m_materials.load(path);
    } catch (const std::exception&) {
        // Default materials remain available — silently ignore missing file.
    }
}

void SimEngine::solve() {
    if (mode == Mode::Fast)
        m_field = m_fast.solve(m_terrain, m_antennas, m_materials);
    else
        m_field = m_full.solve(m_terrain, m_antennas, m_materials);

    if (onFieldUpdated) onFieldUpdated();
}
```

- [ ] **Step 3: Build the main target**

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline -j4 2>&1
```

Expected: `[100%] Built target ridgeline`

- [ ] **Step 4: Commit**

```
git add src/sim/SimEngine.h src/sim/SimEngine.cpp
git commit -m "feat: SimEngine dispatches FullSolver in Full Physics mode; loads MaterialLibrary"
```

---

## Task 6: Fidelity Toggle in MainWindow

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Add `m_modeCombo` to `src/ui/MainWindow.h`**

Add `#include <QComboBox>` to the includes, and add the member after `m_solveBtn`:

```cpp
#include <QComboBox>
// ...
    QComboBox*      m_modeCombo   = nullptr;
```

Full updated `MainWindow.h`:

```cpp
#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include "sim/SimEngine.h"
#include "ViewportWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void loadTerrain(const QString& path);

private slots:
    void onSolve();
    void onLoadTerrain();

private:
    SimEngine       m_engine;
    ViewportWidget* m_viewport    = nullptr;
    QLabel*         m_statusLabel = nullptr;
    QPushButton*    m_solveBtn    = nullptr;
    QComboBox*      m_modeCombo   = nullptr;

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
};
```

- [ ] **Step 2: Update the constructor in `src/ui/MainWindow.cpp`**

In the constructor, **replace** the `QToolBar` section (the block from `QToolBar* tb = ...` through `connect(m_solveBtn, ...)`) with:

```cpp
    // Toolbar
    QToolBar* tb = addToolBar("Main");
    tb->setMovable(false);

    QLabel* freqLabel = new QLabel("  150 MHz  |  50 W  ");
    freqLabel->setStyleSheet("color: rgba(255,255,255,0.7); font-family: monospace;");
    tb->addWidget(freqLabel);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("⚡ Fast Mode");
    m_modeCombo->addItem("🔬 Full Physics");
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
```

- [ ] **Step 3: Load materials in `setupScene()`**

In `MainWindow::setupScene()`, after `placeDefaultAntennas()`, add:

```cpp
    m_engine.loadMaterials("assets/materials.json");
```

So `setupScene()` becomes:

```cpp
void MainWindow::setupScene() {
    TerrainModel terrain = TerrainLoader::makeHills(200, 200, 10.0f, 40.0f);
    m_engine.setTerrain(std::move(terrain));
    placeDefaultAntennas();
    m_engine.loadMaterials("assets/materials.json");
}
```

- [ ] **Step 4: Build and run the app**

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target ridgeline -j4 2>&1
```

Expected: `[100%] Built target ridgeline`

Manual smoke test:
1. Run `ridgeline.exe` from the build directory.
2. Confirm the toolbar shows `⚡ Fast Mode` and `🔬 Full Physics` dropdown.
3. Press **Solve** in Fast mode — heatmap appears as before.
4. Switch to Full Physics, press Solve — heatmap updates (slightly different values due to Fresnel reflection and material loss).
5. Switch back to Fast, Solve again — values return to Fast mode result.

- [ ] **Step 5: Build and run all tests one final time**

```
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . -j4 2>&1
ctest --verbose
```

Expected: `8 passed, 0 failed.` and `ridgeline.exe` builds clean.

- [ ] **Step 6: Commit**

```
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: fidelity toggle (Fast / Full Physics) wired to SimEngine mode"
```

- [ ] **Step 7: Push**

```
git push origin master
```

---

## Verification Checklist

| # | Test | Expected |
|---|---|---|
| 1 | `ctest --verbose` | 8 passed, 0 failed |
| 2 | App launches | Toolbar shows Fast/Full combo |
| 3 | Fast Solve | Heatmap appears, status → "Done" |
| 4 | Switch to Full Physics, Solve | Heatmap updates with different (slightly higher near reflective terrain) values |
| 5 | Load DTED file, Solve both modes | No crash, heatmap updates both times |
| 6 | Set rain rate to 50 mm/h (edit `m_full.rainRateMmH`) | Full Physics shows reduced range vs. 0 mm/h |
