#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include "sim/SimEngine.h"
#include "data/ScenarioIO.h"

static int g_fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; \
        ++g_fail; \
    } \
} while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::abs((double)(a) - (double)(b)) <= (double)(tol))

static TerrainModel makeGeoTerrain(int samples, float elevationM) {
    TerrainModel t;
    t.width = samples;
    t.height = samples;
    const double sampleDeg = 0.1 / static_cast<double>(samples - 1);
    const double centerLat = 35.05;
    t.setCellSpacing(
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLat) * sampleDeg),
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLat) * sampleDeg));
    t.setGeoReference(35.0, -80.0, sampleDeg, sampleDeg);
    t.heights.assign(samples * samples, elevationM);
    return t;
}

static void test_engine_tracks_geo_metadata() {
    SimEngine engine;
    engine.setTerrain(makeGeoTerrain(101, 100.0f));

    const auto world = engine.terrain().latLonToWorld(35.04, -79.96);
    Antenna a;
    a.position = {world.x, 112.0f, world.y};
    engine.addAntenna(a);

    CHECK(engine.antennas().size() == 1u);
    const Antenna& stored = engine.antennas().front();
    CHECK(stored.hasGeoPosition);
    CHECK_NEAR(stored.latitudeDeg, 35.04, 1e-8);
    CHECK_NEAR(stored.longitudeDeg, -79.96, 1e-8);
    CHECK_NEAR(stored.heightAglM, 12.0, 1e-4);
}

static void test_engine_reprojects_across_terrain_resolution() {
    SimEngine engine;
    engine.setTerrain(makeGeoTerrain(101, 100.0f));

    const auto world = engine.terrain().latLonToWorld(35.04, -79.96);
    Antenna a;
    a.position = {world.x, 112.0f, world.y};
    engine.addAntenna(a);

    engine.setTerrain(makeGeoTerrain(201, 200.0f));

    const Antenna& moved = engine.antennas().front();
    const auto ll = engine.terrain().worldToLatLon(moved.position.x, moved.position.z);
    CHECK(moved.hasGeoPosition);
    CHECK_NEAR(ll.latDeg, 35.04, 1e-7);
    CHECK_NEAR(ll.lonDeg, -79.96, 1e-7);
    CHECK_NEAR(moved.position.y, 212.0, 1e-3);
    CHECK_NEAR(moved.heightAglM, 12.0, 1e-4);
}

static void test_scenario_geo_round_trip() {
    ScenarioDoc doc;
    doc.terrain.type = TerrainSource::Type::Global;
    doc.terrain.centerLat = 35.05;
    doc.terrain.centerLon = -79.95;
    doc.terrain.extentKm = 10.0;

    Antenna a;
    a.position = {100.0f, 150.0f, 200.0f};
    a.frequencyMHz = 433.92f;
    a.hasGeoPosition = true;
    a.latitudeDeg = 35.041234;
    a.longitudeDeg = -79.961234;
    a.heightAglM = 18.5f;
    doc.antennas.push_back(a);

    const std::string path = "test_scenario_geo_tmp.json";
    ScenarioIO::save(doc, path);
    ScenarioDoc loaded = ScenarioIO::load(path);
    std::remove(path.c_str());

    CHECK(loaded.antennas.size() == 1u);
    const Antenna& restored = loaded.antennas.front();
    CHECK(restored.hasGeoPosition);
    CHECK_NEAR(restored.latitudeDeg, a.latitudeDeg, 1e-9);
    CHECK_NEAR(restored.longitudeDeg, a.longitudeDeg, 1e-9);
    CHECK_NEAR(restored.heightAglM, a.heightAglM, 1e-5);
}

static void test_antimeridian_round_trip() {
    TerrainModel t;
    t.width = 101;
    t.height = 11;
    t.setCellSpacing(445.0f, 1111.0f);
    t.setGeoReference(10.0, 179.8, 0.01, 0.004);
    t.heights.assign(t.width * t.height, 0.0f);

    const auto world = t.latLonToWorld(10.05, -179.9);
    const auto ll = t.worldToLatLon(world.x, world.y);

    CHECK(world.x > 0.0f);
    CHECK(world.x < t.worldSize().x);
    CHECK_NEAR(ll.latDeg, 10.05, 1e-7);
    CHECK_NEAR(ll.lonDeg, -179.9, 1e-7);
}

int main() {
    test_engine_tracks_geo_metadata();
    test_engine_reprojects_across_terrain_resolution();
    test_scenario_geo_round_trip();
    test_antimeridian_round_trip();

    if (g_fail == 0)
        std::cout << "Geospatial antenna tests passed.\n";
    return g_fail ? 1 : 0;
}
