#pragma once
#include <vector>
#include <glm/glm.hpp>

class TerrainModel {
public:
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

    enum class LOSStatus { Clear, Diffracted, Blocked };

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
    LOSStatus lineOfSightStatus(glm::vec3 from, glm::vec3 to, int steps = 0) const;
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
