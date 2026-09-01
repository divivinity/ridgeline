#include "sim/RFModels.h"
#include "sim/ItmPropagation.h"
#include "sim/FastSolver.h"
#include "sim/FullSolver.h"
#include "sim/MaterialLibrary.h"
#include "data/AntennaPatternIO.h"
#include "data/ScenarioIO.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL: " #c " line " << __LINE__ << '\n'; ++failures; } } while (0)
#define NEAR(a,b,t) CHECK(std::abs((double)(a)-(double)(b)) <= (double)(t))

TerrainModel flatTerrain(int width, int height, float cell) {
    TerrainModel t;
    t.width = width;
    t.height = height;
    t.cellSize = cell;
    t.heights.assign(static_cast<std::size_t>(width) * height, 0.0f);
    return t;
}

void testNoiseFloor() {
    ReceiverSettings rx;
    rx.bandwidthHz = 25000.0f;
    rx.noiseFigureDb = 5.0f;
    NEAR(RFModels::noiseFloorDbm(rx), -125.02, 0.15);
}

void testDirectionalGainAndPolarization() {
    ReceiverSettings rx;
    rx.polarization = "vertical";
    Antenna antenna;
    antenna.position = {0.0f, 10.0f, 0.0f};
    antenna.gainDbi = 12.0f;
    antenna.horizontalBeamwidthDeg = 60.0f;
    antenna.verticalBeamwidthDeg = 60.0f;
    antenna.frontToBackDb = 30.0f;
    antenna.azimuthDeg = 0.0f;
    antenna.polarization = "vertical";

    const float front = RFModels::effectiveAntennaGainDbi(
        antenna, {0.0f, 10.0f, 1000.0f}, rx);
    const float back = RFModels::effectiveAntennaGainDbi(
        antenna, {0.0f, 10.0f, -1000.0f}, rx);
    CHECK(front - back > 20.0f);

    antenna.polarization = "horizontal";
    const float crossPol = RFModels::effectiveAntennaGainDbi(
        antenna, {0.0f, 10.0f, 1000.0f}, rx);
    CHECK(front - crossPol >= 19.0f);
}

void testPatternImport() {
    const std::string path = "test_pattern.csv";
    {
        std::ofstream f(path);
        f << "plane,angle_deg,attenuation_db\n";
        f << "azimuth,0,0\n";
        f << "azimuth,90,10\n";
        f << "azimuth,180,30\n";
        f << "azimuth,270,10\n";
        f << "elevation,-90,30\n";
        f << "elevation,0,0\n";
        f << "elevation,90,30\n";
    }
    Antenna antenna;
    AntennaPatternIO::loadCsv(path, antenna);
    std::remove(path.c_str());
    CHECK(antenna.azimuthPatternDb.size() == 360u);
    CHECK(antenna.elevationPatternDb.size() == 181u);
    NEAR(antenna.azimuthPatternDb[180], 30.0, 0.01);
    NEAR(antenna.elevationPatternDb[90], 0.0, 0.01);
}

void testFastSolverMetrics() {
    TerrainModel terrain = flatTerrain(31, 31, 20.0f);
    MaterialLibrary materials;
    FastSolver solver;
    solver.gridResolution = 40.0f;
    solver.receiver.sensitivityDbm = -130.0f;
    solver.receiver.requiredSnrDb = 0.0f;
    solver.receiver.fadeMarginTargetDb = 0.0f;

    Antenna desired;
    desired.position = {300.0f, 20.0f, 300.0f};
    desired.powerW = 10.0f;
    desired.frequencyMHz = 450.0f;

    Antenna jammer = desired;
    jammer.position = {100.0f, 20.0f, 300.0f};
    jammer.powerW = 1.0f;
    jammer.isJammer = true;

    FieldGrid grid = solver.solve(terrain, {desired, jammer}, materials);
    CHECK(grid.valid());
    CHECK(grid.jammerDbm.size() == grid.dbm.size());
    CHECK(grid.sinrDb.size() == grid.dbm.size());
    bool found = false;
    for (std::size_t i = 0; i < grid.dbm.size(); ++i) {
        if (std::isfinite(grid.dbm[i]) && std::isfinite(grid.jammerDbm[i])) {
            CHECK(grid.sinrDb[i] <= grid.snrDb[i] + 0.01f);
            NEAR(grid.jOverSDb[i], grid.jammerDbm[i] - grid.dbm[i], 0.01);
            found = true;
            break;
        }
    }
    CHECK(found);
}

