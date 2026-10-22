#include "sim/VegetationAreas.h"
#include "sim/RFModels.h"
#include "sim/SolveSignature.h"
#include "data/TerrainLoader.h"
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

void testFieldGridValidity() {
    FieldGrid grid;
    grid.dbm = {-80.0f};
    CHECK(!grid.valid()); // A buffer alone is not a valid raster.
    grid.width = 2; grid.height = 1;
    CHECK(!grid.valid());
    grid.dbm.push_back(-std::numeric_limits<float>::infinity());
    CHECK(grid.valid()); // No signal is a legitimate solved result.
    grid.cellSize = 0;
    CHECK(!grid.valid());
    grid.cellSize = 10;
    grid.originXZ.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!grid.valid());
    grid.originXZ.x = 0;
    grid.height = -1;
    CHECK(!grid.valid());
    CHECK(grid.cellCount() == 0);
}

template <typename Solver>
void testInvalidSolveInputs() {
    Solver solver;
    solver.gridResolution = 40;
    const TerrainModel terrain = flatTerrain(5, 5, 20);
    MaterialLibrary materials;
    Antenna source;
    source.position = {40, 10, 40};
    auto rejects = [&](const TerrainModel& t, const Antenna& a, const char* message) {
        bool rejected = false;
        int progressCalls = 0;
        try { solver.solve(t, {a}, materials, [&](int, int) { ++progressCalls; }); }
        catch (const std::invalid_argument& e) {
            rejected = std::string(e.what()).find(message) != std::string::npos;
        }
        CHECK(rejected);
        CHECK(progressCalls == 0); // Fail before publishing progress or sampling.
    };
    auto broken = terrain;
    broken.heights.pop_back();
    rejects(broken, source, "complete terrain grid");
    broken = terrain;
    broken.heights[0] = std::numeric_limits<float>::quiet_NaN();
    rejects(broken, source, "missing elevation");
    broken = terrain;
    broken.cellSizeX = std::numeric_limits<float>::quiet_NaN();
    rejects(broken, source, "sample spacing");
    broken = terrain;
    broken.setGeoReference(100, 0, .001, .001);
    rejects(broken, source, "WGS84 reference");
    auto outside = source;
    outside.position.x = terrain.worldSize().x + 1;
    rejects(terrain, outside, "outside analysis area");
    outside = source;
    outside.position.y = std::numeric_limits<float>::infinity();
    rejects(terrain, outside, "position must be finite");
    auto badRadio = source;
    badRadio.frequencyMHz = 0;
    rejects(terrain, badRadio, "RF settings");
    badRadio = source;
    badRadio.azimuthPatternDb = {0, std::numeric_limits<float>::quiet_NaN()};
    rejects(terrain, badRadio, "antenna pattern");
    solver.gridResolution = 0;
    rejects(terrain, source, "grid resolution");
    solver.gridResolution = 1e-10f;
    rejects(terrain, source, "grid is too large");
    solver.gridResolution = 40;
    solver.receiver.bandwidthHz = -1;
    rejects(terrain, source, "Receiver settings");
    solver.receiver.bandwidthHz = 25000;
    CHECK(solver.solve(terrain, {source}, materials).valid());
}

