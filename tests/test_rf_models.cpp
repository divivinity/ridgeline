#include "sim/RFModels.h"
#include "sim/ItmPropagation.h"
#include "sim/FastSolver.h"
#include "sim/FullSolver.h"
#include "sim/MaterialLibrary.h"
#include "sim/PointAnalysis.h"
#include "data/AntennaPatternIO.h"
#include "data/ScenarioIO.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
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
    CHECK(grid.servingSourceIndex.size() == grid.dbm.size());
    CHECK(grid.strongestJammerIndex.size() == grid.dbm.size());
    CHECK(grid.strongestDesiredDbm.size() == grid.dbm.size());
    bool found = false;
    for (std::size_t i = 0; i < grid.dbm.size(); ++i) {
        if (std::isfinite(grid.dbm[i]) && std::isfinite(grid.jammerDbm[i])) {
            CHECK(grid.sinrDb[i] <= grid.snrDb[i] + 0.01f);
            NEAR(grid.jOverSDb[i], grid.jammerDbm[i] - grid.dbm[i], 0.01);
            CHECK(grid.servingSourceIndex[i] == 0);
            CHECK(grid.strongestJammerIndex[i] == 1);
            found = true;
            break;
        }
    }
    CHECK(found);
}

void testPointAnalysisExplainsSolvedCell() {
    TerrainModel terrain = flatTerrain(41, 41, 20.0f);
    MaterialLibrary materials;
    FastSolver solver;
    solver.gridResolution = 40.0f;
    solver.receiver.sensitivityDbm = -120.0f;
    solver.receiver.requiredSnrDb = 5.0f;
    solver.receiver.fadeMarginTargetDb = 5.0f;

    Antenna emitter;
    emitter.position = {200.0f, 20.0f, 200.0f};
    emitter.frequencyMHz = 450.0f;
    emitter.powerW = 10.0f;
    Antenna jammer = emitter;
    jammer.position = {600.0f, 15.0f, 200.0f};
    jammer.powerW = 0.01f;
    jammer.isJammer = true;

    const std::vector<Antenna> antennas{emitter, jammer};
    const FieldGrid grid = solver.solve(terrain, antennas, materials);
    const PointAnalysisResult analysis = analyzeFieldPoint(
        grid, terrain, antennas, solver.receiver, 400.0f, 200.0f);
    CHECK(analysis.valid);
    NEAR(analysis.receiverPosition.x, 420.0, 0.01);
    NEAR(analysis.receiverPosition.z, 220.0, 0.01);
    CHECK(analysis.servingSourceIndex == 0);
    CHECK(analysis.strongestJammerIndex == 1);
    CHECK(std::isfinite(analysis.desiredDbm));
    CHECK(analysis.distanceM > 100.0f);
    CHECK(analysis.losStatus == TerrainModel::LOSStatus::Clear);
    CHECK(analysis.profileDistanceM.size() == 161u);
    CHECK(analysis.terrainElevationM.size() == analysis.profileDistanceM.size());
    CHECK(analysis.lineOfSightElevationM.size() == analysis.profileDistanceM.size());
    CHECK(analysis.fresnel60LowerElevationM.size() == analysis.profileDistanceM.size());
    NEAR(analysis.profileDistanceM.front(), 0.0, 0.001);
    NEAR(analysis.profileDistanceM.back(), analysis.distanceM, 0.01);
    NEAR(analysis.lineOfSightElevationM.front(), emitter.position.y, 0.001);
    NEAR(analysis.lineOfSightElevationM.back(), analysis.receiverPosition.y, 0.001);
    CHECK(!analysis.explanations.empty());
}

