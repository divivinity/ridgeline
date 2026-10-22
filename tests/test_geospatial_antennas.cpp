#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
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

static TerrainModel makeCustomGeoTerrain(int samples, float elevationM,
                                         double south, double west,
                                         double spanDeg = 0.1) {
    TerrainModel t;
    t.width = samples;
    t.height = samples;
    const double sampleDeg = spanDeg / static_cast<double>(samples - 1);
    const double centerLat = south + spanDeg * 0.5;
    t.setCellSpacing(
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLat) * sampleDeg),
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLat) * sampleDeg));
    t.setGeoReference(south, west, sampleDeg, sampleDeg);
    t.heights.assign(samples * samples, elevationM);
    return t;
}

static TerrainModel makeGeoTerrain(int samples, float elevationM) {
    return makeCustomGeoTerrain(samples, elevationM, 35.0, -80.0);
}

// A procedural scene like the default hills: no geo-reference at all, so
// sources on it hold plain world metres and never gain a geo position.
static TerrainModel makeProceduralTerrain(int samples, float cellM,
                                          float elevationM) {
    TerrainModel t;
    t.width = samples;
    t.height = samples;
    t.setCellSpacing(cellM, cellM);
    t.heights.assign(samples * samples, elevationM);
    return t;
}

static void test_procedural_source_keeps_relative_position() {
    SimEngine engine;
    engine.setTerrain(makeProceduralTerrain(201, 10.0f, 40.0f));  // 2 km map
    const glm::vec3 oldSize = engine.terrain().worldSize();

    Antenna a;
    a.position = {oldSize.x * 0.1f, 50.0f, oldSize.z * 0.1f};
    engine.addAntenna(a);
    CHECK(!engine.antennas().front().hasGeoPosition);

    engine.setTerrain(makeGeoTerrain(101, 100.0f));  // ~11 km real tile
    const Antenna& moved = engine.antennas().front();
    const glm::vec3 newSize = engine.terrain().worldSize();

    // Same tenth of the map, not the same metres.
    CHECK_NEAR(moved.position.x, newSize.x * 0.1f, 1.0);
    CHECK_NEAR(moved.position.z, newSize.z * 0.1f, 1.0);
    CHECK(std::abs(moved.position.x - oldSize.x * 0.1f) > 1.0f);
    CHECK(moved.position.x >= 0.0f && moved.position.x <= newSize.x);
    CHECK(moved.position.z >= 0.0f && moved.position.z <= newSize.z);
}

static void test_procedural_source_keeps_height_above_ground() {
    SimEngine engine;
    engine.setTerrain(makeProceduralTerrain(201, 10.0f, 40.0f));
    const glm::vec3 oldSize = engine.terrain().worldSize();

    Antenna a;
    a.position = {oldSize.x * 0.25f, 50.0f, oldSize.z * 0.75f};  // 10 m AGL
    engine.addAntenna(a);

    engine.setTerrain(makeGeoTerrain(101, 100.0f));  // ground rises to 100 m
    const Antenna& moved = engine.antennas().front();
    CHECK_NEAR(moved.position.y, 110.0, 1e-3);
    CHECK_NEAR(moved.heightAglM, 10.0, 1e-4);
}

static void test_procedural_source_gains_geo_position() {
    SimEngine engine;
    engine.setTerrain(makeProceduralTerrain(201, 10.0f, 40.0f));
    Antenna a;
    a.position = {500.0f, 50.0f, 500.0f};
    engine.addAntenna(a);
    CHECK(!engine.antennas().front().hasGeoPosition);

    engine.setTerrain(makeGeoTerrain(101, 100.0f));
    const Antenna& moved = engine.antennas().front();
    CHECK(moved.hasGeoPosition);

    // Having gained one, a further switch must follow the exact geographic
    // path and hold the same real-world spot.
    const double lat = moved.latitudeDeg;
    const double lon = moved.longitudeDeg;
    engine.setTerrain(makeGeoTerrain(201, 200.0f));
    CHECK_NEAR(engine.antennas().front().latitudeDeg, lat, 1e-7);
    CHECK_NEAR(engine.antennas().front().longitudeDeg, lon, 1e-7);
}

