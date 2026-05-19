#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include "sim/MaterialLibrary.h"
#include "sim/TerrainModel.h"
#include "sim/FullSolver.h"
#include "sim/SimEngine.h"
#include "data/AntennaLibrary.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)
#define CHECK_NEAR(a, b, tol) CHECK(std::abs((float)(a)-(float)(b)) < (float)(tol))

static void test_default_material() {
    const Material& m = MaterialLibrary::defaultMaterial();
    CHECK(m.name == "dry_soil");
    CHECK_NEAR(m.epsilon_r, 3.0f, 0.01f);
    CHECK_NEAR(m.sigma_sm,  0.001f, 1e-5f);
    CHECK(!m.bandLoss.empty());
}

static void test_loss_interpolation() {
    const Material& m = MaterialLibrary::defaultMaterial();
    CHECK_NEAR(m.lossDbPer100m(100.0f), 0.3f, 0.01f);
    CHECK_NEAR(m.lossDbPer100m(300.0f), 0.5f, 0.01f);
    // At 200 MHz, linear interpolation between 100 MHz (0.3) and 300 MHz (0.5):
    // t = (200-100)/(300-100) = 0.5  →  0.3 + 0.5*(0.5-0.3) = 0.4
    CHECK_NEAR(m.lossDbPer100m(200.0f), 0.4f, 0.01f);
    CHECK_NEAR(m.lossDbPer100m(10.0f),    0.3f, 0.01f);  // clamp low
    CHECK_NEAR(m.lossDbPer100m(10000.0f), 3.0f, 0.01f);  // clamp high
}

static void test_load_from_json(const std::string& path) {
    MaterialLibrary lib;
    lib.load(path);

    const Material& concrete = lib.find("concrete");
    CHECK(concrete.name == "concrete");
    CHECK_NEAR(concrete.epsilon_r, 5.5f, 0.01f);
    CHECK_NEAR(concrete.sigma_sm,  0.02f, 1e-4f);
    CHECK_NEAR(concrete.lossDbPer100m(1000.0f), 5.0f, 0.01f);

    const Material& unknown = lib.find("unobtainium");
    CHECK(unknown.name == "dry_soil");

    for (const char* name : {"dry_soil","wet_soil","concrete","foliage","asphalt","water"}) {
        CHECK(lib.find(name).name == std::string(name));
    }
}

// ── TerrainModel tests ───────────────────────────────────────────────────────

static void test_peak_obstacle_flat_terrain() {
    TerrainModel t;
    t.width    = 200;
    t.height   = 200;
    t.cellSize = 1.0f;
    t.heights.assign(200 * 200, 0.0f);

    // LOS ray at y=10 over flat terrain at y=0 — terrain should be well below LOS
    glm::vec3 from{0.0f, 10.0f, 0.0f};
    glm::vec3 to  {100.0f, 10.0f, 100.0f};
    auto obs = t.peakObstacle(from, to);

    CHECK(obs.clearanceM < 0.0f);        // terrain below LOS
    CHECK(obs.t > 0.0f && obs.t < 1.0f); // peak somewhere along path
}

static void test_peak_obstacle_blocked_terrain() {
    TerrainModel t;
    t.width    = 200;
    t.height   = 200;
    t.cellSize = 1.0f;
    t.heights.assign(200 * 200, 0.0f);

    // Place a 20 m hill near the centre
    for (int dz = -5; dz <= 5; ++dz)
        for (int dx = -5; dx <= 5; ++dx)
            t.heights[(100 + dz) * 200 + (100 + dx)] = 20.0f;

    // Antennas at y=5 on either side — hill should rise above LOS
    glm::vec3 from{0.0f,   5.0f,   0.0f};
    glm::vec3 to  {200.0f, 5.0f, 200.0f};
    auto obs = t.peakObstacle(from, to);

    CHECK(obs.clearanceM > 10.0f);       // hill is 20m, LOS at 5m → excess ~15m
    CHECK(obs.t > 0.3f && obs.t < 0.7f); // peak is in the middle
}

// ── FullSolver tests ──────────────────────────────────────────────────────────

static void test_fullsolver_produces_finite_grid() {
    // Flat terrain, single antenna above centre — every cell should be finite.
    TerrainModel terrain;
    terrain.width    = 10;
    terrain.height   = 10;
    terrain.cellSize = 10.0f;
    terrain.heights.assign(100, 0.0f);

    Antenna tx;
    tx.position     = {50.0f, 10.0f, 50.0f};
    tx.frequencyMHz = 150.0f;
    tx.powerW       = 50.0f;
    tx.gainDbi      = 0.0f;
    tx.isJammer     = false;

    MaterialLibrary lib;
    FullSolver solver;
    solver.rainRateMmH = 0.0f;
    FieldGrid grid = solver.solve(terrain, {tx}, lib);

    CHECK(grid.valid());
    CHECK(grid.width > 0 && grid.height > 0);

    bool anyFinite = false;
    for (float v : grid.dbm)
        if (std::isfinite(v)) { anyFinite = true; break; }
    CHECK(anyFinite);
}

static void test_rain_reduces_power() {
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

    bool rainReducesPower = false;
    for (size_t i = 0; i < g0.dbm.size(); ++i)
        if (std::isfinite(g0.dbm[i]) && std::isfinite(g1.dbm[i]) && g1.dbm[i] < g0.dbm[i]) {
            rainReducesPower = true;
            break;
        }
    CHECK(rainReducesPower);
}

static void test_ridge_reduces_power_vs_flat() {
    // A receiver behind a ridge should have less power than the same setup on flat terrain.
    auto makeAntenna = [](float x, float z) {
        Antenna a;
        a.position     = {x, 10.0f, z};
        a.frequencyMHz = 150.0f;
        a.powerW       = 50.0f;
        a.gainDbi      = 0.0f;
        a.isJammer     = false;
        return a;
    };

    TerrainModel flat;
    flat.width = flat.height = 100;
    flat.cellSize = 10.0f;
    flat.heights.assign(10000, 0.0f);

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

    // Cell at (5, 80): behind the ridge
    float dbmFlat   = gFlat.at(5, 80);
    float dbmRidged = gRidged.at(5, 80);
    if (std::isfinite(dbmFlat) && std::isfinite(dbmRidged))
        CHECK(dbmRidged < dbmFlat);
}

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

static void test_add_antenna() {
    SimEngine engine;
    CHECK(engine.antennas().empty());

    Antenna a;
    a.frequencyMHz = 433.0f;
    a.powerW = 10.0f;
    engine.addAntenna(std::move(a));

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

    engine.removeAntenna(-1);
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
    CHECK(engine.antennas()[0].frequencyMHz == 900.0f);  // element not mutated by bad index
}

int main(int argc, char** argv) {
    std::string materialsPath = (argc > 1) ? argv[1] : "assets/materials.json";

    test_default_material();
    test_loss_interpolation();
    test_load_from_json(materialsPath);
    test_peak_obstacle_flat_terrain();
    test_peak_obstacle_blocked_terrain();
    test_fullsolver_produces_finite_grid();
    test_rain_reduces_power();
    test_ridge_reduces_power_vs_flat();
    test_antenna_library_defaults();
    test_antenna_library_load_json("assets/antennas.json");
    test_add_antenna();
    test_remove_antenna();
    test_update_antenna();

    std::cout << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail ? 1 : 0;
}