void testRejectedTerrainPreservesScenario() {
    SimEngine engine;
    auto terrain = flatTerrain(5, 5, 20);
    engine.setTerrain(terrain);
    Antenna source;
    source.position = {40, 10, 40};
    engine.addAntenna(source);
    engine.mode = SimEngine::Mode::Preview;
    engine.solve();
    const auto field = engine.fieldGrid();
    terrain.heights.pop_back();
    bool rejected = false;
    try { engine.setTerrain(terrain); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    CHECK(engine.terrain().heights.size() == 25);
    CHECK(engine.antennas()[0].position == source.position);
    CHECK(engine.fieldGrid().dbm == field.dbm);
    CHECK(engine.fieldGrid().valid());
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
    CHECK(analysis.canopyTopElevationM.empty());
    CHECK(analysis.foliageDepthM == 0.0f);
    CHECK(analysis.foliageLossDb == 0.0f);
}

// The inspector must charge a path exactly what the solver charged it, so the
// explanation can never disagree with the map.
void testPointAnalysisExplainsFoliageLoss() {
    TerrainModel bare = flatTerrain(41, 41, 20.0f);
    TerrainModel wooded = bare;
    wooded.canopyHeightDm.assign(wooded.heights.size(), 200); // 20 m everywhere.
    MaterialLibrary materials;
    FastSolver solver;
    solver.gridResolution = 40.0f;
    solver.receiver.receiverHeightAglM = 2.0f;

    Antenna emitter;
    emitter.position = {100.0f, 10.0f, 400.0f};
    emitter.frequencyMHz = 450.0f;
    emitter.powerW = 10.0f;
    const std::vector<Antenna> antennas{emitter};

    const FieldGrid bareGrid = solver.solve(bare, antennas, materials);
    const FieldGrid woodedGrid = solver.solve(wooded, antennas, materials);
    const PointAnalysisResult open = analyzeFieldPoint(
        bareGrid, bare, antennas, solver.receiver, 500.0f, 400.0f);
    const PointAnalysisResult woods = analyzeFieldPoint(
        woodedGrid, wooded, antennas, solver.receiver, 500.0f, 400.0f);
    CHECK(open.valid && woods.valid);
    CHECK(woods.servingSourceIndex == 0);

    CHECK(woods.foliageDepthM > 300.0f);
    NEAR(woods.foliageLossDb,
         RFModels::vegetationLossDb(woods.foliageDepthM, emitter.frequencyMHz), 0.001);
    CHECK(woods.foliageLossDb > 1.0f);
    NEAR(open.desiredDbm - woods.desiredDbm, woods.foliageLossDb, 0.05);
    CHECK(std::any_of(woods.explanations.begin(), woods.explanations.end(),
        [](const std::string& text) { return text.find("vegetation") != std::string::npos; }));
    CHECK(std::none_of(open.explanations.begin(), open.explanations.end(),
        [](const std::string& text) { return text.find("vegetation") != std::string::npos; }));

    CHECK(woods.canopyTopElevationM.size() == woods.terrainElevationM.size());
    const std::size_t middle = woods.terrainElevationM.size() / 2;
    NEAR(woods.canopyTopElevationM[middle] - woods.terrainElevationM[middle], 20.0, 0.001);
}

// Each result cell says which model produced its serving path, and an ITM
// fallback says why, because each fallback has a different remedy.
void testPointAnalysisNamesPathModel() {
    auto mentions = [](const PointAnalysisResult& analysis, const char* text) {
        return std::any_of(analysis.explanations.begin(), analysis.explanations.end(),
            [text](const std::string& item) { return item.find(text) != std::string::npos; });
    };
    TerrainModel terrain = flatTerrain(151, 151, 20.0f); // 3 km square.
    MaterialLibrary materials;
    Antenna emitter;
    emitter.position = {100.0f, 30.0f, 100.0f};
    emitter.frequencyMHz = 450.0f;
    emitter.powerW = 10.0f;
    const std::vector<Antenna> antennas{emitter};

    FastSolver fast;
    fast.gridResolution = 200.0f;
    const FieldGrid fastGrid = fast.solve(terrain, antennas, materials);
    const PointAnalysisResult quick = analyzeFieldPoint(
        fastGrid, terrain, antennas, fast.receiver, 500.0f, 500.0f);
    CHECK(quick.pathModel == PathModel::QuickEstimate);
    CHECK(!mentions(quick, "flat 20 dB")); // A clear path needs no caveat.

    FullSolver full;
    full.gridResolution = 200.0f;
    full.enableItm = false;
    const FieldGrid analyticGrid = full.solve(terrain, antennas, materials);
    const PointAnalysisResult analytic = analyzeFieldPoint(
        analyticGrid, terrain, antennas, full.receiver, 500.0f, 500.0f);
    CHECK(analytic.pathModel == PathModel::TerrainAnalytic);
    CHECK(!mentions(analytic, "ITM"));

    full.enableItm = true;
    const FieldGrid itmGrid = full.solve(terrain, antennas, materials);
    const PointAnalysisResult near = analyzeFieldPoint(
        itmGrid, terrain, antennas, full.receiver, 500.0f, 500.0f);
    CHECK(near.pathModel == PathModel::AnalyticShortPath);
    CHECK(mentions(near, "under 1 km"));
    const PointAnalysisResult far = analyzeFieldPoint(
        itmGrid, terrain, antennas, full.receiver, 2500.0f, 2500.0f);
    CHECK(far.pathModel == PathModel::Itm);
    CHECK(!mentions(far, "ITM"));

    Antenna microwave = emitter;
    microwave.frequencyMHz = 24000.0f;
    const FieldGrid bandGrid = full.solve(terrain, {microwave}, materials);
    const PointAnalysisResult band = analyzeFieldPoint(
        bandGrid, terrain, {microwave}, full.receiver, 2500.0f, 2500.0f);
    CHECK(band.pathModel == PathModel::AnalyticOutOfBand);
    CHECK(mentions(band, "20 MHz to 20 GHz"));

    CHECK(pathModelName(PathModel::Itm) == "NTIA ITM");
    CHECK(pathModelName(PathModel::QuickEstimate) != pathModelName(PathModel::TerrainAnalytic));
    CHECK(pathModelName(PathModel::AnalyticShortPath).find("ITM not used") != std::string::npos);
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
    ReceiverSettings receiver;
    receiver.receiverHeightAglM = 12.5f;
    const glm::vec3 boundaryReceiver{
        80.0f, receiver.receiverHeightAglM, 80.0f};
    const float distanceM = glm::distance(emitter.position, boundaryReceiver);
    const float txPowerDbm = 10.0f * std::log10(emitter.powerW * 1000.0f);
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
    fast.receiver = receiver;
    const FieldGrid fastGrid = fast.solve(terrain, {emitter}, materials);

    FullSolver full;
    full.gridResolution = 60.0f;
    full.receiver = receiver;
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
        NEAR(analysis.receiverPosition.y, receiver.receiverHeightAglM, 0.001);
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

void testScenarioV5RoundTrip() {
    ScenarioDoc doc;
    doc.receiver.receiverHeightAglM = 7.25f;
    doc.receiver.sensitivityDbm = -112.0f;
    doc.receiver.bandwidthHz = 12500.0f;
    doc.receiver.noiseFigureDb = 3.5f;
    doc.receiver.requiredSnrDb = 8.0f;
    doc.receiver.fadeMarginTargetDb = 12.0f;
    doc.receiver.polarization = "horizontal";
    doc.calibration.enabled = true;
    doc.calibration.desiredPowerOffsetDb = 4.25f;
    doc.calibrationSourceName = "drive-test.csv";
    doc.calibrationPairedSamples = 17;
    doc.calibrationRmseBeforeDb = 8.5f;
    doc.calibrationRmseAfterDb = 4.0f;
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

    const std::string path = "test_scenario_v5.json";
    ScenarioIO::save(doc, path);
    const ScenarioDoc loaded = ScenarioIO::load(path);
    std::remove(path.c_str());

    NEAR(loaded.receiver.receiverHeightAglM, 7.25, 0.001);
    NEAR(loaded.receiver.sensitivityDbm, -112.0, 0.001);
    NEAR(loaded.receiver.bandwidthHz, 12500.0, 0.1);
    CHECK(loaded.receiver.polarization == "horizontal");
    CHECK(loaded.calibration.enabled);
    NEAR(loaded.calibration.desiredPowerOffsetDb, 4.25, 0.001);
    CHECK(loaded.calibrationSourceName == "drive-test.csv");
    CHECK(loaded.calibrationPairedSamples == 17u);
    NEAR(loaded.calibrationRmseBeforeDb, 8.5, 0.001);
    NEAR(loaded.calibrationRmseAfterDb, 4.0, 0.001);
    CHECK(loaded.antennas.size() == 1u);
    NEAR(loaded.antennas[0].azimuthDeg, 123.0, 0.001);
    NEAR(loaded.antennas[0].cableLossDb, 1.7, 0.001);
    CHECK(loaded.antennas[0].azimuthPatternDb.size() == 3u);
    CHECK(loaded.antennas[0].patternSource == "measured.csv");
}

void testLegacyScenarioDefaultsReceiverHeight() {
    const std::string path = "test_scenario_v4_receiver_default.json";
    {
        std::ofstream file(path);
        file << R"({
            "version": 4,
            "receiver": {
                "sensitivity_dbm": -105.0,
                "bandwidth_hz": 25000.0
            }
        })";
    }

    const ScenarioDoc loaded = ScenarioIO::load(path);
    std::remove(path.c_str());

    NEAR(loaded.receiver.receiverHeightAglM, 1.5, 0.001);
    NEAR(loaded.receiver.sensitivityDbm, -105.0, 0.001);
    CHECK(!loaded.calibration.enabled);
    NEAR(loaded.calibration.desiredPowerOffsetDb, 0.0, 0.001);
}
}


