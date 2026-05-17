#include "TerrainLoader.h"
#include <cmath>

TerrainModel TerrainLoader::makeFlat(int w, int h, float cell, float elev) {
    TerrainModel t;
    t.width = w; t.height = h; t.cellSize = cell;
    t.heights.assign(w * h, elev);
    return t;
}

TerrainModel TerrainLoader::makeHills(int w, int h, float cell, float amp) {
    TerrainModel t;
    t.width = w; t.height = h; t.cellSize = cell;
    t.heights.resize(w * h);
    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            float nx = static_cast<float>(x) / w;
            float nz = static_cast<float>(z) / h;
            t.heights[z * w + x] =
                amp * (0.5f * std::sin(nx * 6.28f * 2.0f) * std::cos(nz * 6.28f * 1.5f)
                     + 0.3f * std::sin(nx * 6.28f * 5.0f + 1.0f)
                     + 0.2f * std::cos(nz * 6.28f * 4.0f + 0.5f));
        }
    }
    return t;
}
