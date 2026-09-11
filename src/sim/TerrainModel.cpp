#include "TerrainModel.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

static constexpr float kEffectiveEarthRadiusM = 6371000.0f * 4.0f / 3.0f;
static constexpr double kPi = 3.14159265358979323846;

void TerrainModel::validate() const {
    if (width < 1 || height < 1 ||
        heights.size() / static_cast<std::size_t>(width) != static_cast<std::size_t>(height) ||
        heights.size() % static_cast<std::size_t>(width) != 0)
        throw std::invalid_argument("Coverage requires a complete terrain grid.");
    const float sx = metersPerCellX(), sz = metersPerCellZ();
    if (!std::isfinite(sx) || !std::isfinite(sz) || sx <= 0 || sz <= 0 ||
        !std::isfinite(cellSizeX) || !std::isfinite(cellSizeZ) || cellSizeX < 0 || cellSizeZ < 0)
        throw std::invalid_argument("Terrain sample spacing must be finite and positive.");
    if (!canopyHeightDm.empty() && canopyHeightDm.size() != heights.size())
        throw std::invalid_argument(
            "Vegetation grid must match the terrain grid sample for sample.");
    if (std::any_of(heights.begin(), heights.end(), [](float h) { return !std::isfinite(h); }))
        throw std::invalid_argument("Terrain contains missing elevation. Load a complete elevation dataset before solving.");
    const glm::vec3 extent = worldSize();
    if (!std::isfinite(extent.x) || !std::isfinite(extent.z))
        throw std::invalid_argument("Terrain extent must be finite.");
    if (georeferenced &&
        (!std::isfinite(originLatDeg) || !std::isfinite(originLonDeg) ||
         !std::isfinite(cellLatDeg) || !std::isfinite(cellLonDeg) ||
         cellLatDeg <= 0 || cellLonDeg <= 0 ||
         originLatDeg < -90 || originLatDeg + (height - 1.0) * cellLatDeg > 90 ||
         originLonDeg < -180 || originLonDeg > 180 || (width - 1.0) * cellLonDeg >= 180))
        throw std::invalid_argument("Terrain has an invalid north-up WGS84 reference.");
}

static double normalizeLongitude(double lonDeg) {
    double wrapped = std::fmod(lonDeg + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped - 180.0;
}

static double longitudeDelta(double lonDeg, double originLonDeg) {
    return normalizeLongitude(lonDeg - originLonDeg);
}

static float earthBulgeM(const glm::vec3& from, const glm::vec3& to, float t) {
    const float totalM = glm::distance(glm::vec2(from.x, from.z),
                                       glm::vec2(to.x,   to.z));
    const float d1 = totalM * t;
    const float d2 = totalM * (1.0f - t);
    return (d1 * d2) / (2.0f * kEffectiveEarthRadiusM);
}

float TerrainModel::metersPerCellX() const {
    return cellSizeX > 0.0f ? cellSizeX : cellSize;
}

float TerrainModel::metersPerCellZ() const {
    return cellSizeZ > 0.0f ? cellSizeZ : cellSize;
}

void TerrainModel::setCellSpacing(float eastWestM, float southNorthM) {
    cellSizeX = eastWestM;
    cellSizeZ = southNorthM;
    cellSize = southNorthM;
}

void TerrainModel::setGeoReference(double swLatDeg, double swLonDeg,
                                   double sampleLatDeg, double sampleLonDeg) {
    originLatDeg = swLatDeg;
    originLonDeg = normalizeLongitude(swLonDeg);
    cellLatDeg = sampleLatDeg;
    cellLonDeg = sampleLonDeg;
    georeferenced = sampleLatDeg != 0.0 && sampleLonDeg != 0.0;
}

TerrainModel::GeoPoint TerrainModel::worldToLatLon(float wx, float wz) const {
    if (!georeferenced) return {};
    const double xSamples = wx / static_cast<double>(metersPerCellX());
    const double zSamples = wz / static_cast<double>(metersPerCellZ());
    return {
        originLatDeg + zSamples * cellLatDeg,
        normalizeLongitude(originLonDeg + xSamples * cellLonDeg)
    };
}

glm::vec2 TerrainModel::latLonToWorld(double latDeg, double lonDeg) const {
    if (!georeferenced) return {0.0f, 0.0f};
    const double xSamples = longitudeDelta(lonDeg, originLonDeg) / cellLonDeg;
    const double zSamples = (latDeg - originLatDeg) / cellLatDeg;
    return {
        static_cast<float>(xSamples * metersPerCellX()),
        static_cast<float>(zSamples * metersPerCellZ())
    };
}

double TerrainModel::metersPerDegreeLat(double latDeg) {
    const double r = latDeg * kPi / 180.0;
    return 111132.92
         - 559.82 * std::cos(2.0 * r)
         + 1.175  * std::cos(4.0 * r)
         - 0.0023 * std::cos(6.0 * r);
}

double TerrainModel::metersPerDegreeLon(double latDeg) {
    const double r = latDeg * kPi / 180.0;
    return 111412.84 * std::cos(r)
         - 93.5     * std::cos(3.0 * r)
         + 0.118    * std::cos(5.0 * r);
}

float TerrainModel::heightAt(int x, int z) const {
    if (width <= 0 || height <= 0 || heights.empty()) return 0.0f;
    x = std::clamp(x, 0, width - 1);
    z = std::clamp(z, 0, height - 1);
    return heights[static_cast<std::size_t>(z) * width + x];
}

float TerrainModel::heightAtWorld(float wx, float wz) const {
    if (width < 2 || height < 2 || heights.empty()) return 0.0f;
    float gx = wx / metersPerCellX();
    float gz = wz / metersPerCellZ();
    int   x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, width  - 2);
    int   z0 = std::clamp(static_cast<int>(std::floor(gz)), 0, height - 2);
    float tx = std::clamp(gx - static_cast<float>(x0), 0.0f, 1.0f);
    float tz = std::clamp(gz - static_cast<float>(z0), 0.0f, 1.0f);
    float h00 = heightAt(x0,   z0);
    float h10 = heightAt(x0+1, z0);
    float h01 = heightAt(x0,   z0+1);
    float h11 = heightAt(x0+1, z0+1);
    return h00*(1-tx)*(1-tz) + h10*tx*(1-tz) + h01*(1-tx)*tz + h11*tx*tz;
}

