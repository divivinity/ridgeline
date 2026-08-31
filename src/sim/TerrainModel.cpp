#include "TerrainModel.h"
#include <cmath>
#include <algorithm>
#include <limits>

static constexpr float kEffectiveEarthRadiusM = 6371000.0f * 4.0f / 3.0f;
static constexpr double kPi = 3.14159265358979323846;

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
    x = std::clamp(x, 0, width - 1);
    z = std::clamp(z, 0, height - 1);
    return heights[z * width + x];
}

float TerrainModel::heightAtWorld(float wx, float wz) const {
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

bool TerrainModel::lineOfSight(glm::vec3 from, glm::vec3 to, int steps) const {
    glm::vec3 dir = to - from;
    for (int i = 1; i < steps; ++i) {
        float t = static_cast<float>(i) / steps;
        glm::vec3 p = from + dir * t;
        if (p.y < heightAtWorld(p.x, p.z) + earthBulgeM(from, to, t))
            return false;
    }
    return true;
}

TerrainModel::LOSStatus TerrainModel::lineOfSightStatus(
        glm::vec3 from, glm::vec3 to, int steps) const {
    glm::vec3 dir = to - from;
    int blocked = 0;
    for (int i = 1; i < steps; ++i) {
        float t = static_cast<float>(i) / steps;
        glm::vec3 p = from + dir * t;
        if (p.y < heightAtWorld(p.x, p.z) + earthBulgeM(from, to, t))
            ++blocked;
    }
    if (blocked == 0)  return LOSStatus::Clear;
    if (blocked <= 3)  return LOSStatus::Diffracted;  // single knife-edge
    return LOSStatus::Blocked;
}

TerrainModel::PeakObstacle
TerrainModel::peakObstacle(glm::vec3 from, glm::vec3 to, int steps) const {
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
