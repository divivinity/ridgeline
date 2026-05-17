#pragma once
#include <vector>
#include <glm/glm.hpp>

struct Antenna {
    glm::vec3 position;   // world XYZ (Y = up, meters AGL added to terrain height)
    float frequencyMHz = 150.0f;
    float powerW        = 50.0f;
    float gainDbi       = 3.0f;
    bool  isJammer      = false;
};

struct FieldGrid {
    int   width    = 0;
    int   height   = 0;
    float cellSize = 10.0f;  // metres per cell
    glm::vec2 originXZ{0.0f, 0.0f};  // world XZ of cell (0,0)
    std::vector<float> dbm;  // [y*width+x], NaN = no signal

    float at(int x, int y) const { return dbm[y * width + x]; }
    bool  valid() const { return !dbm.empty(); }
};