int TerrainModel::adaptivePathSteps(glm::vec3 from, glm::vec3 to,
                                    int minimum, int maximum) const {
    minimum = std::max(minimum, 4);
    maximum = std::max(maximum, minimum);
    const float pathM = glm::distance(glm::vec2(from.x, from.z),
                                      glm::vec2(to.x, to.z));
    const float terrainSampleM = std::max(
        1.0f, std::min(metersPerCellX(), metersPerCellZ()));
    // Sample at least twice per terrain cell along the path so narrow ridges are
    // not skipped, while bounding work for long engineering paths.
    const float targetSpacingM = std::max(2.0f, terrainSampleM * 0.5f);
    const int requested = static_cast<int>(std::ceil(pathM / targetSpacingM)) + 1;
    return std::clamp(requested, minimum, maximum);
}

bool TerrainModel::lineOfSight(glm::vec3 from, glm::vec3 to, int steps) const {
    if (steps <= 0) steps = adaptivePathSteps(from, to);
    glm::vec3 dir = to - from;
    for (int i = 1; i < steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        glm::vec3 p = from + dir * t;
        if (p.y < heightAtWorld(p.x, p.z) + earthBulgeM(from, to, t))
            return false;
    }
    return true;
}

float TerrainModel::canopyHeightAt(int x, int z) const {
    if (canopyHeightDm.empty()) return 0.0f;
    if (x < 0 || z < 0 || x >= width || z >= height) return 0.0f;
    const std::size_t index = static_cast<std::size_t>(z) * width + x;
    if (index >= canopyHeightDm.size()) return 0.0f;
    return static_cast<float>(canopyHeightDm[index]) * 0.1f;
}

float TerrainModel::canopyTopAtWorld(float worldX, float worldZ) const {
    const float ground = heightAtWorld(worldX, worldZ);
    if (canopyHeightDm.empty()) return ground;
    // Nearest sample rather than bilinear: canopy is a discontinuous surface at
    // a stand edge, and interpolating it would invent a ramp that is not there.
    const float sx = metersPerCellX(), sz = metersPerCellZ();
    if (!(sx > 0.0f) || !(sz > 0.0f)) return ground;
    const int x = static_cast<int>(std::lround(worldX / sx));
    const int z = static_cast<int>(std::lround(worldZ / sz));
    return ground + canopyHeightAt(x, z);
}