void testFullSolverBandwidthFiltering() {
    TerrainModel terrain = flatTerrain(31, 31, 20.0f);
    MaterialLibrary materials;
    FullSolver solver;
    solver.gridResolution = 60.0f;
    solver.enableItm = false;
    solver.enableGroundReflection = false;
    solver.enableRain = false;
    solver.enableFresnelPenalty = false;
    solver.receiver.bandwidthHz = 25000.0f;
    solver.receiver.sensitivityDbm = -140.0f;

    Antenna desired;
    desired.position = {300.0f, 20.0f, 300.0f};
    desired.powerW = 10.0f;
    desired.frequencyMHz = 450.0f;

    Antenna jammer = desired;
    jammer.position = {100.0f, 20.0f, 300.0f};
    jammer.powerW = 10.0f;
    jammer.isJammer = true;
    jammer.frequencyMHz = 451.0f;

    const FieldGrid outOfBand = solver.solve(terrain, {desired, jammer}, materials);
    bool sawDesired = false;
    bool sawOutOfBandJammer = false;
    for (std::size_t i = 0; i < outOfBand.dbm.size(); ++i) {
        sawDesired = sawDesired || std::isfinite(outOfBand.dbm[i]);
        sawOutOfBandJammer = sawOutOfBandJammer || std::isfinite(outOfBand.jammerDbm[i]);
    }
    CHECK(sawDesired);
    CHECK(!sawOutOfBandJammer);

    jammer.frequencyMHz = 450.005f; // 5 kHz offset, inside a 25 kHz receiver.
    const FieldGrid inBand = solver.solve(terrain, {desired, jammer}, materials);
    bool sawInBandJammer = false;
    bool sawSinrPenalty = false;
    for (std::size_t i = 0; i < inBand.dbm.size(); ++i) {
        if (std::isfinite(inBand.jammerDbm[i])) sawInBandJammer = true;
        if (std::isfinite(inBand.sinrDb[i]) && std::isfinite(inBand.snrDb[i]) &&
            inBand.sinrDb[i] < inBand.snrDb[i] - 0.1f) {
            sawSinrPenalty = true;
        }
    }
    CHECK(sawInBandJammer);
    CHECK(sawSinrPenalty);
}

void testItmAdapter() {
    TerrainModel terrain = flatTerrain(1001, 2, 10.0f);
    MaterialLibrary materials;
    Antenna antenna;
    antenna.position = {0.0f, 10.0f, 5.0f};
    antenna.frequencyMHz = 230.0f;
    antenna.polarization = "vertical";
    const ItmPrediction prediction = ItmPropagation::predict(
        terrain, antenna.position, {10000.0f, 1.5f, 5.0f},
        antenna, materials.defaultMaterial());
#ifdef HAVE_NTIA_ITM
    CHECK(prediction.valid);
    CHECK(prediction.basicTransmissionLossDb > 90.0f);
#else
    CHECK(!prediction.valid);
#endif
}

void testScenarioV4RoundTrip() {
    ScenarioDoc doc;
    doc.receiver.sensitivityDbm = -112.0f;
    doc.receiver.bandwidthHz = 12500.0f;
    doc.receiver.noiseFigureDb = 3.5f;
    doc.receiver.requiredSnrDb = 8.0f;
    doc.receiver.fadeMarginTargetDb = 12.0f;
    doc.receiver.polarization = "horizontal";
    Antenna a;
    a.azimuthDeg = 123.0f;
    a.downtiltDeg = 4.5f;
    a.horizontalBeamwidthDeg = 35.0f;
    a.verticalBeamwidthDeg = 20.0f;
    a.frontToBackDb = 28.0f;
    a.cableLossDb = 1.7f;
    a.polarization = "horizontal";
    a.azimuthPatternDb = {0.0f, 4.0f, 10.0f};
    a.elevationPatternDb = {20.0f, 0.0f, 20.0f};
    a.patternSource = "measured.csv";
    doc.antennas.push_back(a);

    const std::string path = "test_scenario_v4.json";
    ScenarioIO::save(doc, path);
    const ScenarioDoc loaded = ScenarioIO::load(path);
    std::remove(path.c_str());

    NEAR(loaded.receiver.sensitivityDbm, -112.0, 0.001);
    NEAR(loaded.receiver.bandwidthHz, 12500.0, 0.1);
    CHECK(loaded.receiver.polarization == "horizontal");
    CHECK(loaded.antennas.size() == 1u);
    NEAR(loaded.antennas[0].azimuthDeg, 123.0, 0.001);
    NEAR(loaded.antennas[0].cableLossDb, 1.7, 0.001);
    CHECK(loaded.antennas[0].azimuthPatternDb.size() == 3u);
    CHECK(loaded.antennas[0].patternSource == "measured.csv");
}
}

int main() {
    testNoiseFloor();
    testDirectionalGainAndPolarization();
    testPatternImport();
    testFastSolverMetrics();
    testFullSolverBandwidthFiltering();
    testItmAdapter();
    testScenarioV4RoundTrip();
    if (failures == 0) std::cout << "RF model tests passed.\n";
    return failures ? 1 : 0;
}