void testSolverContributorProvenance() {
    TerrainModel terrain = flatTerrain(21, 21, 20.0f);
    MaterialLibrary materials;
    ReceiverSettings receiver;
    receiver.bandwidthHz = 25000.0f;
    receiver.sensitivityDbm = -150.0f;
    receiver.requiredSnrDb = -50.0f;
    receiver.fadeMarginTargetDb = -50.0f;

    Antenna weakEmitter;
    weakEmitter.position = {200.0f, 20.0f, 200.0f};
    weakEmitter.frequencyMHz = 450.0f;
    weakEmitter.powerW = 1.0f;
    Antenna outOfBandJammer = weakEmitter;
    outOfBandJammer.isJammer = true;
    outOfBandJammer.frequencyMHz = 451.0f;
    outOfBandJammer.powerW = 1000.0f;
    Antenna servingEmitter = weakEmitter;
    servingEmitter.powerW = 10.0f;
    Antenna weakInBandJammer = weakEmitter;
    weakInBandJammer.isJammer = true;
    weakInBandJammer.frequencyMHz = 450.005f;
    weakInBandJammer.powerW = 0.01f;
    Antenna strongInBandJammer = weakInBandJammer;
    strongInBandJammer.powerW = 0.1f;
    Antenna weakestEmitter = weakEmitter;
    weakestEmitter.powerW = 0.01f;
    const std::vector<Antenna> antennas{
        weakEmitter, outOfBandJammer, servingEmitter,
        weakInBandJammer, strongInBandJammer, weakestEmitter};

    FastSolver fast;
    fast.gridResolution = 40.0f;
    fast.receiver = receiver;
    const FieldGrid fastGrid = fast.solve(terrain, antennas, materials);

    FullSolver full;
    full.gridResolution = 40.0f;
    full.receiver = receiver;
    full.enableItm = false;
    full.enableGroundReflection = false;
    full.enableRain = false;
    full.enableFresnelPenalty = false;
    const FieldGrid fullGrid = full.solve(terrain, antennas, materials);

    for (const FieldGrid* grid : {&fastGrid, &fullGrid}) {
        CHECK(grid->servingSourceIndex.size() == grid->dbm.size());
        CHECK(grid->strongestJammerIndex.size() == grid->dbm.size());
        for (std::size_t i = 0; i < grid->dbm.size(); ++i) {
            CHECK(grid->servingSourceIndex[i] == 2);
            CHECK(grid->strongestJammerIndex[i] == 4);
        }
    }
}

void testJammerOnlyPointExplanation() {
    TerrainModel terrain = flatTerrain(11, 11, 20.0f);
    MaterialLibrary materials;
    FastSolver solver;
    solver.gridResolution = 40.0f;
    Antenna jammer;
    jammer.position = {100.0f, 20.0f, 100.0f};
    jammer.isJammer = true;
    jammer.frequencyMHz = 450.0f;
    const std::vector<Antenna> antennas{jammer};
    const FieldGrid fastGrid = solver.solve(terrain, antennas, materials);
    FullSolver full;
    full.gridResolution = 40.0f;
    full.enableItm = false;
    full.enableGroundReflection = false;
    full.enableRain = false;
    full.enableFresnelPenalty = false;
    const FieldGrid fullGrid = full.solve(terrain, antennas, materials);
    for (const FieldGrid* grid : {&fastGrid, &fullGrid}) {
        const PointAnalysisResult analysis = analyzeFieldPoint(
            *grid, terrain, antennas, solver.receiver, 100.0f, 100.0f);
        CHECK(analysis.valid);
        CHECK(analysis.servingSourceIndex == -1);
        CHECK(analysis.strongestJammerIndex == 0);
        CHECK(std::isfinite(analysis.jammerDbm));
        CHECK(std::any_of(analysis.explanations.begin(), analysis.explanations.end(),
            [](const std::string& text) { return text.find("No emitter") != std::string::npos; }));
        CHECK(std::any_of(analysis.explanations.begin(), analysis.explanations.end(),
            [](const std::string& text) { return text.find("jammer") != std::string::npos; }));
    }
}

