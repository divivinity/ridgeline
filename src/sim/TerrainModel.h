#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

class TerrainModel {
public:
    // Configured provider for this loaded grid, not proof of data accuracy or
    // vertical datum. Keep it with the grid when runtime settings change.
    enum class ElevationProvider { Unspecified, Mapzen, Custom };
    ElevationProvider elevationProvider = ElevationProvider::Unspecified;

    // Ground sampling distance of the data this grid was built from, in metres.
    // Zero when unknown. Distinct from the grid spacing: resampling to a finer
    // grid does not create detail, and a grid coarser than its source discards
    // it. Reporting both is the only way to tell those apart on screen.
    float sourceResolutionM = 0.0f;

    struct GeoPoint {
        double latDeg = 0.0;
        double lonDeg = 0.0;
    };

    int   width    = 0;
    int   height   = 0;
    float cellSize = 10.0f;
    float cellSizeX = 0.0f;
    float cellSizeZ = 0.0f;

    bool   georeferenced = false;
    double originLatDeg  = 0.0;
    double originLonDeg  = 0.0;
    double cellLatDeg    = 0.0;
    double cellLonDeg    = 0.0;

    std::vector<float> heights;

    // Canopy height above ground in decimetres, parallel to `heights`. Zero
    // means bare ground; empty means no vegetation data at all, which is the
    // default so every existing construction site is unaffected.
    //
    // An attenuation model needs depth in metres, and only canopy height over
    // bare earth can produce it - cover percent modulates the result and
    // land-cover class cannot give depth at all.
    //
    // Sixteen bits, not eight. A byte of decimetres tops out at 25.5 m, which
    // was enough while the only writer was a hand-drawn height, but LANDFIRE
    // canopy height reaches 510 dm and its tallest class is open-ended above
    // that. Clamping would have halved the canopy over Douglas fir and redwood,
    // and canopy height is geometric: it decides whether the ray is inside the
    // crown at all. At the 1024x1024 ceiling this is 2 MiB against the 4 MiB
    // the float height grid already costs.
    std::vector<std::uint16_t> canopyHeightDm;

    // Fraction of ground covered by crown, 0-100, parallel to `heights`. May be
    // empty even when canopy heights are present: hand-drawn stands carry no
    // cover measurement. Empty means unknown, and unknown is read as 100 - the
    // conservative choice, and the one that leaves hand-drawn vegetation
    // behaving exactly as it did before cover existed.
    std::vector<std::uint8_t> canopyCoverPct;

    bool hasVegetation() const { return !canopyHeightDm.empty(); }
    float canopyHeightAt(int x, int z) const;
    // Crown cover as a 0-1 fraction. One where no cover data is carried.
    float canopyCoverAt(int x, int z) const;
    // Canopy top above sea level at a world position, or the bare ground height
    // where there is no vegetation data.
    float canopyTopAtWorld(float worldX, float worldZ) const;
    // Crown cover as a 0-1 fraction at a world position. One where unknown.
    float canopyCoverAtWorld(float worldX, float worldZ) const;

    // Throws before malformed or incomplete terrain can replace a live model.
    void validate() const;

    enum class LOSStatus { Clear, Diffracted, Blocked };

    // Metres of the path that pass through vegetation: below canopy top but
    // above bare ground. Zero when the model carries no vegetation.
    float foliageDepthM(glm::vec3 from, glm::vec3 to, int steps = 0) const;

    struct PeakObstacle {
        float clearanceM;
        float t;
    };

    float metersPerCellX() const;
    float metersPerCellZ() const;
    void  setCellSpacing(float eastWestM, float southNorthM);
    void  setGeoReference(double swLatDeg, double swLonDeg,
                          double sampleLatDeg, double sampleLonDeg);
    bool  hasGeoReference() const { return georeferenced; }

    GeoPoint worldToLatLon(float wx, float wz) const;
    glm::vec2 latLonToWorld(double latDeg, double lonDeg) const;

    static double metersPerDegreeLat(double latDeg);
    static double metersPerDegreeLon(double latDeg);

    float heightAt(int x, int z) const;
    float heightAtWorld(float wx, float wz) const;

    // steps <= 0 selects a path-length/terrain-resolution adaptive sample count.
    // Explicit positive values remain supported for deterministic tests.
    int adaptivePathSteps(glm::vec3 from, glm::vec3 to,
                          int minimum = 32, int maximum = 2048) const;
    bool lineOfSight(glm::vec3 from, glm::vec3 to, int steps = 0) const;
    // outFoliageDepthM, when given, receives the vegetation depth measured on
    // the same walk. An out-parameter rather than a richer return type because
    // four call sites outside the solver consume this enum directly.
    LOSStatus lineOfSightStatus(glm::vec3 from, glm::vec3 to, int steps = 0,
                                float* outFoliageDepthM = nullptr) const;
    PeakObstacle peakObstacle(glm::vec3 from, glm::vec3 to, int steps = 0) const;
    float fresnelClearanceRatio(glm::vec3 from, glm::vec3 to,
                                float frequencyMHz, int steps = 0) const;

    glm::vec3 worldSize() const {
        return {
            (width  > 1 ? static_cast<float>(width  - 1) * metersPerCellX() : 0.0f),
            0.0f,
            (height > 1 ? static_cast<float>(height - 1) * metersPerCellZ() : 0.0f)
        };
    }
};
