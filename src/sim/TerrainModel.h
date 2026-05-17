#pragma once
#include <vector>
#include <glm/glm.hpp>

class TerrainModel {
public:
    int   width    = 0;   // cells in X
    int   height   = 0;   // cells in Z
    float cellSize = 10.0f;  // metres per cell

    std::vector<float> heights;  // heights[z*width+x] in metres

    float heightAt(int x, int z) const;
    float heightAtWorld(float wx, float wz) const;  // bilinear interp
    bool  lineOfSight(glm::vec3 from, glm::vec3 to, int steps = 256) const;

    glm::vec3 worldSize() const {
        return { width * cellSize, 0.0f, height * cellSize };
    }
};
