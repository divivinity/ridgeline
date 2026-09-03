#include "geo/Mgrs.h"
#include "render/TerrainRaycast.h"
#include "sim/TerrainModel.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL: " #condition " (line " << __LINE__ << ")\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK_NEAR(actual, expected, tolerance) do { \
    const auto actualValue = (actual); \
    const auto expectedValue = (expected); \
    if (std::abs(actualValue - expectedValue) > (tolerance)) { \
        std::cerr << "FAIL: " #actual " = " << actualValue \
                  << ", expected " << expectedValue \
                  << " (line " << __LINE__ << ")\n"; \
        ++failures; \
    } \
} while (0)

void testMgrsKnownLocations() {
    MgrsCoordinate coordinate;
    std::string error;

    CHECK(mgrsToWgs84("18S UJ 23394 07395", coordinate, error));
    CHECK_NEAR(coordinate.latitudeDeg, 38.8977, 0.0002);
    CHECK_NEAR(coordinate.longitudeDeg, -77.0365, 0.0002);
    CHECK(coordinate.precisionDigits == 5);

    CHECK(mgrsToWgs84("31U DQ 48251 11932", coordinate, error));
    CHECK_NEAR(coordinate.latitudeDeg, 48.8582, 0.0002);
    CHECK_NEAR(coordinate.longitudeDeg, 2.2945, 0.0002);

    CHECK(mgrsToWgs84("56H LH 34368 50948", coordinate, error));
    CHECK_NEAR(coordinate.latitudeDeg, -33.8688, 0.0002);
    CHECK_NEAR(coordinate.longitudeDeg, 151.2093, 0.0002);
}

void testMgrsPrecisionAndValidation() {
    MgrsCoordinate coordinate;
    std::string error;
    CHECK(mgrsToWgs84("18SUJ", coordinate, error));
    CHECK(coordinate.precisionDigits == 0);
    CHECK(coordinate.latitudeDeg >= 32.0 && coordinate.latitudeDeg <= 40.0);

    CHECK(!mgrsToWgs84("18IUJ2339407395", coordinate, error));
    CHECK(!error.empty());
    CHECK(!mgrsToWgs84("61SUJ2339407395", coordinate, error));
    CHECK(!mgrsToWgs84("18SUJ123", coordinate, error));
}

void testWgs84MgrsEncodingAndRoundTrip() {
    const struct Fixture {
        double latitude;
        double longitude;
        const char* expected;
    } fixtures[] = {
        {38.8977, -77.0365, "18SUJ2339407395"},
        {48.8582,   2.2945, "31UDQ4825111932"},
        {-33.8688, 151.2093, "56HLH3436850948"},
    };

    for (const auto& fixture : fixtures) {
        std::string encoded;
        std::string error;
        CHECK(wgs84ToMgrs(fixture.latitude, fixture.longitude, 5, encoded, error));
        CHECK(encoded == fixture.expected);
        MgrsCoordinate decoded;
        CHECK(mgrsToWgs84(encoded, decoded, error));
        CHECK_NEAR(decoded.latitudeDeg, fixture.latitude, 0.00002);
        CHECK_NEAR(decoded.longitudeDeg, fixture.longitude, 0.00002);
    }

    std::string encoded;
    std::string error;
    CHECK(!wgs84ToMgrs(85.0, 0.0, 5, encoded, error));
    CHECK(!error.empty());
}

TerrainModel makeSlopedTerrain() {
    TerrainModel terrain;
    terrain.width = 101;
    terrain.height = 101;
    terrain.setCellSpacing(10.0f, 10.0f);
    terrain.heights.resize(terrain.width * terrain.height);
    for (int z = 0; z < terrain.height; ++z) {
        for (int x = 0; x < terrain.width; ++x) {
            terrain.heights[z * terrain.width + x] =
                0.10f * (x * terrain.metersPerCellX())
              + 0.05f * (z * terrain.metersPerCellZ());
        }
    }
    return terrain;
}

glm::vec2 projectToScreen(const glm::vec3& terrainPoint,
                          const glm::mat4& viewProjection,
                          float verticalScale,
                          int width,
                          int height) {
    const glm::vec4 clip = viewProjection * glm::vec4(
        terrainPoint.x, terrainPoint.y * verticalScale, terrainPoint.z, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {
        (ndc.x * 0.5f + 0.5f) * width - 0.5f,
        (0.5f - ndc.y * 0.5f) * height - 0.5f
    };
}

void testScreenRayHitsRenderedTerrain() {
    constexpr int width = 1000;
    constexpr int height = 800;
    constexpr float verticalScale = 2.5f;
    const TerrainModel terrain = makeSlopedTerrain();
    const glm::mat4 view = glm::lookAt(
        glm::vec3(1350.0f, 1450.0f, 1500.0f),
        glm::vec3(500.0f, 150.0f, 500.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), static_cast<float>(width) / height, 1.0f, 10000.0f);
    const glm::mat4 viewProjection = projection * view;

    const glm::vec3 expected{310.0f, terrain.heightAtWorld(310.0f, 720.0f), 720.0f};
    const glm::vec2 screen = projectToScreen(
        expected, viewProjection, verticalScale, width, height);
    glm::vec3 hit{0.0f};
    CHECK(TerrainRaycast::fromScreen(terrain, viewProjection, width, height,
                                     screen.x, screen.y, verticalScale, hit));
    CHECK_NEAR(hit.x, expected.x, 0.25f);
    CHECK_NEAR(hit.z, expected.z, 0.25f);
    CHECK_NEAR(hit.y, expected.y, 0.1f);
}

void testScreenRayRejectsSkyOutsideTerrain() {
    constexpr int width = 800;
    constexpr int height = 600;
    const TerrainModel terrain = makeSlopedTerrain();
    const glm::mat4 view = glm::lookAt(
        glm::vec3(500.0f, 500.0f, 1600.0f),
        glm::vec3(500.0f, 100.0f, 500.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), static_cast<float>(width) / height, 1.0f, 10000.0f);
    glm::vec3 hit{0.0f};
    CHECK(!TerrainRaycast::fromScreen(terrain, projection * view, width, height,
                                      width - 1.0f, 0.0f, 2.5f, hit));
}

} // namespace

int main() {
    testMgrsKnownLocations();
    testMgrsPrecisionAndValidation();
    testWgs84MgrsEncodingAndRoundTrip();
    testScreenRayHitsRenderedTerrain();
    testScreenRayRejectsSkyOutsideTerrain();
    if (failures == 0)
        std::cout << "Location and terrain-picking tests passed.\n";
    return failures == 0 ? 0 : 1;
}
