#pragma once
#include <vector>
#include <glm/glm.hpp>

class TerrainModel {
public:
    struct GeoPoint {
        double latDeg = 0.0;
        double lonDeg = 0.0;
    };

    int   width    = 0;   // cells in X
    int   height   = 0;   // cells in Z
    float cellSize = 10.0f;  // legacy fallback, metres per cell
    float cellSizeX = 0.0f;  // metres per sample east-west; 0 uses cellSize
    float cellSizeZ = 0.0f;  // metres per sample south-north; 0 uses cellSize

    bool   georeferenced = false;
    double originLatDeg  = 0.0;  // southwest sample latitude
    double originLonDeg  = 0.0;  // southwest sample longitude
    double cellLatDeg    = 0.0;  // degrees per sample northward
    double cellLonDeg    = 0.0;  // degrees per sample eastward

    std::vector<float> heights;  // heights[z*width+x] in metres

    enum class LOSStatus { Clear, Diffracted, Blocked };

    struct PeakObstacle {
        float clearanceM;  // terrain_height − LOS_height at worst point; negative = clear
        float t;           // fraction along path [0..1] where peak occurs
    };

    PeakObstacle peakObstacle(glm::vec3 from, glm::vec3 to, int steps = 256) const;

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

    float     heightAt(int x, int z) const;
    float     heightAtWorld(float wx, float wz) const;
    bool      lineOfSight(glm::vec3 from, glm::vec3 to, int steps = 256) const;
    // Clear = no obstruction; Diffracted = single knife-edge obstacle;
    // Blocked = multiple obstructions (ray tracing path unusable).
    LOSStatus lineOfSightStatus(glm::vec3 from, glm::vec3 to, int steps = 256) const;
    float fresnelClearanceRatio(glm::vec3 from, glm::vec3 to,
                                float frequencyMHz, int steps = 256) const;

    glm::vec3 worldSize() const {
        return {
            (width  > 1 ? static_cast<float>(width  - 1) * metersPerCellX() : 0.0f),
            0.0f,
            (height > 1 ? static_cast<float>(height - 1) * metersPerCellZ() : 0.0f)
        };
    }
};