float TerrainModel::foliageDepthM(glm::vec3 from, glm::vec3 to, int steps) const {
    if (canopyHeightDm.empty()) return 0.0f;
    if (steps <= 0) steps = adaptivePathSteps(from, to);
    if (steps < 2) return 0.0f;
    const glm::vec3 dir = to - from;
    const float pathLength = glm::length(dir);
    if (!(pathLength > 0.0f)) return 0.0f;
    const float segment = pathLength / static_cast<float>(steps);

    float depth = 0.0f;
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec3 p = from + dir * t;
        const float bulge = earthBulgeM(from, to, t);
        const float ground = heightAtWorld(p.x, p.z) + bulge;
        const float canopy = canopyTopAtWorld(p.x, p.z) + bulge;
        // Inside the canopy: under its top, but not already underground. A
        // sample below ground is terrain blockage, which diffraction handles.
        if (p.y < canopy && p.y >= ground) depth += segment;
    }
    return depth;
}

TerrainModel::LOSStatus TerrainModel::lineOfSightStatus(
        glm::vec3 from, glm::vec3 to, int steps, float* outFoliageDepthM) const {
    if (steps <= 0) steps = adaptivePathSteps(from, to);
    glm::vec3 dir = to - from;
    const bool wantFoliage = outFoliageDepthM != nullptr && !canopyHeightDm.empty();
    const float segment = steps > 0 ? glm::length(dir) / static_cast<float>(steps) : 0.0f;
    float foliage = 0.0f;
    int blocked = 0;
    for (int i = 1; i < steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        glm::vec3 p = from + dir * t;
        const float bulge = earthBulgeM(from, to, t);
        const float ground = heightAtWorld(p.x, p.z) + bulge;
        if (p.y < ground) ++blocked;
        else if (wantFoliage && p.y < canopyTopAtWorld(p.x, p.z) + bulge)
            foliage += segment;
    }
    if (outFoliageDepthM) *outFoliageDepthM = foliage;
    if (blocked == 0) return LOSStatus::Clear;
    // A very narrow obstruction is treated as one dominant knife edge; longer
    // obstructed intervals are handed to multiple-edge diffraction/ITM.
    const int singleEdgeSamples = std::max(2, steps / 64);
    if (blocked <= singleEdgeSamples) return LOSStatus::Diffracted;
    return LOSStatus::Blocked;
}

TerrainModel::PeakObstacle
TerrainModel::peakObstacle(glm::vec3 from, glm::vec3 to, int steps) const {
    if (steps <= 0) steps = adaptivePathSteps(from, to);
    glm::vec3 dir = to - from;
    float maxExcess = -std::numeric_limits<float>::infinity();
    float maxT      = 0.5f;
    for (int i = 1; i < steps; ++i) {
        float t      = static_cast<float>(i) / static_cast<float>(steps);
        glm::vec3 p  = from + dir * t;
        float excess = heightAtWorld(p.x, p.z) + earthBulgeM(from, to, t) - p.y;
        if (excess > maxExcess) {
            maxExcess = excess;
            maxT      = t;
        }
    }
    return { maxExcess, maxT };
}

float TerrainModel::fresnelClearanceRatio(glm::vec3 from, glm::vec3 to,
                                           float frequencyMHz, int steps) const {
    const float pathM = glm::distance(from, to);
    if (pathM <= 1.0f || frequencyMHz <= 0.0f) return 1.0f;
    if (steps <= 0) steps = adaptivePathSteps(from, to);

    const float lambda = 3.0e8f / (frequencyMHz * 1.0e6f);
    glm::vec3 dir = to - from;
    float minRatio = std::numeric_limits<float>::infinity();

    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float d1 = std::max(pathM * t, 1.0f);
        const float d2 = std::max(pathM * (1.0f - t), 1.0f);
        const float radiusM = std::sqrt(lambda * d1 * d2 / pathM);
        if (radiusM <= 0.0f) continue;

        glm::vec3 p = from + dir * t;
        const float obstructionM = heightAtWorld(p.x, p.z) + earthBulgeM(from, to, t);
        const float clearanceM = p.y - obstructionM;
        minRatio = std::min(minRatio, clearanceM / radiusM);
    }

    return std::isfinite(minRatio) ? minRatio : 1.0f;
}