// A coverage field is only meaningful for the inputs it came from. This
// fingerprint is what makes a displayed result detectably out of date.
void testSolveSignatureTracksEveryInput() {
    std::vector<Antenna> antennas(1);
    antennas[0].position = {100.0f, 30.0f, 200.0f};
    ReceiverSettings receiver;
    CoverageCalibrationSettings calibration;
    TerrainModel terrain = TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f);
    const auto sig = [&] {
        return SolveSignature::of(antennas, receiver, SimEngine::Mode::Fast,
                                  calibration, terrain);
    };
    const std::string base = sig();
    CHECK(!base.empty());
    CHECK(sig() == base);

    antennas[0].frequencyMHz += 1.0f;   CHECK(sig() != base); antennas[0].frequencyMHz -= 1.0f;
    antennas[0].powerW += 1.0f;         CHECK(sig() != base); antennas[0].powerW -= 1.0f;
    antennas[0].position.x += 1.0f;     CHECK(sig() != base); antennas[0].position.x -= 1.0f;
    antennas[0].azimuthDeg += 5.0f;     CHECK(sig() != base); antennas[0].azimuthDeg -= 5.0f;
    antennas[0].cableLossDb += 1.0f;    CHECK(sig() != base); antennas[0].cableLossDb -= 1.0f;
    antennas[0].isJammer = true;        CHECK(sig() != base); antennas[0].isJammer = false;
    antennas[0].azimuthPatternDb = {1.0f, 2.0f};
    CHECK(sig() != base);
    antennas[0].azimuthPatternDb.clear();
    CHECK(sig() == base);

    receiver.sensitivityDbm -= 1.0f;      CHECK(sig() != base); receiver.sensitivityDbm += 1.0f;
    receiver.requiredSnrDb += 1.0f;       CHECK(sig() != base); receiver.requiredSnrDb -= 1.0f;
    receiver.polarization = "horizontal"; CHECK(sig() != base); receiver.polarization = "vertical";
    calibration.enabled = true;           CHECK(sig() != base); calibration.enabled = false;
    CHECK(SolveSignature::of(antennas, receiver, SimEngine::Mode::Full, calibration, terrain) != base);

    antennas.push_back(Antenna{});        CHECK(sig() != base); antennas.pop_back();
    CHECK(sig() == base);

    TerrainModel other = TerrainLoader::makeFlat(64, 64, 45.0f, 0.0f);
    CHECK(SolveSignature::of(antennas, receiver, SimEngine::Mode::Fast, calibration, other) != base);

    // Vegetation changes the result, so it must change the fingerprint - both
    // adding it and changing its height.
    TerrainModel wooded = TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f);
    VegetationAreas::apply(wooded, {{100.0f, 100.0f, 500.0f, 500.0f, 15.0f, "wood"}});
    const std::string woodedSig =
        SolveSignature::of(antennas, receiver, SimEngine::Mode::Fast, calibration, wooded);
    CHECK(woodedSig != base);

    TerrainModel taller = TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f);
    VegetationAreas::apply(taller, {{100.0f, 100.0f, 500.0f, 500.0f, 20.0f, "wood"}});
    CHECK(SolveSignature::of(antennas, receiver, SimEngine::Mode::Fast, calibration, taller)
          != woodedSig);

    TerrainModel moved = TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f);
    VegetationAreas::apply(moved, {{200.0f, 200.0f, 600.0f, 600.0f, 15.0f, "wood"}});
    CHECK(SolveSignature::of(antennas, receiver, SimEngine::Mode::Fast, calibration, moved)
          != woodedSig);

    // Clearing it returns to the bare fingerprint.
    TerrainModel cleared = TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f);
    VegetationAreas::apply(cleared, {});
    CHECK(SolveSignature::of(antennas, receiver, SimEngine::Mode::Fast, calibration, cleared)
          == SolveSignature::of(antennas, receiver, SimEngine::Mode::Fast, calibration,
                                TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f)));
}


