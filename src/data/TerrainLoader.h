#pragma once
#include "sim/TerrainModel.h"
#include <string>

class TerrainLoader {
public:
    // Creates a flat procedural terrain (for sandbox use)
    static TerrainModel makeFlat(int widthCells, int heightCells,
                                 float cellSize, float elevation = 0.0f);

    // Creates rolling hills procedurally (for demos without real DEM data)
    static TerrainModel makeHills(int widthCells, int heightCells,
                                  float cellSize, float amplitude = 40.0f);
};