static void test_degenerate_previous_terrain_is_safe() {
    SimEngine engine;
    engine.setTerrain(makeProceduralTerrain(1, 10.0f, 0.0f));  // zero extent
    Antenna a;
    a.position = {123.0f, 10.0f, 456.0f};
    engine.addAntenna(a);

    engine.setTerrain(makeGeoTerrain(101, 100.0f));
    const Antenna& moved = engine.antennas().front();
    const glm::vec3 newSize = engine.terrain().worldSize();
    CHECK(std::isfinite(moved.position.x));
    CHECK(std::isfinite(moved.position.z));
    CHECK(std::isfinite(moved.position.y));
    CHECK(moved.position.x >= 0.0f && moved.position.x <= newSize.x);
    CHECK(moved.position.z >= 0.0f && moved.position.z <= newSize.z);
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

static void test_outside_source_is_preserved_not_clamped() {
    SimEngine engine;
    engine.setTerrain(makeGeoTerrain(101, 100.0f));
    const auto world = engine.terrain().latLonToWorld(35.08, -79.92);
    Antenna a;
    a.position = {world.x, 115.0f, world.y};
    engine.addAntenna(a);

    engine.setTerrain(makeCustomGeoTerrain(101, 120.0f, 34.0, -81.0, 0.1));
    const Antenna& moved = engine.antennas().front();
    CHECK(moved.hasGeoPosition);
    CHECK_NEAR(moved.latitudeDeg, 35.08, 1e-7);
    CHECK_NEAR(moved.longitudeDeg, -79.92, 1e-7);
    CHECK(moved.position.x > engine.terrain().worldSize().x ||
          moved.position.z > engine.terrain().worldSize().z ||
          moved.position.x < 0.0f || moved.position.z < 0.0f);
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

static void test_southern_western_round_trip() {
    TerrainModel t = makeCustomGeoTerrain(101, 25.0f, -34.0, -71.0, 0.2);
    const auto world = t.latLonToWorld(-33.91, -70.87);
    const auto ll = t.worldToLatLon(world.x, world.y);
    CHECK_NEAR(ll.latDeg, -33.91, 1e-7);
    CHECK_NEAR(ll.lonDeg, -70.87, 1e-7);
}

static void test_high_latitude_round_trip() {
    TerrainModel t = makeCustomGeoTerrain(101, 15.0f, 79.8, 20.0, 0.2);
    const auto world = t.latLonToWorld(79.91, 20.14);
    const auto ll = t.worldToLatLon(world.x, world.y);
    CHECK(world.x > 0.0f && world.y > 0.0f);
    CHECK_NEAR(ll.latDeg, 79.91, 1e-7);
    CHECK_NEAR(ll.lonDeg, 20.14, 1e-7);
}

static void test_cancelled_solve_does_not_publish_partial_field() {
    SimEngine engine;
    TerrainModel t;
    t.width = 20;
    t.height = 20;
    t.cellSize = 10.0f;
    t.heights.assign(400, 0.0f);
    engine.setTerrain(std::move(t));

    Antenna a;
    a.position = {100.0f, 10.0f, 100.0f};
    engine.addAntenna(a);
    engine.mode = SimEngine::Mode::Engineering;

    bool threw = false;
    try {
        engine.solve({}, [] { return true; });
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()) == "Coverage solve cancelled";
    }

    CHECK(threw);
    CHECK(!engine.fieldGrid().valid());
}

static VegetationArea standAt(float minX, float minZ, float maxX, float maxZ,
                              float heightM = 15.0f) {
    return {minX, minZ, maxX, maxZ, heightM, "stand"};
}

// A drawn stand on one real-world terrain must sit on the same ground after a
// location load moves the terrain's south-west origin, as imported canopy does.
static void test_drawn_vegetation_keeps_its_ground() {
    SimEngine engine;
    const TerrainModel first = makeCustomGeoTerrain(101, 0.0f, 35.0, -80.0, 0.1);
    engine.setTerrain(first);
    engine.setVegetation({standAt(3000.0f, 3000.0f, 4000.0f, 4000.0f)});
    const auto sw = first.worldToLatLon(3000.0f, 3000.0f);
    const auto ne = first.worldToLatLon(4000.0f, 4000.0f);

    engine.setTerrain(makeCustomGeoTerrain(121, 0.0f, 34.98, -80.03, 0.14));
    const TerrainModel& second = engine.terrain();
    CHECK(engine.vegetation().size() == 1);
    const VegetationArea& carried = engine.vegetation()[0];
    const auto carriedSw = second.worldToLatLon(carried.minX, carried.minZ);
    const auto carriedNe = second.worldToLatLon(carried.maxX, carried.maxZ);
    CHECK_NEAR(carriedSw.latDeg, sw.latDeg, 1e-6);
    CHECK_NEAR(carriedSw.lonDeg, sw.lonDeg, 1e-6);
    CHECK_NEAR(carriedNe.latDeg, ne.latDeg, 1e-6);
    CHECK_NEAR(carriedNe.lonDeg, ne.lonDeg, 1e-6);

    const glm::vec2 centre = second.latLonToWorld((sw.latDeg + ne.latDeg) * 0.5,
                                                  (sw.lonDeg + ne.lonDeg) * 0.5);
    const int cx = static_cast<int>(std::lround(centre.x / second.metersPerCellX()));
    const int cz = static_cast<int>(std::lround(centre.y / second.metersPerCellZ()));
    CHECK(second.canopyHeightAt(cx, cz) > 0.0f);

    // The stand's old world metres describe different ground on the new
    // terrain, and nothing is painted there.
    CHECK(!carried.contains(3500.0f, 3500.0f));
    const int ox = static_cast<int>(std::lround(3500.0f / second.metersPerCellX()));
    const int oz = static_cast<int>(std::lround(3500.0f / second.metersPerCellZ()));
    CHECK(second.canopyHeightAt(ox, oz) == 0.0f);
}

// A location on another continent carries a drawn area tens of thousands of
// kilometres off the new terrain. It has to stay valid there - counted as
// outside, so it can be found and deleted - and return to its own ground when
// the first location is loaded again. Scenario coordinates are single precision,
// which is about a metre at that distance, hence the tolerance.
static void test_drawn_vegetation_survives_a_far_location() {
    SimEngine engine;
    const TerrainModel asheville = makeCustomGeoTerrain(101, 0.0f, 35.55, -82.60, 0.1);
    engine.setTerrain(asheville);
    engine.setVegetation({standAt(3000.0f, 3000.0f, 4000.0f, 4000.0f)});

    engine.setTerrain(makeCustomGeoTerrain(101, 0.0f, 35.63, 139.64, 0.1));
    CHECK(engine.vegetation().size() == 1);
    const VegetationArea farAway = engine.vegetation()[0];
    CHECK(std::abs(farAway.minX) > 1.0e7f);
    CHECK(farAway.valid());
    CHECK(VegetationAreas::countOutside(engine.terrain(), engine.vegetation()) == 1);

    engine.setTerrain(asheville);
    CHECK(engine.vegetation().size() == 1);
    const VegetationArea back = engine.vegetation()[0];
    CHECK(back.valid());
    CHECK_NEAR(back.minX, 3000.0, 1.5);
    CHECK_NEAR(back.minZ, 3000.0, 1.5);
    CHECK_NEAR(back.maxX, 4000.0, 1.5);
    CHECK_NEAR(back.maxZ, 4000.0, 1.5);
    const int cx = static_cast<int>(std::lround(3500.0f / engine.terrain().metersPerCellX()));
    const int cz = static_cast<int>(std::lround(3500.0f / engine.terrain().metersPerCellZ()));
    CHECK(engine.terrain().canopyHeightAt(cx, cz) > 0.0f);
}

// A drawn area half a world from the new terrain's origin can have its two
// corners land on opposite sides of the longitude seam. Projected separately
// they once made a rectangle around the whole planet, which overlapped the new
// terrain and planted canopy across it. Both corners must stay on one branch.
static void test_drawn_vegetation_keeps_its_width_across_the_seam() {
    SimEngine engine;
    const TerrainModel greenwich = makeCustomGeoTerrain(101, 0.0f, -0.05, -0.05, 0.1);
    engine.setTerrain(greenwich);
    // About -0.005 to +0.005 degrees of longitude: it straddles the meridian.
    const float half = static_cast<float>(TerrainModel::metersPerDegreeLon(0.0) * 0.005);
    const float centreX = greenwich.worldSize().x * 0.5f;
    const float centreZ = greenwich.worldSize().z * 0.5f;
    const VegetationArea stand = standAt(centreX - half, centreZ - 500.0f,
                                         centreX + half, centreZ + 500.0f);
    engine.setVegetation({stand});

    engine.setTerrain(makeCustomGeoTerrain(101, 0.0f, -0.05, 179.999, 0.1));
    CHECK(engine.vegetation().size() == 1);
    const VegetationArea carried = engine.vegetation()[0];
    CHECK(carried.valid());
    // Half a world from the origin a float resolves about 2 m, hence the tolerance.
    CHECK_NEAR(carried.maxX - carried.minX, stand.maxX - stand.minX, 3.0);
    CHECK(VegetationAreas::countOutside(engine.terrain(), engine.vegetation()) == 1);
    const TerrainModel& far = engine.terrain();
    bool painted = false;
    for (int z = 0; z < far.height && !painted; ++z)
        for (int x = 0; x < far.width && !painted; ++x)
            painted = far.canopyHeightAt(x, z) > 0.0f;
    CHECK(!painted);

    engine.setTerrain(greenwich);
    const VegetationArea back = engine.vegetation()[0];
    CHECK_NEAR(back.minX, stand.minX, 1.5);
    CHECK_NEAR(back.maxX, stand.maxX, 1.5);
}

// With no georeference on either side there is no shared frame, so areas keep
// their metres exactly rather than being moved by a guess.
static void test_drawn_vegetation_verbatim_without_georeference() {
    SimEngine engine;
    engine.setTerrain(makeProceduralTerrain(101, 10.0f, 0.0f));
    const VegetationArea stand = standAt(120.5f, 230.25f, 480.75f, 610.125f);
    engine.setVegetation({stand});
    engine.setTerrain(makeProceduralTerrain(201, 5.0f, 0.0f));
    CHECK(engine.vegetation().size() == 1);
    CHECK(engine.vegetation()[0].minX == stand.minX);
    CHECK(engine.vegetation()[0].minZ == stand.minZ);
    CHECK(engine.vegetation()[0].maxX == stand.maxX);
    CHECK(engine.vegetation()[0].maxZ == stand.maxZ);

    engine.setTerrain(makeCustomGeoTerrain(101, 0.0f, 35.0, -80.0, 0.1));
    engine.setVegetation({stand});
    engine.setTerrain(makeProceduralTerrain(101, 10.0f, 0.0f));
    CHECK(engine.vegetation()[0].minX == stand.minX);
    CHECK(engine.vegetation()[0].maxZ == stand.maxZ);
}

// A rename changes nothing the solver reads, so it must not rebuild the canopy
// and bump the revision that invalidates results and rebuilds the map texture.
static void test_vegetation_rename_does_not_rebuild_canopy() {
    SimEngine engine;
    engine.setTerrain(makeProceduralTerrain(64, 10.0f, 0.0f));
    engine.setVegetation({standAt(100.0f, 100.0f, 200.0f, 200.0f)});
    const auto revision = engine.canopyRevision();

    VegetationArea renamed = engine.vegetation()[0];
    renamed.name = "renamed";
    engine.setVegetation({renamed});
    CHECK(engine.canopyRevision() == revision);
    CHECK(engine.vegetation()[0].name == "renamed");

    VegetationArea taller = renamed;
    taller.canopyHeightM = 16.0f;
    engine.setVegetation({taller});
    CHECK(engine.canopyRevision() == revision + 1);

    VegetationArea felled = taller;
    felled.kind = VegetationKind::Clearing;
    engine.setVegetation({felled});
    CHECK(engine.canopyRevision() == revision + 2);
}

int main() {
    test_procedural_source_keeps_relative_position();
    test_procedural_source_keeps_height_above_ground();
    test_procedural_source_gains_geo_position();
    test_degenerate_previous_terrain_is_safe();
    test_engine_tracks_geo_metadata();
    test_engine_reprojects_across_terrain_resolution();
    test_outside_source_is_preserved_not_clamped();
    test_scenario_geo_round_trip();
    test_antimeridian_round_trip();
    test_southern_western_round_trip();
    test_high_latitude_round_trip();
    test_cancelled_solve_does_not_publish_partial_field();

    test_drawn_vegetation_keeps_its_ground();
    test_drawn_vegetation_survives_a_far_location();
    test_drawn_vegetation_keeps_its_width_across_the_seam();
    test_drawn_vegetation_verbatim_without_georeference();
    test_vegetation_rename_does_not_rebuild_canopy();

    if (g_fail == 0)
        std::cout << "Geospatial/cancellation tests passed.\n";
    return g_fail ? 1 : 0;
}
