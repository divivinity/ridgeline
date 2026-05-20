#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include "sim/MaterialLibrary.h"
#include "sim/TerrainModel.h"
#include "sim/FullSolver.h"
#include "sim/SimEngine.h"
#include "data/AntennaLibrary.h"
#include "data/ScenarioIO.h"
#include <cstdio>  // for std::remove

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

static void test_antenna_library_load_defaults() {
    // loadDefaults() should produce the same 4 types as defaults()
    AntennaLibrary lib;
    lib.loadDefaults();
    CHECK(lib.types().size() == AntennaLibrary::defaults().types().size());
    CHECK(lib.find("omni")   != nullptr);
    CHECK(lib.find("dipole") != nullptr);
    CHECK(lib.find("yagi")   != nullptr);
    CHECK(lib.find("patch")  != nullptr);

    // After load() fails, loadDefaults() should recover the library.
    AntennaLibrary lib2;
    bool threw = false;
    try { lib2.load("nonexistent_antennas.json"); } catch (...) { threw = true; }
    CHECK(threw);
    lib2.loadDefaults();
    CHECK(lib2.find("omni") != nullptr);
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
    doc.terrain.hgtLat  = 35.05;
    doc.terrain.hgtLon  = -78.88;
    doc.terrain.hgtSize = 512;
    doc.mode            = SimEngine::Mode::Fast;

    const std::string path = "test_scenario_hgt_tmp.json";
    ScenarioIO::save(doc, path);
    ScenarioDoc loaded = ScenarioIO::load(path);

    CHECK(loaded.terrain.type == TerrainSource::Type::Hgt);
    CHECK(loaded.terrain.hgtPath == "/data/N35W079.hgt");
    CHECK_NEAR(loaded.terrain.hgtLat, 35.05, 0.000001);
    CHECK_NEAR(loaded.terrain.hgtLon, -78.88, 0.000001);
    CHECK(loaded.terrain.hgtSize == 512);
    CHECK(loaded.mode == SimEngine::Mode::Fast);

    std::remove(path.c_str());
}

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
    test_antenna_library_load_defaults();
    test_add_antenna();
    test_remove_antenna();
    test_update_antenna();
    test_scenario_io_hills_round_trip();
    test_scenario_io_hgt_round_trip();
    test_spectrum_length();
    test_spectrum_freq_range();
    test_spectrum_higher_freq_lower_power();
    test_spectrum_power_reasonable();
    test_spectrum_single_step();

    std::cout << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail ? 1 : 0;
}