void testPartialEdgeCellsUseTerrainBoundary() {
    TerrainModel terrain = flatTerrain(5, 5, 20.0f); // 80 m × 80 m.
    MaterialLibrary materials;
    Antenna emitter;
    emitter.position = {0.0f, 20.0f, 0.0f};
    emitter.powerW = 1.0f;
    emitter.frequencyMHz = 450.0f;
    const glm::vec3 boundaryReceiver{80.0f, 1.5f, 80.0f};
    const float distanceM = glm::distance(emitter.position, boundaryReceiver);
    const float txPowerDbm = 10.0f * std::log10(emitter.powerW * 1000.0f);
    const ReceiverSettings receiver;
    const float gainDbi = RFModels::effectiveAntennaGainDbi(
        emitter, boundaryReceiver, receiver);
    const float materialLossDb = MaterialLibrary::defaultMaterial()
        .lossDbPer100m(emitter.frequencyMHz) * distanceM / 100.0f;
    const float expectedDbm = txPowerDbm -
        (20.0f * std::log10(distanceM) +
         20.0f * std::log10(emitter.frequencyMHz * 1.0e6f) - 147.55f - gainDbi +
         materialLossDb);

    FastSolver fast;
    fast.gridResolution = 60.0f; // Last nominal center is 90 m, outside the AOI.
    const FieldGrid fastGrid = fast.solve(terrain, {emitter}, materials);

    FullSolver full;
    full.gridResolution = 60.0f;
    full.enableItm = false;
    full.enableGroundReflection = false;
    full.enableRain = false;
    full.enableFresnelPenalty = false;
    const FieldGrid fullGrid = full.solve(terrain, {emitter}, materials);

    for (const FieldGrid* grid : {&fastGrid, &fullGrid}) {
        CHECK(grid->width == 2 && grid->height == 2);
        NEAR(grid->dbm.back(), expectedDbm, 0.02);
        const PointAnalysisResult analysis = analyzeFieldPoint(
            *grid, terrain, {emitter}, receiver, 80.0f, 80.0f);
        NEAR(analysis.receiverPosition.x, 80.0, 0.001);
        NEAR(analysis.receiverPosition.z, 80.0, 0.001);
    }
}

void testAggregateDesiredPowerNamesStrongestContributor() {
    TerrainModel terrain = flatTerrain(11, 11, 20.0f);
    MaterialLibrary materials;
    Antenna first;
    first.position = {100.0f, 20.0f, 100.0f};
    first.frequencyMHz = 450.0f;
    first.powerW = 1.0f;
    Antenna second = first;
    FullSolver solver;
    solver.gridResolution = 40.0f;
    solver.enableItm = false;
    solver.enableGroundReflection = false;
    solver.enableRain = false;
    solver.enableFresnelPenalty = false;
    const std::vector<Antenna> antennas{first, second};
    const FieldGrid grid = solver.solve(terrain, antennas, materials);
    const PointAnalysisResult analysis = analyzeFieldPoint(
        grid, terrain, antennas, solver.receiver, 180.0f, 180.0f);
    CHECK(analysis.servingSourceIndex == 0);
    NEAR(analysis.desiredDbm - analysis.strongestSourceDbm, 3.0103, 0.02);
    CHECK(std::any_of(analysis.explanations.begin(), analysis.explanations.end(),
        [](const std::string& text) {
            return text.find("co-channel emitters") != std::string::npos;
        }));
}

void testLongProfileIncludesEffectiveEarthCurvature() {
    TerrainModel terrain = flatTerrain(2001, 2, 10.0f);
    Antenna emitter;
    emitter.position = {0.0f, 30.0f, 5.0f};
    emitter.frequencyMHz = 450.0f;
    FieldGrid grid;
    grid.width = 1;
    grid.height = 1;
    grid.cellSize = 10.0f;
    grid.originXZ = {20000.0f, 5.0f};
    grid.maxXZ = {20000.0f, 10.0f};
    grid.dbm = {-80.0f};
    grid.jammerDbm = {-std::numeric_limits<float>::infinity()};
    grid.snrDb = {40.0f};
    grid.sinrDb = {40.0f};
    grid.jOverSDb = {-std::numeric_limits<float>::infinity()};
    grid.fadeMarginDb = {20.0f};
    grid.service = {1};
    grid.servingSourceIndex = {0};
    grid.strongestJammerIndex = {-1};
    const PointAnalysisResult analysis = analyzeFieldPoint(
        grid, terrain, {emitter}, ReceiverSettings{}, 20000.0f, 5.0f);
    CHECK(analysis.profileDistanceM.size() == 2049u);
    const auto middle = analysis.terrainElevationM.size() / 2;
    CHECK(analysis.terrainElevationM[middle] > 1.0f);
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
    CHECK(std::all_of(outOfBand.strongestJammerIndex.begin(),
                      outOfBand.strongestJammerIndex.end(),
                      [](std::int32_t index) { return index == -1; }));

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
    testPointAnalysisExplainsSolvedCell();
    testSolverContributorProvenance();
    testJammerOnlyPointExplanation();
    testPartialEdgeCellsUseTerrainBoundary();
    testAggregateDesiredPowerNamesStrongestContributor();
    testLongProfileIncludesEffectiveEarthCurvature();
    testFullSolverBandwidthFiltering();
    testItmAdapter();
    testScenarioV4RoundTrip();
    if (failures == 0) std::cout << "RF model tests passed.\n";
    return failures ? 1 : 0;
}
