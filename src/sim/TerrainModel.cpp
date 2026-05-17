#include "TerrainModel.h"
#include <cmath>
#include <algorithm>

float TerrainModel::heightAt(int x, int z) const {
    x = std::clamp(x, 0, width - 1);
    z = std::clamp(z, 0, height - 1);
    return heights[z * width + x];
}

float TerrainModel::heightAtWorld(float wx, float wz) const {
    float gx = wx / cellSize;
    float gz = wz / cellSize;
    int   x0 = static_cast<int>(gx), z0 = static_cast<int>(gz);
    float tx = gx - x0, tz = gz - z0;
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
        float ground = heightAtWorld(p.x, p.z);
        if (p.y < ground) return false;
    }
    return true;
}
