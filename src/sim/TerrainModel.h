#pragma once
#include <vector>
#include <glm/glm.hpp>

class TerrainModel {
public:
    int   width    = 0;   // cells in X
    int   height   = 0;   // cells in Z
    float cellSize = 10.0f;  // metres per cell

    std::vector<float> heights;  // heights[z*width+x] in metres

    enum class LOSStatus { Clear, Diffracted, Blocked };

    struct PeakObstacle {
        float clearanceM;  // terrain_height − LOS_height at worst point; negative = clear
        float t;           // fraction along path [0..1] where peak occurs
    };

    PeakObstacle peakObstacle(glm::vec3 from, glm::vec3 to, int steps = 256) const;

    float     heightAt(int x, int z) const;
    float     heightAtWorld(float wx, float wz) const;
    bool      lineOfSight(glm::vec3 from, glm::vec3 to, int steps = 256) const;
    // Clear = no obstruction; Diffracted = single knife-edge obstacle;
    // Blocked = multiple obstructions (ray tracing path unusable).
    LOSStatus lineOfSightStatus(glm::vec3 from, glm::vec3 to, int steps = 256) const;

    glm::vec3 worldSize() const {
        return { width * cellSize, 0.0f, height * cellSize };
    }
};