// Vegetation attenuation. The shape matters more than any single number: the
// model this replaces was unbounded linear and reached ~96 dB at 1 km of
// woodland against a measured asymptote near 26.5 dB.
void testVegetationLossSaturates() {
    // No vegetation, no loss. Degenerate inputs must not produce one either.
    CHECK(RFModels::vegetationLossDb(0.0f, 949.0f) == 0.0f);
    CHECK(RFModels::vegetationLossDb(-5.0f, 949.0f) == 0.0f);
    CHECK(RFModels::vegetationLossDb(100.0f, 0.0f) == 0.0f);
    CHECK(RFModels::vegetationLossDb(std::numeric_limits<float>::quiet_NaN(), 949.0f) == 0.0f);
    CHECK(RFModels::vegetationLossDb(100.0f, std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    // Shallow depth behaves like the specific attenuation, since the
    // exponential is still nearly linear there.
    const float gamma949 = RFModels::vegetationGammaDbPerM(949.0f);
    CHECK(std::abs(gamma949 - 0.17f) < 1e-4f);
    const float shallow = RFModels::vegetationLossDb(10.0f, 949.0f);
    CHECK(std::abs(shallow - gamma949 * 10.0f) < 0.1f);

    // THE POINT OF THE MODEL: it saturates. Ten times the depth must not give
    // ten times the loss, and the result must stay under the ceiling forever.
    const float ceiling = RFModels::vegetationMaxLossDb(949.0f);
    CHECK(std::abs(ceiling - 26.5f) < 1e-4f);
    const float deep = RFModels::vegetationLossDb(1000.0f, 949.0f);
    CHECK(deep < ceiling);
    CHECK(deep > 0.9f * ceiling);              // but close to it by 1 km
    // Asymptotic: at absurd depth it reaches the ceiling in float arithmetic
    // and must never exceed it.
    CHECK(RFModels::vegetationLossDb(100000.0f, 949.0f) <= ceiling);
    // Sub-linear where it matters: a linear dB/m form would give gamma*1000,
    // which is what produced ~96 dB at 1 km before.
    CHECK(deep < 0.25f * gamma949 * 1000.0f);

    // Monotonic in depth, and never negative.
    float previous = 0.0f;
    for (float d = 0.0f; d <= 2000.0f; d += 25.0f) {
        const float loss = RFModels::vegetationLossDb(d, 949.0f);
        CHECK(loss >= previous - 1e-4f);
        CHECK(loss >= 0.0f);
        previous = loss;
    }

    // Higher frequency attenuates more, across the anchored range.
    CHECK(RFModels::vegetationLossDb(50.0f, 2118.0f) >
          RFModels::vegetationLossDb(50.0f, 105.9f));

    // Held flat outside the anchors rather than extrapolated: P.833's own Am
    // values stop being monotone above ~2 GHz, and a monotone fit reaches ~94 dB
    // at 28 GHz against a measured ~34.5 dB.
    CHECK(RFModels::vegetationMaxLossDb(28000.0f) == RFModels::vegetationMaxLossDb(2118.0f));
    CHECK(RFModels::vegetationGammaDbPerM(28000.0f) == RFModels::vegetationGammaDbPerM(2118.0f));
    CHECK(RFModels::vegetationMaxLossDb(1.0f) == RFModels::vegetationMaxLossDb(105.9f));
    CHECK(RFModels::vegetationLossDb(1000.0f, 28000.0f) < 40.0f);

    // A 1 km stand must never approach the ~96 dB the previous linear form gave.
    CHECK(RFModels::vegetationLossDb(1000.0f, 949.0f) < 30.0f);
}


// Depth measurement and the end-to-end effect on a solve. The model is only
// useful if the geometry feeding it is right.
void testVegetationDepthAndSolveEffect() {
    // Flat ground, a band of 20 m canopy across the middle of the scene.
    TerrainModel t = TerrainLoader::makeFlat(64, 64, 10.0f, 0.0f);
    CHECK(!t.hasVegetation());
    CHECK(t.foliageDepthM({0, 5, 0}, {600, 5, 0}) == 0.0f);

    t.canopyHeightDm.assign(t.heights.size(), 0);
    for (int z = 0; z < t.height; ++z)
        for (int x = 20; x < 40; ++x)
            t.canopyHeightDm[z * t.width + x] = 200;   // 20.0 m
    CHECK(t.hasVegetation());
    t.validate();   // must accept a matching grid

    // A ray at 5 m altitude crosses the band: about 200 m of canopy between
    // x=200 and x=400 on a 10 m grid.
    const float depth = t.foliageDepthM({0.0f, 5.0f, 300.0f}, {630.0f, 5.0f, 300.0f});
    CHECK(depth > 150.0f && depth < 250.0f);

    // Above the canopy there is no vegetation on the path at all.
    CHECK(t.foliageDepthM({0.0f, 40.0f, 300.0f}, {630.0f, 40.0f, 300.0f}) < 1.0f);

    // A path entirely outside the band never enters it.
    CHECK(t.foliageDepthM({0.0f, 5.0f, 300.0f}, {150.0f, 5.0f, 300.0f}) == 0.0f);

    // A mismatched grid is a bug, not a partially usable model.
    TerrainModel bad = t;
    bad.canopyHeightDm.pop_back();
    bool rejected = false;
    try { bad.validate(); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);

    // End to end: the same scene with and without vegetation must differ, and
    // vegetation must never improve the signal.
    std::vector<Antenna> antennas(1);
    antennas[0].position = {50.0f, 15.0f, 300.0f};
    antennas[0].frequencyMHz = 949.0f;
    antennas[0].powerW = 50.0f;
    MaterialLibrary materials;
    FastSolver solver;
    solver.gridResolution = 40.0f;

    TerrainModel bare = TerrainLoader::makeFlat(64, 64, 10.0f, 0.0f);
    const FieldGrid withoutVeg = solver.solve(bare, antennas, materials);
    const FieldGrid withVeg = solver.solve(t, antennas, materials);
    CHECK(withoutVeg.valid() && withVeg.valid());
    CHECK(withoutVeg.dbm.size() == withVeg.dbm.size());

    int weaker = 0, stronger = 0;
    for (std::size_t i = 0; i < withVeg.dbm.size(); ++i) {
        if (!std::isfinite(withVeg.dbm[i]) || !std::isfinite(withoutVeg.dbm[i])) continue;
        if (withVeg.dbm[i] < withoutVeg.dbm[i] - 0.01f) ++weaker;
        if (withVeg.dbm[i] > withoutVeg.dbm[i] + 0.01f) ++stronger;
    }
    CHECK(weaker > 0);      // vegetation actually reaches the solve
    CHECK(stronger == 0);   // and never helps
}


// Hand-drawn stands, stored as areas and rasterised into the canopy grid.
void testVegetationAreasRasterise() {
    TerrainModel t = TerrainLoader::makeFlat(64, 64, 10.0f, 0.0f);   // 630 m square

    // No areas means no vegetation at all, not a grid of zeroes that
    // hasVegetation() would then report as present.
    VegetationAreas::apply(t, {});
    CHECK(!t.hasVegetation());
    CHECK(t.canopyHeightDm.empty());

    // An invalid area is ignored rather than producing an empty stand.
    VegetationAreas::apply(t, {{100.0f, 100.0f, 50.0f, 200.0f, 15.0f, "inverted"}});
    CHECK(!t.hasVegetation());
    VegetationAreas::apply(t, {{100.0f, 100.0f, 200.0f, 200.0f, 0.0f, "zero height"}});
    CHECK(!t.hasVegetation());

    // A real stand paints its footprint and nothing else.
    VegetationAreas::apply(t, {{100.0f, 100.0f, 200.0f, 200.0f, 15.0f, "wood"}});
    CHECK(t.hasVegetation());
    CHECK(t.canopyHeightDm.size() == t.heights.size());
    t.validate();
    CHECK(std::abs(t.canopyHeightAt(15, 15) - 15.0f) < 0.06f);   // inside
    CHECK(t.canopyHeightAt(1, 1) == 0.0f);                       // outside
    CHECK(t.canopyHeightAt(60, 60) == 0.0f);

    // Height is clamped to what a decimetre byte can carry, not wrapped.
    VegetationAreas::apply(t, {{0.0f, 0.0f, 630.0f, 630.0f, 400.0f, "impossible"}});
    CHECK(std::abs(t.canopyHeightAt(30, 30) - VegetationAreas::kMaxCanopyHeightM) < 0.06f);

    // Overlapping stands are one canopy at the tallest height, not a sum.
    VegetationAreas::apply(t, {
        {100.0f, 100.0f, 300.0f, 300.0f, 10.0f, "short"},
        {200.0f, 200.0f, 400.0f, 400.0f, 20.0f, "tall"},
    });
    CHECK(std::abs(t.canopyHeightAt(25, 25) - 20.0f) < 0.06f);   // in both
    CHECK(std::abs(t.canopyHeightAt(15, 15) - 10.0f) < 0.06f);   // short only
    CHECK(std::abs(t.canopyHeightAt(38, 38) - 20.0f) < 0.06f);   // tall only

    // An area entirely off the terrain leaves nothing behind.
    VegetationAreas::apply(t, {{5000.0f, 5000.0f, 6000.0f, 6000.0f, 15.0f, "elsewhere"}});
    CHECK(!t.hasVegetation());

    // An area partly off the terrain still paints the part that is on it.
    VegetationAreas::apply(t, {{-500.0f, -500.0f, 150.0f, 150.0f, 12.0f, "overhang"}});
    CHECK(t.hasVegetation());
    CHECK(std::abs(t.canopyHeightAt(0, 0) - 12.0f) < 0.06f);
    CHECK(t.canopyHeightAt(50, 50) == 0.0f);

    // Reapplying replaces rather than accumulating.
    VegetationAreas::apply(t, {{100.0f, 100.0f, 200.0f, 200.0f, 15.0f, "wood"}});
    CHECK(t.canopyHeightAt(0, 0) == 0.0f);
    CHECK(std::abs(t.canopyHeightAt(15, 15) - 15.0f) < 0.06f);

    const VegetationArea probe{0.0f, 0.0f, 100.0f, 100.0f, 10.0f, ""};
    CHECK(probe.contains(50.0f, 50.0f));
    CHECK(!probe.contains(150.0f, 50.0f));
    CHECK(probe.valid());
}

int main() {
    testFieldGridValidity();
    testInvalidSolveInputs<FastSolver>();
    testInvalidSolveInputs<FullSolver>();
    testRejectedTerrainPreservesScenario();
    testNoiseFloor();
    testDirectionalGainAndPolarization();
    testPatternImport();
    testFastSolverMetrics();
    testPointAnalysisExplainsSolvedCell();
    testPointAnalysisExplainsFoliageLoss();
    testPointAnalysisNamesPathModel();
    testSolverContributorProvenance();
    testJammerOnlyPointExplanation();
    testPartialEdgeCellsUseTerrainBoundary();
    testAggregateDesiredPowerNamesStrongestContributor();
    testLongProfileIncludesEffectiveEarthCurvature();
    testFullSolverBandwidthFiltering();
    testItmAdapter();
    testScenarioV5RoundTrip();
    testLegacyScenarioDefaultsReceiverHeight();
    testSolveSignatureTracksEveryInput();
    testVegetationLossSaturates();
    testVegetationDepthAndSolveEffect();
    testVegetationAreasRasterise();
    if (failures == 0) std::cout << "RF model tests passed.\n";
    return failures ? 1 : 0;
}
